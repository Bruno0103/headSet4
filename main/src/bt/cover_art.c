/**
 * @file cover_art.c
 * @brief Implementação do cliente AVRCP Cover Art (BIP) e decodificador JPEG para LVGL 9.
 *
 * Em conformidade com AGENTS.md (§2, §3, §4.2, WP 6.1 e Reparo Q.8):
 * - Acumula pacotes do protocolo OBEX diretamente em buffer alocado na PSRAM externa de 8 MB.
 * - Utiliza a biblioteca Tiny JPEG Decompressor (tjpgd) embutida na ROM do ESP32 para
 *   decodificar a imagem diretamente para matriz de pixels RGB565 (200x200).
 * - Cópia profunda e atômica de dados garantindo isolamento total contra Use-After-Free.
 * - Integração perfeita com o modelo reativo de UI (ui_model) através da fila thread-safe.
 */

#include "cover_art.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp32/rom/tjpgd.h"
#include "ui_model.h"
#include "ui_gen.h"

static const char *TAG = "cover_art";

#define TJPGD_WORKBUF_SIZE 4096

/* Estado do serviço de Cover Art */
typedef struct {
    bool connected;                       /* Conexão OBEX/BIP ativa */
    bool getting;                         /* Transmissão de thumbnail em andamento */
    uint32_t get_start_ms;                /* Timestamp de início da transmissão para watchdog anti-deadlock */
    char image_hdl_old[16];               /* Identificador anterior para evitar downloads redundantes */
    uint32_t jpeg_size;                   /* Tamanho atual do JPEG recebido */
    uint32_t jpeg_capacity;               /* Capacidade alocada do buffer JPEG em PSRAM */
    uint8_t *jpeg_data;                   /* Buffer acumulador de dados JPEG em PSRAM */
    uint16_t *rgb565_pixels;              /* Bitmap decodificado 200x200 em PSRAM */
    bool has_custom_image;                /* Indicador de capa customizada ativa */
    SemaphoreHandle_t mutex;              /* Mutex para proteção de buffers */
} cover_art_state_t;

static cover_art_state_t s_state;

/* Descritor de imagem nativo do LVGL 9 */
static lv_image_dsc_t s_cover_art_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = COVER_ART_WIDTH,
        .h = COVER_ART_HEIGHT,
        .stride = COVER_ART_WIDTH * sizeof(uint16_t),
        .reserved_2 = 0,
    },
    .data_size = COVER_ART_WIDTH * COVER_ART_HEIGHT * sizeof(uint16_t),
    .data = NULL,
    .reserved = NULL,
};

/* Sessão de decodificação JPEG */
typedef struct {
    const uint8_t *in_data;
    size_t in_size;
    size_t in_offset;
    uint16_t *out_pixels;
    uint16_t target_w;
    uint16_t target_h;
    int offset_x;
    int offset_y;
} jpeg_decode_session_t;

/* Função de entrada para o Tiny JPEG Decompressor (ROM do ESP32) */
static UINT tjpgd_infunc(JDEC *jd, BYTE *buff, UINT ndata)
{
    jpeg_decode_session_t *sess = (jpeg_decode_session_t *)jd->device;
    if (!sess) return 0;

    size_t remain = sess->in_size - sess->in_offset;
    size_t to_read = (ndata < remain) ? ndata : remain;
    if (to_read == 0) return 0;

    if (buff) {
        memcpy(buff, sess->in_data + sess->in_offset, to_read);
    }
    sess->in_offset += to_read;
    return (UINT)to_read;
}

/* Função de saída que grava os blocos de pixels RGB888 convertendo para RGB565 */
static UINT tjpgd_outfunc(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_decode_session_t *sess = (jpeg_decode_session_t *)jd->device;
    BYTE *rgb = (BYTE *)bitmap;
    int block_w = rect->right - rect->left + 1;

    for (int y = rect->top; y <= rect->bottom; y++) {
        int dst_y = y + sess->offset_y;
        if (dst_y < 0 || dst_y >= sess->target_h) {
            rgb += block_w * 3;
            continue;
        }

        for (int x = rect->left; x <= rect->right; x++) {
            int dst_x = x + sess->offset_x;
            BYTE r = *rgb++;
            BYTE g = *rgb++;
            BYTE b = *rgb++;

            if (dst_x >= 0 && dst_x < sess->target_w) {
                /* Converte RGB888 (24 bits) para RGB565 Little Endian (16 bits) */
                uint16_t pixel = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                sess->out_pixels[dst_y * sess->target_w + dst_x] = pixel;
            }
        }
    }
    return 1; /* Retorna 1 para continuar a descompressão */
}

