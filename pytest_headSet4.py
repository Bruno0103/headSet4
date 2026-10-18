# -*- coding: utf-8 -*-
"""
================================================================================================
HEADSET4 — CONSOLE INTERATIVO DE BANCADA (HIL) & SUÍTE DE TESTES PYTEST-EMBEDDED
================================================================================================
Alvo de Hardware : ESP32-WROVER-E (Dual-Core Xtensa LX6 @ 240 MHz · 8 MB PSRAM Externa)
Framework        : ESP-IDF v6.x (FreeRTOS SMP · LVGL 9 · Codec WM8960 · Dual-Slot BT)
Documento Padrão : AGENTS.md (§0.3, §1.2, §2, §3, §4.1–§4.3, §6)

Este script fornece DUAS modalidades complementares de validação:

1. CONSOLE INTERATIVO & MONITOR SERIAL DE BANCADA (Execução direta via Python):
   $ python pytest_headSet4.py [--port COMx] [--baud 115200]
   - Conversa diretamente com o ESP32 via porta serial UART.
   - Analisa e decodifica os logs em tempo real com destaque em cores ANSI.
   - Mantém um Dashboard visual da integridade de todos os subsistemas de hardware.
   - Permite enviar comandos/mensagens de texto pela serial em tempo real.
   - Permite acionar reset de hardware (via DTR/RTS) para auditar a sequência de boot fria.
   - Avalia a saúde de memória (Heap Interno e mapeamento da PSRAM externa de 8 MB).
   - Detecta e alerta instantaneamente pânicos, Guru Meditation Errors e vazamentos.

2. SUÍTE AUTOMATIZADA PYTEST-EMBEDDED (Execução via framework Pytest do ESP-IDF):
   $ pytest pytest_headSet4.py --target esp32
   - Executa testes determinísticos de regressão usando a fixture oficial 'dut: Dut'.
   - Valida a sequência de boot, subsistemas e critérios do Quality Gate de AGENTS.md.
================================================================================================
"""

import os
import sys
import time
import re
import argparse
import threading
from typing import Optional, Dict, Any, List

# ==============================================================================================
# IMPORTS CONDICIONAIS DO PYTEST / PYTEST-EMBEDDED
# ==============================================================================================
# O ambiente pode ser executado como um script de bancada independente (usando apenas pyserial)
# ou dentro da suíte formal de CI/CD do ESP-IDF com pytest-embedded.
# ==============================================================================================
try:
    import pytest
    from pytest_embedded import Dut
    from pytest_embedded_idf.utils import idf_parametrize
    HAS_PYTEST_EMBEDDED = True
except ImportError:
    HAS_PYTEST_EMBEDDED = False

    class _MockPytestMark:
        def __getattr__(self, name: str):
            def decorator(func):
                return func
            return decorator

    class _MockPytest:
        mark = _MockPytestMark()

    pytest = _MockPytest()  # type: ignore

    # Cria stubs defensivos caso alguém importe ou execute sem o pacote instalado
    class Dut:  # type: ignore
        pass

    def idf_parametrize(*args, **kwargs):  # type: ignore
        def decorator(f):
            return f
        return decorator

try:
    import serial
    import serial.tools.list_ports
    HAS_PYSERIAL = True
except ImportError:
    HAS_PYSERIAL = False


# ==============================================================================================
# CÓDIGOS DE ESCAPE ANSI PARA O CONSOLE VISUAL (TERMINAL / POWERSHELL)
# ==============================================================================================
class TermColors:
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    DIM     = "\033[2m"
    RED     = "\033[91m"
    GREEN   = "\033[92m"
    YELLOW  = "\033[93m"
    BLUE    = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN    = "\033[96m"
    WHITE   = "\033[97m"
    BG_BLUE = "\033[44m"
    BG_RED  = "\033[41m"
    BG_DARK = "\033[40m"


