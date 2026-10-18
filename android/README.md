# Aplicativo Android Companion — SuperHeadSetAPP

O aplicativo oficial do **HeadSet4** reside no diretório [`android/SuperHeadSetAPP`](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/android/SuperHeadSetAPP). Ele é um projeto completo para Android Studio, construído em **Kotlin**, **Jetpack Compose**, **Material 3**, **Coroutines** e **StateFlow**, com estética Cyberpunk/Neo-techno e alta resiliência de comunicação BLE.

---

## 1. Estrutura do Projeto Android (`android/SuperHeadSetAPP`)

```text
android/SuperHeadSetAPP/
├── app/
│   ├── build.gradle.kts             # Dependências Jetpack Compose, Coroutines e Lifecycle
│   └── src/main/
│       ├── AndroidManifest.xml      # Permissões Bluetooth LE para Android 12+ (API 31–35)
│       └── java/com/example/
│           ├── MainActivity.kt      # Ponto de entrada, Scaffold, Navegação e Banners de Feedback
│           ├── ble/
│           │   ├── HeadSetBleManager.kt      # Motor BLE completo com Correlation ID e Handshake
│           │   ├── HeadSetGattAttributes.kt  # UUIDs canônicos (Controle, EQ, BAS, FastPair)
│           │   ├── HeadSetGattQueue.kt       # Fila sequencial GATT anti-erro 133
│           │   └── ImageConverter.kt         # Conversor de Bitmaps para RGB565 Little-Endian (240x296)
│           ├── model/
│           │   └── HeadSetModels.kt          # HeadSetUiState, MediaSlot, CommandFeedback, etc.
│           ├── ui/
│           │   ├── components/      # CyberTopBar, CyberBottomNav, GattConsoleDialog, etc.
│           │   ├── screens/         # DashboardScreen, EqualizerScreen, HardwareScreen, DisplayGalleryScreen
│           │   └── theme/           # Cores neon, tipografia e temas escuros
│           └── viewmodel/
│               └── HeadSetViewModel.kt       # Integração StateFlow e despacho de ações da UI
├── build.gradle.kts
└── settings.gradle.kts
```

---

## 2. Inovações e Protocolos Integrados

1. **Correlation ID Universal nos Comandos:**
   - Todo comando enviado pelo `HeadSetBleManager` injeta automaticamente um `id` numérico incremental (`"id": 101`).
   - O tempo de ida e volta (RTT/latência) é medido individualmente por comando e exibido em tempo real.
2. **Handshake e Push Proativo:**
   - Detecta o evento push `{"evt":"app.connected"}` emitido pelo ESP32 ao ligar as notificações CCCD.
   - Dispara o comando formal `{"cmd":"app_connect"}` para negociar compatibilidade e obter metadados de firmware e lista de features.
3. **Tratamento de Confirmações e Erros:**
   - Respostas `"status":"ok"` atualizam os controles e o histórico de pacotes.
   - Respostas `"status":"error"` extraem os campos padronizados `reason` e `detail` (ex: `out_of_range`, `invalid_image_size`), alertando o usuário no banner de feedback.
4. **Gerenciamento de Galerias e Upload:**
   - Suporte completo aos 6 slots de Wallpapers e 6 slots de GIFs.
   - Comandos de seleção (`set_wallpaper_idx`, `set_gif_idx`) e exclusão de slots (`delete_media_slot`).

---

## 3. Compilação e Deploy Direto no Smartphone via USB (ADB)

O projeto inclui automação completa para compilar o APK e instalá-lo diretamente no smartphone com depuração USB ativa:

### Pré-requisitos Verificados:
- Smartphone Android conectado via cabo USB com **Depuração USB** habilitada.
- Chave de autorização aceita na tela do aparelho (`adb devices` listando o aparelho como `device`).
- JDK 17 (configurado automaticamente para o Gradle 9.3.1 / AGP 9.1.1).

### Métodos de Execução:

1. **Via Script de Lote (1 Clique no Windows):**
   - Dê um duplo-clique em [`install_to_phone.bat`](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/android/SuperHeadSetAPP/install_to_phone.bat) no Windows Explorer ou execute no terminal.

2. **Via PowerShell:**
   ```powershell
   cd a:\BackupDesktopFileBruno\Estudos\ESP\ESP32\headSet4\android\SuperHeadSetAPP
   .\install_to_phone.ps1
   ```

3. **Comandos Manuais equivalentes:**
   ```powershell
   # 1. Definir JAVA_HOME para o JDK 17
   $env:JAVA_HOME = "C:\Users\Bruno\.jdks\jbr-17.0.12"

   # 2. Compilar APK
   .\gradlew.bat assembleDebug

   # 3. Instalar no celular
   & "C:\Users\Bruno\AppData\Local\Android\Sdk\platform-tools\adb.exe" install -r -d app\build\outputs\apk\debug\app-debug.apk

   # 4. Abrir app na tela do celular
   & "C:\Users\Bruno\AppData\Local\Android\Sdk\platform-tools\adb.exe" shell am start -n com.aistudio.headset4.kpxvzm/com.example.MainActivity

   # 5. Monitorar logs do aplicativo
   & "C:\Users\Bruno\AppData\Local\Android\Sdk\platform-tools\adb.exe" logcat -s HeadSetBle:V HeadSetUI:V
   ```