/* Limpa o buffer acumulador de dados JPEG recebidos */
static void free_jpeg_accumulation_buffer(void)
{
    if (s_state.jpeg_data) {
        free(s_state.jpeg_data);
        s_state.jpeg_data = NULL;
    }
    s_state.jpeg_size = 0;
    s_state.jpeg_capacity = 0;
}

esp_err_t cover_art_init(void)
{
    memset(&s_state, 0, sizeof(s_state));

    s_state.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_state.mutex, ESP_ERR_NO_MEM, TAG, "Falha ao criar mutex");

    /* Aloca o buffer de saída RGB565 de 200x200 (80.000 bytes) exclusivamente na PSRAM */
    const size_t pixel_buf_size = COVER_ART_WIDTH * COVER_ART_HEIGHT * sizeof(uint16_t);
    s_state.rgb565_pixels = (uint16_t *)heap_caps_calloc(1, pixel_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_state.rgb565_pixels) {
        ESP_LOGW(TAG, "PSRAM indisponivel para buffer RGB565 de Cover Art; tentando heap interno");
        s_state.rgb565_pixels = (uint16_t *)calloc(1, pixel_buf_size);
    }
    ESP_RETURN_ON_FALSE(s_state.rgb565_pixels, ESP_ERR_NO_MEM, TAG, "Erro ao alocar buffer RGB565");

    /* Aponta o descritor LVGL para a memória alocada */
    s_cover_art_dsc.data = (const uint8_t *)s_state.rgb565_pixels;

    ESP_LOGI(TAG, "Subsistema de Cover Art inicializado com sucesso (200x200 RGB565, 80 KB em PSRAM)");
    return ESP_OK;
}

void cover_art_srv_open(void)
{
    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    free_jpeg_accumulation_buffer();
    s_state.getting = false;
    memset(s_state.image_hdl_old, 0, sizeof(s_state.image_hdl_old));
    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
    ESP_LOGI(TAG, "Servico de Cover Art aberto localmente");
}

void cover_art_srv_close(void)
{
    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    s_state.connected = false;
    s_state.getting = false;
    free_jpeg_accumulation_buffer();
    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
    ESP_LOGI(TAG, "Servico de Cover Art fechado");
}

void cover_art_srv_connect(uint16_t mtu)
{
    ESP_LOGI(TAG, "Iniciando conexao OBEX Cover Art (MTU=%u)...", mtu);
    esp_err_t err = esp_avrc_ct_cover_art_connect(mtu);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao conectar cliente Cover Art: %s", esp_err_to_name(err));
    }
}

void cover_art_srv_set_connected(bool connected)
{
    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    s_state.connected = connected;
    if (!connected) {
        s_state.getting = false;
    }
    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
}

void cover_art_srv_ca_req(void)
{
    if (s_state.connected) {
        ESP_LOGI(TAG, "Requisitando identificador de Cover Art (MD_ATTR_COVER_ART)...");
        esp_avrc_ct_send_metadata_cmd(0, ESP_AVRC_MD_ATTR_COVER_ART);
    }
}

void cover_art_reset_getting(void)
{
    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    s_state.getting = false;
    free_jpeg_accumulation_buffer();
    memset(s_state.image_hdl_old, 0, sizeof(s_state.image_hdl_old));
    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
    ESP_LOGD(TAG, "Estado de obtencao de Cover Art (getting) e historico de handle resetados");
}