# ==============================================================================================
# PADRÕES REGEX DE TELEMETRIA E SUBSISTEMAS DO HEADSET4 (ESP-IDF v6.x)
# ==============================================================================================
# Mapeados diretamente a partir dos logs reais emitidos pelos módulos do firmware:
# - main/main.c
# - main/src/audio/audio.c e wm8960.c
# - main/src/bt/bt_core.c e bt_avrcp.c
# - main/src/sensor/apds9930.c e battery.c
# - main/src/display/display.c e ui_model.c
# ==============================================================================================
REGEX_PATTERNS = {
    # Inicialização do Chip ESP32 e revisão de silício
    "chip_rev": re.compile(r"Chip ESP32 rev v(?P<rev>\d+\.\d+)"),

    # Memória não-volátil NVS Flash
    "nvs": re.compile(r"Partição NVS (?P<status>inicializada|recuperada).*"),

    # Codec de Áudio Wolfson WM8960 e Actor Audio
    "audio_codec": re.compile(
        r"(WM8960 configurado|Actor Audio criado com sucesso|audio_codec: codec ligado)"
    ),

    # Pilha Bluetooth Classic + BLE Dual Mode
    "bt_stack": re.compile(r"Pilha BTDM \(Classic \+ BLE\) ativa"),

    # Perfis Bluetooth iniciados na BtAppTask
    "bt_profiles": re.compile(r"bt_core:.*(AVRCP|A2DP|HFP|GAP|BLE|Fast Pair|Link manager)"),

    # Sensor Óptico de Proximidade e Uso na Cabeça (APDS-9930 via I2C)
    "apds9930": re.compile(r"APDS-9930 ativo \(ID 0x(?P<id>[0-9A-Fa-f]+)"),

    # Atuador Háptico Vibracall (GPIO 15)
    "actuators": re.compile(r"(modulo de atuadores/vibracall|actuators|Vibracall)"),

    # Hardware e leitura analógica de bateria (ADC1 + MOSFET de chaveamento)
    "battery_init": re.compile(r"Subsistema de bateria inicializado com sucesso"),
    "battery_reading": re.compile(
        r"\[BATERIA\] Mediana: raw=(?P<raw>\d+) \| Pino: (?P<pin_mv>\d+) mV \| Tensão real: (?P<volts>[\d\.]+) V"
    ),
    "battery_pct": re.compile(
        r"\[BATERIA\] Publicando evento SENSOR_EVT_BATTERY.*: (?P<pct>\d+)% \((?P<mv>\d+) mV\)"
    ),

    # Display LCD TFT ILI9341 (SPI a 40 MHz) e Toque Resistivo XPT2046
    "display_hw": re.compile(r"Hardware do display \(ILI9341 a 40 MHz\) e Touch .* prontos!"),

    # Interface Reativa LVGL 9 e Task Dedicada FreeRTOS
    "lvgl_task": re.compile(r"Tarefa FreeRTOS do LVGL iniciada com sucesso"),
    "ui_model": re.compile(r"UI Model inicializado com sucesso"),

    # Telemetria periódica de Heap Interno e PSRAM de 8 MB (log_memory)
    "memory_diag": re.compile(
        r"\[(?P<tag>boot|\d+s)\] heap interno livre=(?P<heap>\d+) \(min=(?P<min>\d+), maior bloco=(?P<max>\d+)\) \| PSRAM livre=(?P<psram>\d+)"
    ),

    # Detecção de Erros Críticos, Falhas de Asserção e Pânicos do FreeRTOS
    "critical_panic": re.compile(
        r"(Guru Meditation Error|abort\(\) was called|CORRUPT HEAP|Backtrace:|assert failed:)"
    ),
}


