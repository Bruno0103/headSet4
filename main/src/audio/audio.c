#include "audio.h"

#include "esp_check.h"

#include "audio_codec.h"
#include "audio_io.h"

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(audio_codec_init(), "audio", "codec");
    ESP_RETURN_ON_ERROR(audio_io_init(), "audio", "io");
    return ESP_OK;
}