void cover_art_srv_ct_metadata_update(const uint8_t *image_handle, int len)
{
    if (!image_handle || len <= 0) return;

    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    /* Watchdog de recuperação automática: se 'getting' estiver travado por mais de 8s, destrava */
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_state.getting && (now - s_state.get_start_ms > 8000)) {
        ESP_LOGW(TAG, "Watchdog Cover Art: download anterior expirou (>8s), destravando estado");
        free_jpeg_accumulation_buffer();
        s_state.getting = false;
    }

    if (s_state.connected && !s_state.getting) {
        char hdl_str[16] = {0};
        size_t cpy_len = (len < sizeof(hdl_str) - 1) ? (size_t)len : (sizeof(hdl_str) - 1);
        memcpy(hdl_str, image_handle, cpy_len);
        hdl_str[cpy_len] = '\0';

        /* Verifica se o identificador é novo para evitar downloads redundantes */
        if (strncmp(s_state.image_hdl_old, hdl_str, sizeof(s_state.image_hdl_old)) != 0) {
            ESP_LOGI(TAG, "Novo Image Handle detectado: '%s' (anterior: '%s')", hdl_str, s_state.image_hdl_old);
            strncpy(s_state.image_hdl_old, hdl_str, sizeof(s_state.image_hdl_old) - 1);

            free_jpeg_accumulation_buffer();
            s_state.getting = true;
            s_state.get_start_ms = now;

            if (s_state.mutex) xSemaphoreGive(s_state.mutex);

            /* Requisita o thumbnail vinculado ao identificador */
            esp_err_t err = esp_avrc_ct_cover_art_get_linked_thumbnail((uint8_t *)image_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Falha ao requisitar linked thumbnail: %s", esp_err_to_name(err));
                if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
                s_state.getting = false;
                if (s_state.mutex) xSemaphoreGive(s_state.mutex);
            }
            return;
        } else {
            ESP_LOGD(TAG, "Image Handle identico ao anterior ('%s'), download ignorado", hdl_str);
        }
    }

    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
}

void cover_art_save_chunk(const uint8_t *p_data, uint16_t data_len)
{
    if (!p_data || data_len == 0) return;

    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    /* Realoca o buffer na PSRAM para comportar o novo fragmento */
    uint32_t needed = s_state.jpeg_size + data_len;
    if (needed > s_state.jpeg_capacity) {
        uint32_t new_cap = needed + 4096; /* Crescimento incremental com folga */
        uint8_t *new_buf = (uint8_t *)heap_caps_realloc(s_state.jpeg_data, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_buf) {
            new_buf = (uint8_t *)realloc(s_state.jpeg_data, new_cap);
        }
        if (!new_buf) {
            ESP_LOGE(TAG, "Erro de memoria ao expandir buffer JPEG (%lu bytes)", (unsigned long)new_cap);
            free_jpeg_accumulation_buffer();
            s_state.getting = false;
            if (s_state.mutex) xSemaphoreGive(s_state.mutex);
            return;
        }
        s_state.jpeg_data = new_buf;
        s_state.jpeg_capacity = new_cap;
    }

    /* Copia os dados brutos recebidos */
    memcpy(s_state.jpeg_data + s_state.jpeg_size, p_data, data_len);
    s_state.jpeg_size += data_len;

    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
}

void cover_art_set_image_final(bool final)
{
    if (!final) return;

    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    ESP_LOGI(TAG, "Pacote Cover Art finalizado! Total recebido: %lu bytes. Decodificando JPEG...",
             (unsigned long)s_state.jpeg_size);

    if (s_state.jpeg_data && s_state.jpeg_size > 0) {
        esp_err_t err = cover_art_decode_jpeg_buffer(s_state.jpeg_data, s_state.jpeg_size);
        if (err == ESP_OK) {
            s_state.has_custom_image = true;
            /* Notifica o modelo reativo de UI de forma assíncrona e thread-safe.
             * ISOLAMENTO ARQUITETURAL: Não modificamos o ponteiro global image_image,
             * garantindo que screen_main e screen_main_gif mantenham seus próprios papéis de parede.
             * A capa é repassada exclusivamente para a tela screen_main_cover_art. */
            ui_model_post_cover_art(&s_cover_art_dsc);
            ESP_LOGI(TAG, "Cover Art decodificada e despachada exclusivamente para screen_main_cover_art!");
        } else {
            ESP_LOGE(TAG, "Falha ao decodificar imagem JPEG de Cover Art");
        }
    }

    /* Libera o buffer temporário de recepção após a decodificação */
    free_jpeg_accumulation_buffer();
    s_state.getting = false;

    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
}