# ==============================================================================================
# MODELO DE ESTADO E TELEMETRIA DO HEADSET4
# ==============================================================================================
class HeadSet4TelemetryState:
    """
    Armazena o estado consolidado da placa capturado a partir do fluxo de dados serial.
    Garante sincronia thread-safe entre a thread leitora da UART e a interface com o usuário.
    """

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.chip_revision: Optional[str] = None
        self.nvs_ok: bool = False
        self.audio_codec_ok: bool = False
        self.bt_stack_ok: bool = False
        self.apds9930_ok: bool = False
        self.apds9930_id: Optional[str] = None
        self.actuators_ok: bool = False
        self.battery_ok: bool = False
        self.battery_volts: Optional[float] = None
        self.battery_pct: Optional[int] = None
        self.display_hw_ok: bool = False
        self.lvgl_ok: bool = False
        self.ui_model_ok: bool = False

        # Métricas de Memória (em bytes)
        self.heap_free: Optional[int] = None
        self.heap_min: Optional[int] = None
        self.psram_free: Optional[int] = None

        # Histórico de erros e alertas
        self.critical_errors: List[str] = []
        self.raw_logs_history: List[str] = []
        self.start_time = time.time()

    def update_from_line(self, line: str) -> None:
        """Processa uma única linha de log e atualiza as variáveis de telemetria."""
        clean_line = line.strip()
        if not clean_line:
            return

        with self.lock:
            self.raw_logs_history.append(clean_line)
            if len(self.raw_logs_history) > 1000:
                self.raw_logs_history.pop(0)

            # 1. Chip ESP32
            m = REGEX_PATTERNS["chip_rev"].search(clean_line)
            if m:
                self.chip_revision = m.group("rev")

            # 2. NVS Flash
            if REGEX_PATTERNS["nvs"].search(clean_line):
                self.nvs_ok = True

            # 3. Codec de Áudio WM8960
            if REGEX_PATTERNS["audio_codec"].search(clean_line):
                self.audio_codec_ok = True

            # 4. Pilha Bluetooth
            if REGEX_PATTERNS["bt_stack"].search(clean_line):
                self.bt_stack_ok = True

            # 5. Sensor APDS-9930
            m = REGEX_PATTERNS["apds9930"].search(clean_line)
            if m:
                self.apds9930_ok = True
                self.apds9930_id = m.group("id")

            # 6. Atuadores Hápticos
            if REGEX_PATTERNS["actuators"].search(clean_line):
                self.actuators_ok = True

            # 7. Bateria
            if REGEX_PATTERNS["battery_init"].search(clean_line):
                self.battery_ok = True

            m = REGEX_PATTERNS["battery_reading"].search(clean_line)
            if m:
                try:
                    self.battery_volts = float(m.group("volts"))
                except ValueError:
                    pass

            m = REGEX_PATTERNS["battery_pct"].search(clean_line)
            if m:
                try:
                    self.battery_pct = int(m.group("pct"))
                except ValueError:
                    pass

            # 8. Display & LVGL 9
            if REGEX_PATTERNS["display_hw"].search(clean_line):
                self.display_hw_ok = True

            if REGEX_PATTERNS["lvgl_task"].search(clean_line):
                self.lvgl_ok = True

            if REGEX_PATTERNS["ui_model"].search(clean_line):
                self.ui_model_ok = True

            # 9. Memória e PSRAM
            m = REGEX_PATTERNS["memory_diag"].search(clean_line)
            if m:
                try:
                    self.heap_free = int(m.group("heap"))
                    self.heap_min = int(m.group("min"))
                    self.psram_free = int(m.group("psram"))
                except ValueError:
                    pass

            # 10. Erros Críticos / Pânico
            m = REGEX_PATTERNS["critical_panic"].search(clean_line)
            if m:
                self.critical_errors.append(clean_line)

    def is_boot_complete(self) -> bool:
        """Determina se todos os subsistemas essenciais completaram o boot."""
        with self.lock:
            return bool(
                self.nvs_ok
                and self.audio_codec_ok
                and self.bt_stack_ok
                and self.apds9930_ok
                and self.battery_ok
                and self.display_hw_ok
                and self.lvgl_ok
                and len(self.critical_errors) == 0
            )

    def get_summary_dict(self) -> Dict[str, Any]:
        """Retorna uma visão estruturada do estado atual da placa."""
        with self.lock:
            return {
                "chip_revision": self.chip_revision or "Desconhecido",
                "nvs_ok": self.nvs_ok,
                "audio_codec_ok": self.audio_codec_ok,
                "bt_stack_ok": self.bt_stack_ok,
                "apds9930_ok": self.apds9930_ok,
                "apds9930_id": self.apds9930_id,
                "actuators_ok": self.actuators_ok,
                "battery_ok": self.battery_ok,
                "battery_volts": self.battery_volts,
                "battery_pct": self.battery_pct,
                "display_hw_ok": self.display_hw_ok,
                "lvgl_ok": self.lvgl_ok,
                "heap_free_kb": (self.heap_free / 1024) if self.heap_free else None,
                "psram_free_mb": (self.psram_free / (1024 * 1024)) if self.psram_free else None,
                "critical_errors_count": len(self.critical_errors),
                "boot_complete": self.is_boot_complete(),
            }


