# ==============================================================================
# Script de Automação: Build, Instalação e Execução Direta no Smartphone via ADB
# Projeto: HeadSet4 Android Companion (SuperHeadSetAPP)
# ==============================================================================

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  HeadSet4: Compilação e Deploy no Telefone via USB (ADB)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Configurar JDK 17 (compatível com Android Gradle Plugin 9.x)
if (-not (Test-Path "$env:JAVA_HOME\bin\javac.exe")) {
    $jdkPath = "C:\Users\Bruno\.jdks\jbr-17.0.12"
    if (Test-Path "$jdkPath\bin\javac.exe") {
        $env:JAVA_HOME = $jdkPath
        Write-Host "[OK] JAVA_HOME configurado para: $env:JAVA_HOME" -ForegroundColor Green
    } else {
        Write-Host "[AVISO] JDK 17 não encontrado em $jdkPath. Verifique seu ambiente." -ForegroundColor Yellow
    }
}

# 2. Localizar executável do ADB
$adbPath = "C:\Users\Bruno\AppData\Local\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adbPath)) {
    $adbCmd = Get-Command adb -ErrorAction SilentlyContinue
    if ($adbCmd) {
        $adbPath = $adbCmd.Source
    } else {
        Write-Host "[ERRO] Executável adb.exe não encontrado!" -ForegroundColor Red
        exit 1
    }
}
Write-Host "[OK] ADB localizado em: $adbPath" -ForegroundColor Green

# 3. Verificar status do dispositivo conectado
Write-Host ""
Write-Host "[1/4] Verificando smartphone conectado via USB..." -ForegroundColor Magenta
$devices = & $adbPath devices
Write-Host $devices

$hasDevice = ($devices | Where-Object { $_ -match "\bdevice\b" -and $_ -notmatch "attached" }).Count -gt 0
if (-not $hasDevice) {
    Write-Host "[ERRO] Nenhum smartphone autorizado detectado via ADB!" -ForegroundColor Red
    Write-Host "Certifique-se de que:" -ForegroundColor Yellow
    Write-Host " 1. O cabo USB está conectado firmemente ao computador." -ForegroundColor Yellow
    Write-Host " 2. A 'Depuração USB' está ativa em Opções do Desenvolvedor." -ForegroundColor Yellow
    Write-Host " 3. Você aceitou o pop-up 'Permitir depuração USB deste computador' na tela do celular." -ForegroundColor Yellow
    exit 1
}

# 4. Compilar APK em modo Debug
Write-Host ""
Write-Host "[2/4] Compilando APK Debug via Gradle..." -ForegroundColor Magenta
$buildResult = & .\gradlew.bat assembleDebug

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERRO] Falha na compilação do APK pelo Gradle!" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "[OK] APK compilado com sucesso!" -ForegroundColor Green

# 5. Instalar o APK no smartphone
$apkPath = "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $apkPath)) {
    Write-Host "[ERRO] Arquivo APK não encontrado em $apkPath" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[3/4] Enviando e instalando APK no telefone via ADB..." -ForegroundColor Magenta
$installOut = & $adbPath install -r -d $apkPath 2>&1
Write-Host $installOut

if ($installOut -match "INSTALL_FAILED_UPDATE_INCOMPATIBLE|signatures do not match") {
    Write-Host "[AVISO] Assinatura incompatível com a versão anterior instalada." -ForegroundColor Yellow
    Write-Host "[INFO] Desinstalando versão antiga do app no smartphone..." -ForegroundColor Yellow
    & $adbPath uninstall "com.aistudio.headset4.kpxvzm"
    Write-Host "[INFO] Reinstalando APK com nova assinatura..." -ForegroundColor Yellow
    $installOut = & $adbPath install -r -d $apkPath 2>&1
    Write-Host $installOut
}

if ($LASTEXITCODE -ne 0 -and $installOut -notmatch "Success") {
    Write-Host "[ERRO] Falha ao instalar o APK no telefone!" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] APK instalado com sucesso no smartphone!" -ForegroundColor Green

# 6. Iniciar a Activity principal do aplicativo
$appPackage = "com.aistudio.headset4.kpxvzm"
$mainActivity = "com.example.MainActivity"
Write-Host ""
Write-Host "[4/4] Iniciando aplicativo na tela do smartphone..." -ForegroundColor Magenta
& $adbPath shell am start -n "$appPackage/$mainActivity"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  Sucesso! O SuperHeadSetAPP está em execução no celular." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Para acompanhar os logs de BLE e Telemetria em tempo real, execute:" -ForegroundColor Cyan
Write-Host "  & '$adbPath' logcat -s HeadSetBle:V System.out:I AndroidRuntime:E" -ForegroundColor Gray