esp_err_t cover_art_decode_jpeg_buffer(const uint8_t *jpeg_buf, size_t jpeg_len)
{
    if (!jpeg_buf || jpeg_len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Aloca a área de trabalho recomendada de 4096 bytes para o TJpgDec */
    void *work_pool = heap_caps_malloc(TJPGD_WORKBUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work_pool) {
        work_pool = malloc(TJPGD_WORKBUF_SIZE);
    }
    ESP_RETURN_ON_FALSE(work_pool, ESP_ERR_NO_MEM, TAG, "Erro ao alocar workbuf TJpgDec");

    jpeg_decode_session_t sess = {
        .in_data = jpeg_buf,
        .in_size = jpeg_len,
        .in_offset = 0,
        .out_pixels = s_state.rgb565_pixels,
        .target_w = COVER_ART_WIDTH,
        .target_h = COVER_ART_HEIGHT,
        .offset_x = 0,
        .offset_y = 0,
    };

    /* Aloca a estrutura JDEC dinamicamente para não consumir 3,1 KB da pilha da BtAppTask */
    JDEC *jd = (JDEC *)malloc(sizeof(JDEC));
    if (!jd) {
        ESP_LOGE(TAG, "Falha ao alocar memoria para descritor JDEC");
        free(work_pool);
        return ESP_ERR_NO_MEM;
    }

    JRESULT rc = jd_prepare(jd, tjpgd_infunc, work_pool, TJPGD_WORKBUF_SIZE, &sess);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "jd_prepare falhou com codigo %d", rc);
        free(jd);
        free(work_pool);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Dimensoes originais do JPEG: %u x %u pixels", jd->width, jd->height);

    /* Determina a escala adequada (0: 1/1, 1: 1/2, 2: 1/4, 3: 1/8) */
    uint8_t scale = 0;
    if (jd->width > 400 || jd->height > 400) {
        scale = 2; /* Reduz para 1/4 */
    } else if (jd->width > 200 || jd->height > 200) {
        scale = 1; /* Reduz para 1/2 */
    }

    uint16_t scaled_w = jd->width >> scale;
    uint16_t scaled_h = jd->height >> scale;

    /* Centraliza a imagem no quadro de 200x200 */
    sess.offset_x = (COVER_ART_WIDTH - (int)scaled_w) / 2;
    sess.offset_y = (COVER_ART_HEIGHT - (int)scaled_h) / 2;

    /* Preenche previamente o buffer com a cor de fundo do card (#1A2026 -> RGB565 0x1905) */
    for (int i = 0; i < COVER_ART_WIDTH * COVER_ART_HEIGHT; i++) {
        s_state.rgb565_pixels[i] = 0x1905;
    }

    /* Executa a descompressão para os pixels RGB565 */
    rc = jd_decomp(jd, tjpgd_outfunc, scale);
    free(jd);
    free(work_pool);

    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "jd_decomp falhou com codigo %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Decodificacao concluida! Imagem ajustada para %u x %u no quadro 200x200", scaled_w, scaled_h);
    return ESP_OK;
}

const lv_image_dsc_t *cover_art_get_image_dsc(void)
{
    if (s_state.has_custom_image && s_cover_art_dsc.data != NULL) {
        return &s_cover_art_dsc;
    }
    /* Fallback para a imagem padrão incluída na compilação */
    extern const void * image_image_data;
    return (const lv_image_dsc_t *)&image_image_data;
}

bool cover_art_has_image(void)
{
    return s_state.has_custom_image;
}

void cover_art_reset_to_default(void)
{
    if (s_state.mutex) xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    s_state.has_custom_image = false;
    extern const void * image_image_data;
    ui_model_post_cover_art((const lv_image_dsc_t *)&image_image_data);
    if (s_state.mutex) xSemaphoreGive(s_state.mutex);
    ESP_LOGI(TAG, "Cover Art restaurada para a imagem padrao do sistema");
}