# ==============================================================================================
# CONSOLE INTERATIVO & CONTROLADOR SERIAL DE BANCADA
# ==============================================================================================
class HeadSet4ConsoleValidator:
    """
    Controlador do Console Interativo para validação HIL direta via porta serial.
    Fornece terminal bidirecional, análise contínua de logs e relatórios visuais.
    """

    def __init__(self, port: str, baud: int = 115200) -> None:
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.state = HeadSet4TelemetryState()
        self.is_running = False
        self.reader_thread: Optional[threading.Thread] = None
        self.show_raw_logs = True

    def open(self) -> bool:
        """Abre a conexão com a porta serial configurada."""
        if not HAS_PYSERIAL:
            print(f"{TermColors.RED}[ERRO] Pacote 'pyserial' não encontrado! Instale com: pip install pyserial{TermColors.RESET}")
            return False

        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
            self.is_running = True
            self.reader_thread = threading.Thread(target=self._read_serial_loop, daemon=True)
            self.reader_thread.start()
            return True
        except Exception as e:
            print(f"{TermColors.RED}[ERRO] Falha ao abrir porta serial {self.port}: {e}{TermColors.RESET}")
            return False

    def close(self) -> None:
        """Encerra a thread de leitura e fecha a porta serial."""
        self.is_running = False
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass

    def send_line(self, line: str) -> None:
        """Envia uma linha de texto terminada com retorno de carro para o ESP32."""
        if self.ser and self.ser.is_open:
            try:
                payload = (line + "\r\n").encode("utf-8", errors="replace")
                self.ser.write(payload)
                self.ser.flush()
            except Exception as e:
                print(f"{TermColors.RED}[ERRO] Falha ao enviar pela serial: {e}{TermColors.RESET}")

    def reset_hardware(self) -> None:
        """
        Gera uma sequência de pulsos elétricos nos pinos DTR e RTS
        do conversor USB-Serial para reiniciar o SoC ESP32 e capturar o boot frio.
        """
        if not self.ser or not self.ser.is_open:
            return

        print(f"{TermColors.CYAN}[HIL] Enviando pulso de Reset de Hardware para o ESP32 (DTR/RTS)...{TermColors.RESET}")
        try:
            self.ser.setDTR(False)
            self.ser.setRTS(True)
            time.sleep(0.1)
            self.ser.setRTS(False)
            time.sleep(0.05)
            # Limpa o estado atual para avaliar nova inicialização
            self.state = HeadSet4TelemetryState()
            print(f"{TermColors.GREEN}[HIL] SoC reiniciado! Aguardando sequência de boot...{TermColors.RESET}")
        except Exception as e:
            print(f"{TermColors.YELLOW}[AVISO] Não foi possível alternar DTR/RTS: {e}{TermColors.RESET}")

    def _read_serial_loop(self) -> None:
        """Loop contínuo em thread secundária para captura e decodificação dos logs."""
        buffer = bytearray()
        while self.is_running and self.ser and self.ser.is_open:
            try:
                chunk = self.ser.read(256)
                if chunk:
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        line_bytes, buffer = buffer.split(b"\n", 1)
                        line_str = line_bytes.decode("utf-8", errors="replace").rstrip("\r")
                        self.state.update_from_line(line_str)
                        if self.show_raw_logs:
                            self._print_formatted_log(line_str)
                else:
                    time.sleep(0.01)
            except Exception:
                break

    def _print_formatted_log(self, line: str) -> None:
        """Imprime uma linha de log com realce de cores conforme sua severidade ESP-IDF."""
        if line.startswith("E (") or " [E] " in line or "E:" in line:
            print(f"{TermColors.RED}{line}{TermColors.RESET}")
        elif line.startswith("W (") or " [W] " in line or "W:" in line:
            print(f"{TermColors.YELLOW}{line}{TermColors.RESET}")
        elif line.startswith("I (") or " [I] " in line or "I:" in line:
            if "act_audio" in line or "wm8960" in line:
                print(f"{TermColors.CYAN}{line}{TermColors.RESET}")
            elif "bt_" in line or "bt_link" in line:
                print(f"{TermColors.BLUE}{line}{TermColors.RESET}")
            elif "display" in line or "lvgl" in line:
                print(f"{TermColors.MAGENTA}{line}{TermColors.RESET}")
            elif "[BATERIA]" in line or "apds" in line:
                print(f"{TermColors.GREEN}{line}{TermColors.RESET}")
            else:
                print(f"{TermColors.WHITE}{line}{TermColors.RESET}")
        else:
            print(f"{TermColors.DIM}{line}{TermColors.RESET}")

    def print_dashboard(self) -> None:
        """Exibe o painel visual de status dos subsistemas no console."""
        s = self.state.get_summary_dict()

        def tag(val: bool) -> str:
            return f"{TermColors.GREEN}[✓ OK]{TermColors.RESET}" if val else f"{TermColors.RED}[✗ FALHA]{TermColors.RESET}"

        print("\n" + "=" * 78)
        print(f"{TermColors.BOLD}{TermColors.CYAN}       HEADSET4 — PAINEL DE TELEMETRIA E DIAGNÓSTICO DE SUBSISTEMAS{TermColors.RESET}")
        print("=" * 78)
        print(f"  • SoC ESP32 Silício   : {TermColors.BOLD}v{s['chip_revision']}{TermColors.RESET}")
        print(f"  • Partição NVS Flash  : {tag(s['nvs_ok'])} (Configurações e emparelhamentos)")
        print(f"  • Codec Áudio WM8960  : {tag(s['audio_codec_ok'])} (I2S DMA 44.1 kHz + DSP)")
        print(f"  • Pilha Bluetooth     : {tag(s['bt_stack_ok'])} (Dual-Mode: A2DP/HFP + BLE FastPair)")
        print(f"  • Sensor APDS-9930    : {tag(s['apds9930_ok'])} (ID: 0x{s['apds9930_id'] or '??'} · Detecção na Cabeça)")
        print(f"  • Atuador Vibracall   : {tag(s['actuators_ok'])} (Feedback Háptico GPIO 15)")

        bat_str = f"{s['battery_volts']:.2f} V" if s['battery_volts'] else "-- V"
        if s['battery_pct'] is not None:
            bat_str += f" ({s['battery_pct']}%)"
        print(f"  • Monitor de Bateria  : {tag(s['battery_ok'])} [Leitura: {bat_str}]")

        print(f"  • Display LCD ILI9341 : {tag(s['display_hw_ok'])} (SPI3 VSPI a 40 MHz)")
        print(f"  • Interface LVGL 9    : {tag(s['lvgl_ok'])} (Task FreeRTOS renderizando a 30+ FPS)")

        print("-" * 78)
        heap_str = f"{s['heap_free_kb']:.1f} KB livres" if s['heap_free_kb'] else "Aguardando diagnóstico..."
        psram_str = f"{s['psram_free_mb']:.2f} MB livres" if s['psram_free_mb'] else "Aguardando diagnóstico..."
        print(f"  • Heap Interno (SRAM) : {TermColors.BOLD}{heap_str}{TermColors.RESET}")
        print(f"  • PSRAM Externa (8MB) : {TermColors.BOLD}{psram_str}{TermColors.RESET}")

        if s['critical_errors_count'] > 0:
            print(f"  • {TermColors.RED}{TermColors.BOLD}PÂNICOS DETECTADOS  : {s['critical_errors_count']} erro(s) crítico(s)!{TermColors.RESET}")
        else:
            print(f"  • Erros Críticos / Pânico : {TermColors.GREEN}[Nenhum detectado]{TermColors.RESET}")
        print("=" * 78 + "\n")

    def run_interactive_console(self) -> None:
        """
        Inicia a sessão interativa de console permitindo conversação bidirecional
        com o ESP32 e comandos de controle do usuário.
        """
        print(f"{TermColors.BOLD}{TermColors.CYAN}")
        print("╔══════════════════════════════════════════════════════════════════════════════╗")
        print("║           HEADSET4 — CONSOLE INTERATIVO & VALIDADOR DE BANCADA (HIL)        ║")
        print("║                 ESP32-WROVER-E · ESP-IDF v6.x · LVGL 9 · WM8960              ║")
        print("╚══════════════════════════════════════════════════════════════════════════════╝")
        print(f"{TermColors.RESET}")
        print(f"{TermColors.WHITE}Comandos locais disponíveis no console:{TermColors.RESET}")
        print(f"  {TermColors.YELLOW}/help{TermColors.RESET}    : Exibe esta lista de comandos")
        print(f"  {TermColors.YELLOW}/status{TermColors.RESET}  : Exibe o Dashboard de status consolidado dos subsistemas")
        print(f"  {TermColors.YELLOW}/reset{TermColors.RESET}   : Dispara pulso de reset de hardware (DTR/RTS) no ESP32")
        print(f"  {TermColors.YELLOW}/test{TermColors.RESET}    : Executa auditoria automática de boot por 20 segundos")
        print(f"  {TermColors.YELLOW}/raw{TermColors.RESET}     : Alterna exibição contínua dos logs seriais na tela")
        print(f"  {TermColors.YELLOW}/clear{TermColors.RESET}   : Limpa a tela do terminal")
        print(f"  {TermColors.YELLOW}/exit{TermColors.RESET}    : Encerra a conexão serial e sai do validador")
        print(f"{TermColors.DIM}Qualquer outro texto digitado será transmitido diretamente para o ESP32.{TermColors.RESET}\n")

        try:
            while self.is_running:
                user_input = input(f"{TermColors.BOLD}{TermColors.CYAN}headset4> {TermColors.RESET}")
                cmd = user_input.strip()

                if not cmd:
                    continue
                elif cmd in ("/exit", "/quit", "exit", "quit"):
                    print(f"{TermColors.YELLOW}Encerrando console interativo...{TermColors.RESET}")
                    break
                elif cmd in ("/help", "help"):
                    print(f"{TermColors.WHITE}Comandos: /status, /reset, /test, /raw, /clear, /exit (ou digite para enviar à serial){TermColors.RESET}")
                elif cmd in ("/status", "status"):
                    self.print_dashboard()
                elif cmd in ("/reset", "reset"):
                    self.reset_hardware()
                elif cmd in ("/raw", "raw"):
                    self.show_raw_logs = not self.show_raw_logs
                    status_text = "ATIVADA" if self.show_raw_logs else "PAUSADA"
                    print(f"{TermColors.CYAN}[INFO] Exibição de logs seriais {status_text}.{TermColors.RESET}")
                elif cmd in ("/clear", "clear", "cls"):
                    os.system("cls" if os.name == "nt" else "clear")
                elif cmd in ("/test", "test"):
                    self.run_automated_audit(timeout_sec=20)
                else:
                    # Envia a linha para o ESP32 via UART
                    self.send_line(cmd)
                    print(f"{TermColors.DIM}>> Enviado: {cmd}{TermColors.RESET}")
        except KeyboardInterrupt:
            print(f"\n{TermColors.YELLOW}Sessão finalizada pelo usuário (Ctrl+C).{TermColors.RESET}")
        finally:
            self.close()

    def run_automated_audit(self, timeout_sec: int = 25) -> bool:
        """
        Executa um ciclo completo de auditoria determinística:
        Reinicia a placa, monitora os logs até o timeout e valida todos os critérios do Quality Gate.
        """
        print(f"\n{TermColors.CYAN}==========================================================================")
        print(f" INICIANDO AUDITORIA DETERMINÍSTICA DE HARDWARE (Timeout: {timeout_sec}s)")
        print(f"=========================================================================={TermColors.RESET}")
        self.reset_hardware()

        start = time.time()
        last_check = 0
        while time.time() - start < timeout_sec:
            time.sleep(0.5)
            # Mostra progresso a cada 3 segundos
            elapsed = int(time.time() - start)
            if elapsed // 3 != last_check:
                last_check = elapsed // 3
                print(f"{TermColors.DIM}[Auditoria] Aguardando logs... {elapsed}s / {timeout_sec}s decorridos{TermColors.RESET}")

            # Se todos os subsistemas derem OK e não houver erros, podemos aprovar
            if self.state.is_boot_complete():
                print(f"{TermColors.GREEN}[✓] Todos os subsistemas essenciais inicializados antes do timeout!{TermColors.RESET}")
                break

        self.print_dashboard()
        s = self.state.get_summary_dict()

        # Avaliação de Critérios Invioláveis de AGENTS.md
        failures = []
        if not s["nvs_ok"]:
            failures.append("Partição NVS Flash não inicializou")
        if not s["audio_codec_ok"]:
            failures.append("Codec Wolfson WM8960 / Actor Audio não confirmou inicialização")
        if not s["bt_stack_ok"]:
            failures.append("Pilha Bluetooth BTDM não iniciou")
        if not s["apds9930_ok"]:
            failures.append("Sensor óptico APDS-9930 não respondeu no barramento I2C")
        if not s["display_hw_ok"]:
            failures.append("Hardware do display ILI9341 / XPT2046 não completou setup")
        if not s["lvgl_ok"]:
            failures.append("Task principal da interface LVGL 9 (lvgl_task) não foi criada")
        if s["critical_errors_count"] > 0:
            failures.append(f"{s['critical_errors_count']} erro(s) crítico(s) de pânico detectados")

        print("--------------------------------------------------------------------------")
        if len(failures) == 0:
            print(f"{TermColors.GREEN}{TermColors.BOLD}✔ VEREDICTO FINAL: HOMOLOGADO COM SUCESSO (100% dos Subsistemas OK){TermColors.RESET}")
            print("--------------------------------------------------------------------------\n")
            return True
        else:
            print(f"{TermColors.RED}{TermColors.BOLD}✘ VEREDICTO FINAL: REPROVADO — Pendências encontradas:{TermColors.RESET}")
            for f in failures:
                print(f"  {TermColors.RED}• {f}{TermColors.RESET}")
            print("--------------------------------------------------------------------------\n")
            return False


