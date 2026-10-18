@echo off
chcp 65001 >nul
echo ==========================================================
echo   HeadSet4: Compilação e Deploy no Telefone via USB (ADB)
echo ==========================================================

REM 1. Configurar JDK 17
if not exist "%JAVA_HOME%\bin\javac.exe" (
    if exist "C:\Users\Bruno\.jdks\jbr-17.0.12\bin\javac.exe" (
        set "JAVA_HOME=C:\Users\Bruno\.jdks\jbr-17.0.12"
        echo [OK] JAVA_HOME configurado para JDK 17
    )
)

set "ADB=C:\Users\Bruno\AppData\Local\Android\Sdk\platform-tools\adb.exe"

REM 2. Verificar conexão do smartphone
echo.
echo [1/4] Verificando smartphone conectado via USB...
"%ADB%" devices

REM 3. Compilar APK
echo.
echo [2/4] Compilando APK Debug com Gradle...
call gradlew.bat assembleDebug
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilação do APK!
    pause
    exit /b %ERRORLEVEL%
)

REM 4. Instalar no celular
echo.
echo [3/4] Instalando APK no celular...
"%ADB%" install -r -d app\build\outputs\apk\debug\app-debug.apk
if %ERRORLEVEL% neq 0 (
    echo [AVISO] Tentando reinstalação limpa (removendo versão anterior)...
    "%ADB%" uninstall com.aistudio.headset4.kpxvzm
    "%ADB%" install -r -d app\build\outputs\apk\debug\app-debug.apk
    if %ERRORLEVEL% neq 0 (
        echo [ERRO] Falha persistente na instalação do APK via ADB!
        pause
        exit /b %ERRORLEVEL%
    )
)

REM 5. Executar app
echo.
echo [4/4] Iniciando aplicativo na tela do smartphone...
"%ADB%" shell am start -n com.aistudio.headset4.kpxvzm/com.example.MainActivity

echo.
echo ==========================================================
echo   App instalado e aberto com sucesso no celular!
echo ==========================================================
pause