# ==============================================================================================
# FUNÇÃO AUXILIAR DE DETECÇÃO AUTOMÁTICA DE PORTA SERIAL DO ESP32
# ==============================================================================================
def detect_esp32_serial_port() -> Optional[str]:
    """
    Busca automaticamente por portas seriais associadas a chips conversores comumente
    usados em placas de desenvolvimento ESP32 (CP210x, CH340, FTDI, Espressif USB JTAG/Serial).
    """
    if not HAS_PYSERIAL:
        return None

    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    # Tenta casar com padrões conhecidos de conversores USB-Serial
    target_keywords = ["CP210", "CH340", "FTDI", "UART", "ESPRESSIF", "USB SERIAL", "USB-SERIAL"]
    for p in ports:
        desc = (p.description or "").upper()
        hwid = (p.hwid or "").upper()
        for kw in target_keywords:
            if kw in desc or kw in hwid:
                return p.device

    # Se houver apenas uma porta COM no computador, utiliza ela como padrão
    if len(ports) == 1:
        return ports[0].device

    return None


# ==============================================================================================
# SUÍTE DE TESTES AUTOMATIZADA COM PYTEST-EMBEDDED (INTEGRAÇÃO ESP-IDF CI/CD)
# ==============================================================================================
# As funções abaixo são descobertas e executadas automaticamente pelo comando:
#   pytest pytest_headSet4.py --target esp32
# Elas usam a fixture oficial 'dut: Dut' do ecossistema pytest-embedded.
# ==============================================================================================

@pytest.mark.generic
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_headset4_boot_sequence(dut: Dut) -> None:
    """
    Testa a sequência de inicialização ordenada do HeadSet4 conforme AGENTS.md §0.3 e §4.2:
    1. Revisão do chip ESP32
    2. Inicialização da Partição NVS Flash
    3. Codec de Áudio Wolfson WM8960 em I2S DMA
    4. Ativação da pilha Bluetooth Classic + BLE Dual Mode
    5. Sensor óptico de proximidade APDS-9930 (I2C)
    6. Subsistema de monitoramento de bateria (ADC1)
    7. Inicialização do display LCD ILI9341 e task do LVGL 9
    """
    # 1. Valida detecção do SoC ESP32
    dut.expect(r"Chip ESP32 rev v\d+\.\d+", timeout=15)

    # 2. Valida recuperação e montagem da partição NVS
    dut.expect(r"Partição NVS (inicializada|recuperada)", timeout=10)

    # 3. Valida configuração do Codec de áudio e Actor Audio
    dut.expect(
        r"(WM8960 configurado.*aguardando audio em standby|Actor Audio criado com sucesso)",
        timeout=15,
    )

    # 4. Valida inicialização do Bluetooth Dual Mode (BTDM)
    dut.expect(r"Pilha BTDM \(Classic \+ BLE\) ativa", timeout=15)

    # 5. Valida detecção do sensor óptico APDS-9930 via I2C
    dut.expect(r"APDS-9930 ativo \(ID 0x[0-9A-Fa-f]+", timeout=10)

    # 6. Valida o subsistema de bateria
    dut.expect(r"Subsistema de bateria inicializado com sucesso", timeout=10)

    # 7. Valida hardware de tela e criação da tarefa de renderização gráfica
    dut.expect(r"Hardware do display \(ILI9341 a 40 MHz\) e Touch .* prontos!", timeout=15)
    dut.expect(r"Tarefa FreeRTOS do LVGL iniciada com sucesso", timeout=10)


@pytest.mark.generic
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_headset4_memory_and_psram(dut: Dut) -> None:
    """
    Testa a saúde da memória e o mapeamento da PSRAM externa de 8 MB do ESP32-WROVER-E.
    Garante que o heap interno não esteja degradado e que a PSRAM foi ativada.
    """
    # Aguarda o log periódico de diagnóstico de memória (emitido no boot e a cada 30s)
    match = dut.expect(
        r"\[(boot|\d+s)\] heap interno livre=(\d+) \(min=(\d+), maior bloco=(\d+)\) \| PSRAM livre=(\d+)",
        timeout=35,
    )
    # Extrai os valores numéricos capturados pelo regex
    heap_free = int(match.group(2))
    psram_free = int(match.group(5))

    # O heap interno deve ter pelo menos 40 KB livres no boot
    assert heap_free >= 40000, f"Heap interno livre insuficiente: {heap_free} bytes (mínimo 40 KB)"

    # A PSRAM externa deve estar presente com pelo menos 1 MB livre dos 8 MB do WROVER-E
    assert psram_free >= 1000000, f"PSRAM externa não detectada ou insuficiente: {psram_free} bytes"


@pytest.mark.generic
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_headset4_subsystems_stability(dut: Dut) -> None:
    """
    Monitora a placa por 15 segundos garantindo ausência de erros críticos ou aborts do FreeRTOS.
    """
    # Verifica ausência de pânico durante a operação contínua
    dut.expect_none(
        r"(Guru Meditation Error|abort\(\) was called|CORRUPT HEAP|Backtrace:|assert failed:)",
        timeout=15,
    )


# ==============================================================================================
# PONTO DE ENTRADA PRINCIPAL PARA EXECUÇÃO EM LINHA DE COMANDO
# ==============================================================================================
def main() -> None:
    parser = argparse.ArgumentParser(
        description="HeadSet4 — Validador de Bancada HIL & Console Serial Interativo (ESP32-WROVER-E)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemplos de Uso:
  1. Conectar interativamente no console com auto-detecção da porta:
     python pytest_headSet4.py

  2. Conectar especificando a porta COM e baud rate:
     python pytest_headSet4.py --port COM3 --baud 115200

  3. Executar auditoria automática de boot (ideal para scripts de teste):
     python pytest_headSet4.py --port COM3 --auto-test

  4. Listar todas as portas seriais conectadas:
     python pytest_headSet4.py --list-ports

  5. Executar via pytest (quando pacotes pytest-embedded estiverem instalados):
     pytest pytest_headSet4.py --target esp32
        """,
    )

    parser.add_argument("-p", "--port", type=str, default=None, help="Porta serial (ex: COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Velocidade em baud rate (padrão: 115200)")
    parser.add_argument("-a", "--auto-test", action="store_true", help="Executa auditoria determinística e encerra com código de saída")
    parser.add_argument("-t", "--timeout", type=int, default=25, help="Tempo limite em segundos para auditoria de boot (padrão: 25)")
    parser.add_argument("-l", "--list-ports", action="store_true", help="Lista as portas seriais disponíveis no sistema e sai")

    args = parser.parse_args()

    # 1. Opção para listar portas disponíveis
    if args.list_ports:
        if not HAS_PYSERIAL:
            print(f"{TermColors.RED}Instale 'pyserial' para escanear portas: pip install pyserial{TermColors.RESET}")
            sys.exit(1)
        ports = list(serial.tools.list_ports.comports())
        print(f"\n{TermColors.BOLD}Portas Seriais Detectadas no Computador:{TermColors.RESET}")
        if not ports:
            print("  Nenhuma porta serial encontrada.")
        else:
            for p in ports:
                print(f"  • {TermColors.CYAN}{p.device}{TermColors.RESET} - {p.description} [{p.hwid}]")
        print()
        sys.exit(0)

    # 2. Resolução da porta serial
    selected_port = args.port
    if not selected_port:
        selected_port = detect_esp32_serial_port()

    if not selected_port:
        print(f"\n{TermColors.YELLOW}[AVISO] Nenhuma porta serial do ESP32 foi detectada automaticamente.{TermColors.RESET}")
        print("Por favor, conecte a placa via cabo USB ou forneça a porta explicitamente:")
        print(f"  {TermColors.CYAN}python pytest_headSet4.py --port COMx{TermColors.RESET}\n")
        sys.exit(1)

    print(f"{TermColors.GREEN}[HIL] Conectando à porta serial: {selected_port} ({args.baud} bps)...{TermColors.RESET}")
    validator = HeadSet4ConsoleValidator(port=selected_port, baud=args.baud)

    if not validator.open():
        sys.exit(1)

    # 3. Execução do modo solicitado
    if args.auto_test:
        success = validator.run_automated_audit(timeout_sec=args.timeout)
        validator.close()
        sys.exit(0 if success else 1)
    else:
        validator.run_interactive_console()


if __name__ == "__main__":
    main()
