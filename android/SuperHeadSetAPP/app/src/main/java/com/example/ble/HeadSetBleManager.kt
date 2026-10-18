package com.example.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Base64
import android.util.Log
import android.view.KeyEvent
import com.example.model.BleConnectionStatus
import com.example.model.CommandFeedback
import com.example.model.CommandStatus
import com.example.model.DiscoveredBluetoothDevice
import com.example.model.GattPacketLog
import com.example.model.HeadSetUiState
import com.example.model.MediaSlot
import com.example.model.MediaUploadProgress
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.nio.charset.StandardCharsets

@SuppressLint("MissingPermission")
class HeadSetBleManager(
    private val context: Context,
    private val scope: CoroutineScope
) {
    private val tag = "HeadSetBleManager"

    private val bluetoothAdapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private val audioManager: AudioManager? =
        context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager

    private var bluetoothGatt: BluetoothGatt? = null
    private var gattQueue: HeadSetGattQueue? = null

    private var charCmdRx: BluetoothGattCharacteristic? = null
    private var charNotifyTx: BluetoothGattCharacteristic? = null
    private var charEqPreset: BluetoothGattCharacteristic? = null
    private var charEqGains: BluetoothGattCharacteristic? = null
    private var charBatteryLevel: BluetoothGattCharacteristic? = null

    private var heartbeatJob: Job? = null
    private var activeUploadJob: Job? = null
    private var scanTimeoutJob: Job? = null
    private var simulationTelemetryJob: Job? = null
    private var clearFeedbackJob: Job? = null

    private var lastPingSendTimestamp = 0L
    private var lastUserActionTimestamp = 0L
    private val incomingBuffer = StringBuilder()

    // Controle de Correlation ID e rastreamento de confirmações de comandos
    private val nextCorrelationId = java.util.concurrent.atomic.AtomicInteger(100)

    data class PendingCommand(
        val id: Int,
        val cmd: String,
        val timestamp: Long = System.currentTimeMillis()
    )

    private val pendingCommands = java.util.concurrent.ConcurrentHashMap<Int, PendingCommand>()

    private var scanCallback: ScanCallback? = null
    private var isScanningActive = false

    private val initialImages = listOf(
        MediaSlot(
            id = 0,
            title = "Cyber Grid",
            subtitle = "RGB565 142 KB (Padrão)",
            type = "image",
            imgUrl = "https://lh3.googleusercontent.com/aida-public/AB6AXuD-hmkg_riUgW5lHIBzGww4ybc5J-y4DB0xqQVrg4w50FrzFbK6u9vDpAGAu-uhvxgtJoo_gbBFthGvftll-XFvbNucgajjm93zGVQn6HFcN3vwClDwGrhvIQDx7iJHyD74_vTy_4ndESTGZYtVWQsWcarriEilalujSv0dWsz7Ko5JWVK_6zowJRuDC6TyjB0H3MNICXtq9rBYD_LpdoUcF5GdRJnzo7iIFbanU1yVS9eGEx6508jV",
            isEmpty = false,
            sizeBytes = 142080,
            hasData = true,
            crc32 = "A1B2C3D4"
        ),
        MediaSlot(
            id = 1,
            title = "Obsidian Core",
            subtitle = "RGB565 142 KB (Flash)",
            type = "image",
            imgUrl = "https://lh3.googleusercontent.com/aida-public/AB6AXuC_z_g9SMn_Tae4VivkzghMJ45eHh--xg2GiiG9qK8QHCBg2ujqlhfi1F2OWYYoPf4EPjkXoz0YLDOjAaszn2BG6i3n939QkfsDaaC0BkUC3JbXMFjYlZ9n6eUIqnUWojpOIy6pMD_IcGwGyXQdKVhMdOl2QsjSRPJUDq22zicoPruK1aJ-hdUx_r3HQN5pee-HUZondVNX7YLmHRH3Oo4a1tw_ryTO8eMmfH2YrcZual0N3XF9BZFP",
            isEmpty = false,
            sizeBytes = 142080,
            hasData = true,
            crc32 = "B2C3D4E5"
        ),
        MediaSlot(
            id = 2,
            title = "Neon Matrix",
            subtitle = "RGB565 142 KB (Slot 2)",
            type = "matrix",
            isEmpty = false,
            sizeBytes = 142080,
            hasData = true,
            crc32 = "D4E5F6A7"
        ),
        MediaSlot(id = 3, title = "Foto 3", subtitle = "Disponível", isEmpty = true, sizeBytes = 0, hasData = false),
        MediaSlot(id = 4, title = "Foto 4", subtitle = "Disponível", isEmpty = true, sizeBytes = 0, hasData = false),
        MediaSlot(id = 5, title = "Foto 5", subtitle = "Disponível", isEmpty = true, sizeBytes = 0, hasData = false)
    )

    private val initialGifs = listOf(
        MediaSlot(id = 0, title = "Ondas Sonoras", subtitle = "142 KB Loop", type = "wave", isEmpty = false, sizeBytes = 142000, hasData = true),
        MediaSlot(id = 1, title = "Pulso Elétrico", subtitle = "185 KB Loop", type = "pulse", isEmpty = false, sizeBytes = 185000, hasData = true),
        MediaSlot(id = 2, title = "Matrix Scan", subtitle = "164 KB Loop", type = "matrix_anim", isEmpty = false, sizeBytes = 164000, hasData = true),
        MediaSlot(id = 3, title = "Radar Holográfico", subtitle = "128 KB Loop", type = "radar", isEmpty = false, sizeBytes = 128000, hasData = true),
        MediaSlot(id = 4, title = "Neon Skull", subtitle = "210 KB Loop", type = "skull", isEmpty = false, sizeBytes = 210000, hasData = true),
        MediaSlot(id = 5, title = "GIF 5", subtitle = "Disponível", isEmpty = true, sizeBytes = 0, hasData = false)
    )

    private val _uiState = MutableStateFlow(
        HeadSetUiState(
            imageSlots = initialImages,
            gifSlots = initialGifs
        )
    )
    val uiState = _uiState.asStateFlow()

    init {
        setupBondStateReceiver()
        populateBondedDevices()
        syncAudioState()
    }

    fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    /**
     * Registers a broadcast receiver to monitor SMP bond state changes.
     */
    private fun setupBondStateReceiver() {
        try {
            val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            context.registerReceiver(object : BroadcastReceiver() {
                override fun onReceive(context: Context?, intent: Intent?) {
                    if (intent?.action == BluetoothDevice.ACTION_BOND_STATE_CHANGED) {
                        val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                        }
                        val bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                        Log.i(tag, "Bond state changed for ${device?.name}: $bondState")
                        if (bondState == BluetoothDevice.BOND_BONDED) {
                            _uiState.update { it.copy(isBonded = true, lastGattMessage = "Vínculo criptográfico SMP estabelecido com sucesso!") }
                            // Resend initial snapshot if needed
                            sendJsonCommand("""{"cmd":"get_all_status"}""")
                        }
                    }
                }
            }, filter)
        } catch (e: Exception) {
            Log.w(tag, "Register bond receiver: ${e.message}")
        }
    }

    /**
     * Populates discovered device list with currently bonded devices.
     */
    fun populateBondedDevices() {
        try {
            val bonded = bluetoothAdapter?.bondedDevices ?: emptySet()
            val list = bonded.map { dev ->
                val isHs = dev.name?.contains("HeadSet", ignoreCase = true) == true
                DiscoveredBluetoothDevice(
                    name = dev.name ?: "Dispositivo Pareado",
                    address = dev.address,
                    rssi = -50,
                    isBonded = true,
                    isHeadSetCompatible = isHs,
                    isConnected = dev.address == _uiState.value.deviceAddress && _uiState.value.connectionStatus == BleConnectionStatus.CONNECTED
                )
            }
            if (list.isNotEmpty()) {
                _uiState.update { it.copy(discoveredDevices = list) }
            }
        } catch (e: Exception) {
            Log.w(tag, "populateBondedDevices error: ${e.message}")
        }
    }

    /**
     * Inspects active music stream volume from Android AudioManager.
     */
    fun syncAudioState() {
        try {
            val isMusicActive = audioManager?.isMusicActive == true
            val streamVol = audioManager?.getStreamVolume(AudioManager.STREAM_MUSIC) ?: 50
            val maxVol = audioManager?.getStreamMaxVolume(AudioManager.STREAM_MUSIC) ?: 100
            val volPct = if (maxVol > 0) ((streamVol.toFloat() / maxVol.toFloat()) * 100).toInt().coerceIn(0, 100) else 50

            _uiState.update { current ->
                current.copy(
                    isMusicActive = isMusicActive,
                    audioVolume = if (System.currentTimeMillis() - lastUserActionTimestamp > 400 && current.audioVolume == 0) volPct else current.audioVolume
                )
            }
        } catch (e: Exception) {
            Log.w(tag, "syncAudioState: ${e.message}")
        }
    }

    // ========================================================================
    // SCANNING & GATT CONNECTION ENGINE
    // ========================================================================

    /**
     * Starts BLE scan using BluetoothLeScanner with filters for HeadSet4.
     */
    fun startScanAndConnect(targetDeviceName: String = "HeadSet4") {
        if (!isBluetoothEnabled()) {
            _uiState.update {
                it.copy(
                    connectionStatus = BleConnectionStatus.DISCONNECTED,
                    lastGattMessage = "Bluetooth desligado. Ative o Bluetooth para conectar."
                )
            }
            return
        }

        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null) {
            _uiState.update {
                it.copy(
                    connectionStatus = BleConnectionStatus.DISCONNECTED,
                    lastGattMessage = "Scanner BLE indisponível neste dispositivo."
                )
            }
            return
        }

        // Cancel previous scan
        stopScan()

        populateBondedDevices()

        // Check if device is already bonded
        val bondedHs = bluetoothAdapter?.bondedDevices?.firstOrNull {
            it.name?.contains(targetDeviceName, ignoreCase = true) == true
        }

        if (bondedHs != null) {
            Log.i(tag, "HeadSet4 encontrado nos dispositivos pareados! Conectando diretamente: ${bondedHs.address}")
            connectToDevice(bondedHs)
            return
        }

        _uiState.update {
            it.copy(
                connectionStatus = BleConnectionStatus.SCANNING,
                isScanningDevices = true,
                lastGattMessage = "Buscando HeadSet4 via BLE..."
            )
        }

        isScanningActive = true

        val filters = listOf(
            ScanFilter.Builder().setDeviceName(targetDeviceName).build(),
            ScanFilter.Builder().setServiceUuid(ParcelUuid(HeadSetGattAttributes.UUID_SERVICE_CONTROL)).build(),
            ScanFilter.Builder().setServiceUuid(ParcelUuid(HeadSetGattAttributes.UUID_SERVICE_FASTPAIR)).build()
        )

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device ?: return
                val devName = device.name ?: result.scanRecord?.deviceName ?: "Dispositivo BLE"
                val devAddress = device.address ?: return
                val rssi = result.rssi

                Log.d(tag, "BLE Scan result: $devName ($devAddress) RSSI: $rssi")

                _uiState.update { state ->
                    val existing = state.discoveredDevices.find { it.address == devAddress }
                    val isHs = devName.contains("HeadSet", ignoreCase = true) ||
                            devName.contains("ESP32", ignoreCase = true) ||
                            result.scanRecord?.serviceUuids?.any {
                                it.uuid == HeadSetGattAttributes.UUID_SERVICE_CONTROL ||
                                        it.uuid == HeadSetGattAttributes.UUID_SERVICE_FASTPAIR
                            } == true

                    val updated = if (existing != null) {
                        state.discoveredDevices.map {
                            if (it.address == devAddress) it.copy(rssi = rssi, name = devName, isHeadSetCompatible = isHs) else it
                        }
                    } else {
                        state.discoveredDevices + DiscoveredBluetoothDevice(
                            name = devName,
                            address = devAddress,
                            rssi = rssi,
                            isBonded = device.bondState == BluetoothDevice.BOND_BONDED,
                            isHeadSetCompatible = isHs
                        )
                    }
                    state.copy(discoveredDevices = updated)
                }

                if (devName.contains(targetDeviceName, ignoreCase = true)) {
                    Log.i(tag, "HeadSet4 localizado no scan! Conectando a $devAddress...")
                    stopScan()
                    connectToDevice(device)
                }
            }

            override fun onScanFailed(errorCode: Int) {
                Log.e(tag, "Scan BLE falhou: $errorCode")
                stopScan()
                _uiState.update {
                    it.copy(
                        connectionStatus = BleConnectionStatus.DISCONNECTED,
                        lastGattMessage = "Falha no scan BLE: código $errorCode"
                    )
                }
            }
        }

        try {
            // Also start general scan without strict filter to catch ESP32 if advertising incomplete name
            scanner.startScan(null, settings, scanCallback)
        } catch (e: Exception) {
            Log.e(tag, "startScan exception: ${e.message}")
        }

        // Auto-timeout scan after 15 seconds
        scanTimeoutJob?.cancel()
        scanTimeoutJob = scope.launch {
            delay(15000)
            if (isScanningActive) {
                stopScan()
                if (_uiState.value.connectionStatus == BleConnectionStatus.SCANNING) {
                    _uiState.update {
                        it.copy(
                            connectionStatus = BleConnectionStatus.DISCONNECTED,
                            lastGattMessage = "Nenhum HeadSet4 encontrado após 15s. Ligue o headset e tente novamente."
                        )
                    }
                }
            }
        }
    }

    fun stopScan() {
        if (isScanningActive && scanCallback != null) {
            try {
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            } catch (e: Exception) {
                Log.w(tag, "stopScan error: ${e.message}")
            }
        }
        isScanningActive = false
        scanCallback = null
        scanTimeoutJob?.cancel()
        _uiState.update { it.copy(isScanningDevices = false) }
    }

    /**
     * Connects to a specific BluetoothDevice over GATT with TRANSPORT_LE.
     */
    fun connectToDevice(device: BluetoothDevice) {
        stopScan()

        // Disconnect existing
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null

        val name = device.name?.ifEmpty { "HeadSet4" } ?: "HeadSet4"
        val address = device.address

        _uiState.update {
            it.copy(
                connectionStatus = BleConnectionStatus.CONNECTING,
                deviceName = name,
                deviceAddress = address,
                isSimulationMode = false,
                lastGattMessage = "Conectando ao GATT de $name (TRANSPORT_LE)..."
            )
        }

        logPacket("TX", "CONNECT -> $name ($address) TRANSPORT_LE")

        bluetoothGatt = device.connectGatt(
            context,
            false,
            gattCallback,
            BluetoothDevice.TRANSPORT_LE
        )
    }

    /**
     * Enables or disables hardware simulation mode when physical ESP32 is offline.
     */
    fun toggleSimulationMode(enable: Boolean) {
        if (enable) {
            disconnect()
            _uiState.update {
                it.copy(
                    connectionStatus = BleConnectionStatus.CONNECTED,
                    isGattConnected = true,
                    isSimulationMode = true,
                    deviceName = "HeadSet4 (Simulador ESP32)",
                    deviceAddress = "CC:50:E3:8A:2F:10",
                    negotiatedMtu = 512,
                    latencyMs = 15,
                    batteryPercent = 92,
                    batteryMillivolts = 4050,
                    isCharging = false,
                    isHeadsetWorn = true,
                    audioVolume = 75,
                    audioMuted = false,
                    displayBrightness = 80,
                    displayOn = true,
                    slot0Name = "Galaxy S24 Ultra",
                    slot0Connected = true,
                    slot1Name = "Notebook Dell",
                    slot1Connected = false,
                    activeWallpaperIdx = 1,
                    activeGifIdx = 0,
                    lastGattMessage = "Simulador HeadSet4 ativo · Protocolo JSON verificado"
                )
            }
            logPacket("RX", """{"status":"ok","cmd":"get_all_status","bat":92,"mv":4050,"prox":"worn","vol":75,"disp":true,"brilho":80}""", isSuccess = true)
            startSimulationTelemetry()
        } else {
            simulationTelemetryJob?.cancel()
            disconnect()
        }
    }

    private fun startSimulationTelemetry() {
        simulationTelemetryJob?.cancel()
        simulationTelemetryJob = scope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(12000)
                if (_uiState.value.isSimulationMode) {
                    val simBat = (_uiState.value.batteryPercent - 1).coerceAtLeast(15)
                    _uiState.update { it.copy(batteryPercent = simBat) }
                    logPacket("RX", """{"evt":"sensor.battery","data":{"pct":$simBat,"mv":3980,"chg":false}}""")
                }
            }
        }
    }

    fun disconnect() {
        stopScan()
        heartbeatJob?.cancel()
        activeUploadJob?.cancel()
        simulationTelemetryJob?.cancel()

        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
        } catch (e: Exception) {
            Log.w(tag, "Gatt close error: ${e.message}")
        }
        bluetoothGatt = null
        gattQueue = null
        charCmdRx = null
        charNotifyTx = null
        charEqPreset = null
        charEqGains = null
        charBatteryLevel = null

        _uiState.update { state ->
            val list = state.discoveredDevices.map { it.copy(isConnected = false, isConnecting = false) }
            state.copy(
                connectionStatus = BleConnectionStatus.DISCONNECTED,
                isGattConnected = false,
                isAudioConnected = false,
                isSimulationMode = false,
                lastGattMessage = "Headphone desconectado",
                discoveredDevices = list,
                uploadProgress = null
            )
        }
        logPacket("TX", "DISCONNECT")
    }

    // ========================================================================
    // GATT CALLBACK & PROTOCOL PIPELINE
    // ========================================================================

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            Log.i(tag, "onConnectionStateChange status: $status, newState: $newState")

            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(tag, "GATT Conectado! Inicializando Fila e solicitando MTU 512...")
                    gattQueue = HeadSetGattQueue(gatt)

                    _uiState.update {
                        it.copy(
                            connectionStatus = BleConnectionStatus.CONNECTING,
                            isGattConnected = false,
                            lastGattMessage = "GATT conectado! Negociando MTU estendido (512 bytes)..."
                        )
                    }
                    logPacket("RX", "GATT_CONNECTED (status: $status)")

                    scope.launch(Dispatchers.IO) {
                        // 1. Negocia MTU estendido de 512 bytes conforme protocolo
                        val mtu = gattQueue?.requestMtu(512) ?: 23
                        Log.i(tag, "MTU negociado: $mtu bytes. Descobrindo serviços...")
                        _uiState.update { it.copy(negotiatedMtu = mtu, lastGattMessage = "MTU $mtu bytes. Descobrindo serviços GATT...") }
                        logPacket("RX", "MTU_NEGOTIATED: $mtu bytes")

                        // 2. Descobre os serviços GATT
                        gatt.discoverServices()
                    }
                }

                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.w(tag, "GATT Desconectado (status: $status)")
                    logPacket("RX", "GATT_DISCONNECTED (status: $status)", isError = true)
                    gattQueue = null
                    try {
                        bluetoothGatt?.close()
                    } catch (e: Exception) {
                        Log.w(tag, "close: ${e.message}")
                    }
                    bluetoothGatt = null
                    heartbeatJob?.cancel()

                    _uiState.update {
                        it.copy(
                            connectionStatus = BleConnectionStatus.DISCONNECTED,
                            isGattConnected = false,
                            isAudioConnected = false,
                            lastGattMessage = "Conexão perdida (status: $status). Toque para reconectar."
                        )
                    }
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(tag, "onMtuChanged: $mtu, status: $status")
            gattQueue?.notifyMtuChanged(mtu, status)
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(tag, "Falha na descoberta de serviços: $status")
                logPacket("RX", "SERVICE_DISCOVERY_FAILED (status: $status)", isError = true)
                return
            }

            Log.i(tag, "Serviços GATT descobertos com sucesso! Mapeando características...")
            scope.launch(Dispatchers.IO) {
                setupCharacteristics(gatt)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            Log.d(tag, "onCharacteristicWrite: ${characteristic.uuid}, status: $status")
            if (status == 15 /* ESP_GATT_INSUF_ENCRYPTION */) {
                Log.w(tag, "GATT_INSUFFICIENT_ENCRYPTION detectado! Iniciando autenticação SMP (createBond)...")
                gatt.device.createBond()
            }
            gattQueue?.notifyCharacteristicWrite(status)
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            Log.d(tag, "onDescriptorWrite: ${descriptor.uuid}, status: $status")
            gattQueue?.notifyDescriptorWrite(status)
        }

        // Dual API Read Handlers (Supports API 26..35+)
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            Log.d(tag, "onCharacteristicRead (API 33+): ${characteristic.uuid}, status: $status, size: ${value.size}")
            gattQueue?.notifyCharacteristicRead(value, status)
            if (status == BluetoothGatt.GATT_SUCCESS) {
                handleNotificationValue(characteristic, value)
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: ByteArray(0)
            Log.d(tag, "onCharacteristicRead (legacy): ${characteristic.uuid}, status: $status, size: ${value.size}")
            gattQueue?.notifyCharacteristicRead(value, status)
            if (status == BluetoothGatt.GATT_SUCCESS) {
                handleNotificationValue(characteristic, value)
            }
        }

        // Dual API Notification Handlers (Supports API 26..35+)
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handleNotificationValue(characteristic, value)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: return
            handleNotificationValue(characteristic, value)
        }
    }

    /**
     * Configures Control and Battery CCCDs sequentially through the queue.
     */
    private suspend fun setupCharacteristics(gatt: BluetoothGatt) {
        val ctlService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_CONTROL)
        val eqService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_EQ)
        val batService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_BATTERY)

        charCmdRx = ctlService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_COMMAND_RX)
        charNotifyTx = ctlService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_NOTIFY_TX)
        charEqPreset = eqService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_EQ_PRESET)
        charEqGains = eqService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_EQ_GAINS)
        charBatteryLevel = batService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_BATTERY_LEVEL)

        // 1. Enable notifications on Telemetry TX (UUID 5E0A1C02)
        charNotifyTx?.let { tx ->
            gatt.setCharacteristicNotification(tx, true)
            val cccd = tx.getDescriptor(HeadSetGattAttributes.UUID_DESCRIPTOR_CCCD)
            if (cccd != null) {
                val ok = gattQueue?.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                Log.i(tag, "CCCD de Telemetria/Controle escrito: $ok")
            }
        }

        // 2. Enable notifications on SIG Battery Level (UUID 0x2A19)
        charBatteryLevel?.let { bat ->
            gatt.setCharacteristicNotification(bat, true)
            val batCccd = bat.getDescriptor(HeadSetGattAttributes.UUID_DESCRIPTOR_CCCD)
            if (batCccd != null) {
                val ok = gattQueue?.writeDescriptor(batCccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                Log.i(tag, "CCCD de Bateria escrito: $ok")
            }
            // Read initial battery level immediately
            val initialBatBytes = gattQueue?.readCharacteristic(bat)
            if (initialBatBytes != null && initialBatBytes.isNotEmpty()) {
                val pct = initialBatBytes[0].toInt() and 0xFF
                Log.i(tag, "Nível inicial de bateria lido via BAS: $pct%")
                _uiState.update { it.copy(batteryPercent = pct) }
            }
        }

        _uiState.update {
            it.copy(
                connectionStatus = BleConnectionStatus.CONNECTED,
                isGattConnected = true,
                isAudioConnected = true,
                isBonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED,
                lastGattMessage = "HeadSet4 conectado e pronto! Solicitando status..."
            )
        }

        logPacket("RX", "CANAL BLE PRONTO: Serviços de controle, equalizador e bateria ativos!", isSuccess = true)

        // Handshake formal de conexão com o HeadSet4: {"cmd": "app_connect", ...}
        sendJsonCommand("""{"cmd":"app_connect","client":"SuperHeadSetAPP","app_version":"1.0.0"}""")
        delay(80)

        // Request initial system snapshot: {"cmd": "get_all_status"}
        sendJsonCommand("""{"cmd":"get_all_status"}""")
        delay(100)
        // Request media gallery catalog: {"cmd": "get_media_info"}
        sendJsonCommand("""{"cmd":"get_media_info"}""")

        startHeartbeat()
    }

    private fun handleNotificationValue(characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        if (characteristic.uuid == HeadSetGattAttributes.UUID_CHAR_NOTIFY_TX) {
            val chunk = String(value, StandardCharsets.UTF_8)
            parseIncomingTelemetryChunk(chunk)
        } else if (characteristic.uuid == HeadSetGattAttributes.UUID_CHAR_BATTERY_LEVEL && value.isNotEmpty()) {
            val batteryPct = value[0].toInt() and 0xFF
            Log.i(tag, "Notificação de Bateria BAS recebida: $batteryPct%")
            _uiState.update { it.copy(batteryPercent = batteryPct) }
            logPacket("RX", "BAS Battery Level: $batteryPct%", isSuccess = true)
        }
    }

    private fun parseIncomingTelemetryChunk(chunk: String) {
        incomingBuffer.append(chunk)
        val content = incomingBuffer.toString()

        if (content.endsWith("\n") || content.endsWith("}")) {
            val completeJson = content.trim()
            incomingBuffer.clear()
            processCompleteJson(completeJson)
        }
    }

    private fun processCompleteJson(jsonStr: String) {
        try {
            val json = JSONObject(jsonStr)

            // 1. Heartbeat Pong: {"status":"ok","cmd":"ping","pong":true}
            if (json.optBoolean("pong", false) || (json.optString("cmd") == "ping" && json.optString("status") == "ok")) {
                val roundTrip = (System.currentTimeMillis() - lastPingSendTimestamp).toInt().coerceIn(4, 500)
                _uiState.update { it.copy(latencyMs = roundTrip, lastPingPongMs = System.currentTimeMillis()) }
                logPacket("RX", jsonStr, isSuccess = true)
                return
            }

            // 2. Correlation ID & Latency Matching
            val correlationId = if (json.has("id")) json.optInt("id") else null
            val pendingCmd = correlationId?.let { pendingCommands.remove(it) }
            if (pendingCmd != null) {
                val rtt = (System.currentTimeMillis() - pendingCmd.timestamp).toInt().coerceIn(4, 2000)
                _uiState.update { it.copy(latencyMs = rtt) }
            }

            // 3. Command Success Response: {"status":"ok","cmd":"..."}
            val status = json.optString("status", "")
            val cmd = json.optString("cmd", pendingCmd?.cmd ?: "")

            if (status == "ok" && cmd.isNotEmpty()) {
                val idSuffix = if (correlationId != null) " (#$correlationId)" else ""
                val msg = "HeadSet4 confirmou: $cmd$idSuffix com sucesso"
                logPacket("RX", jsonStr, isSuccess = true)
                setCommandFeedback(cmd, CommandStatus.SUCCESS, msg)

                // Check specific command responses
                when (cmd) {
                    "app_connect" -> {
                        val fw = json.optString("fw_version", "1.0.0")
                        val api = json.optInt("api_version", 1)
                        val headset = json.optString("headset", "HeadSet4")
                        val featArray = json.optJSONArray("features")
                        val featuresList = mutableListOf<String>()
                        if (featArray != null) {
                            for (i in 0 until featArray.length()) {
                                featuresList.add(featArray.optString(i))
                            }
                        }
                        Log.i(tag, "Handshake formal confirmado por $headset (FW v$fw, API v$api)! Features: $featuresList")
                        _uiState.update {
                            it.copy(
                                firmwareVersion = fw,
                                apiVersion = api,
                                features = featuresList,
                                lastGattMessage = "Handshake concluído com $headset (v$fw)!"
                            )
                        }
                    }
                    "set_wallpaper_idx" -> {
                        val active = json.optInt("active_img", _uiState.value.activeWallpaperIdx)
                        _uiState.update { it.copy(activeWallpaperIdx = active) }
                    }
                    "set_gif_idx" -> {
                        val active = json.optInt("active_gif", _uiState.value.activeGifIdx)
                        _uiState.update { it.copy(activeGifIdx = active) }
                    }
                    "delete_media_slot" -> {
                        val slot = json.optInt("slot", -1)
                        val isGif = json.optBoolean("is_gif", false)
                        Log.i(tag, "Slot $slot (${if (isGif) "GIF" else "Imagem"}) apagado na Flash NVS")
                        fetchMediaInfo()
                    }
                }
            }

            // 4. Command Error Response: {"status":"error", ...} or {"error":"...", "cmd":"..."}
            if (status == "error" || json.has("error")) {
                val reason = json.optString("reason", json.optString("error", "Comando rejeitado"))
                val detail = json.optString("detail", "")
                val fullReason = if (detail.isNotEmpty()) "$reason ($detail)" else reason
                val msg = "Falha no fone ($cmd): $fullReason"
                logPacket("RX", jsonStr, isError = true)
                setCommandFeedback(cmd, CommandStatus.ERROR, msg)
                return
            }

            // 4. Chunk ACK Response: {"status":"ack","seq":N,"received":480}
            if (status == "ack") {
                val seq = json.optInt("seq", -1)
                val bytes = json.optInt("received", 0)
                Log.d(tag, "Chunk ACK recebido para seq: $seq ($bytes bytes)")
                logPacket("RX", jsonStr, isSuccess = true)
                return
            }

            // 5. Global Snapshot Response (get_all_status): {"bt1":"...","bat":92,...}
            if (json.has("bt1") && json.has("bat")) {
                logPacket("RX", jsonStr, isSuccess = true)
                _uiState.update {
                    it.copy(
                        slot0Name = json.optString("bt1", it.slot0Name),
                        slot1Name = json.optString("bt2", it.slot1Name),
                        slot0Connected = json.optString("st1") == "Conectado",
                        slot1Connected = json.optString("st2") == "Conectado",
                        autoSwitchEnabled = json.optBoolean("alt", it.autoSwitchEnabled),
                        isHeadsetWorn = json.optString("prox") == "worn",
                        batteryPercent = json.optInt("bat", it.batteryPercent),
                        displayOn = json.optBoolean("disp", it.displayOn),
                        displayBrightness = json.optInt("brilho", it.displayBrightness),
                        audioVolume = json.optInt("vol", it.audioVolume),
                        activeWallpaperIdx = json.optInt("img", it.activeWallpaperIdx),
                        activeGifIdx = json.optInt("gif", it.activeGifIdx),
                        lastGattMessage = "Snapshot de estado completo sincronizado com HeadSet4!"
                    )
                }
                setCommandFeedback("get_all_status", CommandStatus.SUCCESS, "Snapshot sincronizado com o fone!")
                return
            }

            // 6. Media Catalog Response (get_media_info)
            if (cmd == "get_media_info" || json.has("images") || json.has("gifs")) {
                logPacket("RX", jsonStr, isSuccess = true)
                val activeImg = json.optInt("active_img", _uiState.value.activeWallpaperIdx)
                val activeGif = json.optInt("active_gif", _uiState.value.activeGifIdx)
                val imagesArray = json.optJSONArray("images")
                val gifsArray = json.optJSONArray("gifs")

                _uiState.update { state ->
                    var newImages = state.imageSlots
                    if (imagesArray != null) {
                        newImages = (0..5).map { idx ->
                            val obj = (0 until imagesArray.length())
                                .map { imagesArray.getJSONObject(it) }
                                .firstOrNull { it.optInt("slot") == idx }
                            if (obj != null) {
                                val hasData = obj.optBoolean("has_data", false)
                                val name = obj.optString("name", "Foto $idx")
                                val size = obj.optInt("size", 142080)
                                val existing = state.imageSlots.getOrNull(idx)
                                existing?.copy(
                                    id = idx,
                                    title = if (hasData) name else "Foto $idx",
                                    subtitle = if (hasData) "${size / 1024} KB RGB565" else "Disponível",
                                    isEmpty = !hasData,
                                    sizeBytes = size,
                                    hasData = hasData
                                ) ?: MediaSlot(idx, if (hasData) name else "Foto $idx", if (hasData) "${size / 1024} KB RGB565" else "Disponível", isEmpty = !hasData, sizeBytes = size, hasData = hasData)
                            } else {
                                state.imageSlots.getOrNull(idx) ?: MediaSlot(idx, "Foto $idx", "Disponível", isEmpty = true)
                            }
                        }
                    }

                    var newGifs = state.gifSlots
                    if (gifsArray != null) {
                        newGifs = (0..5).map { idx ->
                            val obj = (0 until gifsArray.length())
                                .map { gifsArray.getJSONObject(it) }
                                .firstOrNull { it.optInt("slot") == idx }
                            if (obj != null) {
                                val hasData = obj.optBoolean("has_data", false)
                                val name = obj.optString("name", "GIF $idx")
                                val size = obj.optInt("size", 142000)
                                val existing = state.gifSlots.getOrNull(idx)
                                existing?.copy(
                                    id = idx,
                                    title = if (hasData) name else "GIF $idx",
                                    subtitle = if (hasData) "${size / 1024} KB Loop" else "Disponível",
                                    isEmpty = !hasData,
                                    sizeBytes = size,
                                    hasData = hasData
                                ) ?: MediaSlot(idx, if (hasData) name else "GIF $idx", if (hasData) "${size / 1024} KB Loop" else "Disponível", isEmpty = !hasData, sizeBytes = size, hasData = hasData)
                            } else {
                                state.gifSlots.getOrNull(idx) ?: MediaSlot(idx, "GIF $idx", "Disponível", isEmpty = true)
                            }
                        }
                    }

                    state.copy(
                        activeWallpaperIdx = activeImg,
                        activeGifIdx = activeGif,
                        imageSlots = newImages,
                        gifSlots = newGifs,
                        lastGattMessage = "Catálogo de mídia sincronizado com o display!"
                    )
                }
                return
            }

            // 7. Differential Telemetry Event (Delta Stream): {"evt":"...","data":{...}}
            if (json.has("evt")) {
                val evt = json.getString("evt")
                val data = json.optJSONObject("data") ?: return
                logPacket("RX", jsonStr)

                when (evt) {
                    "app.connected" -> {
                        val fw = data.optString("fw_version", "1.0.0")
                        val api = data.optInt("api_version", 1)
                        val name = data.optString("name", "HeadSet4")
                        Log.i(tag, "Push proativo de boas-vindas 'app.connected' recebido de $name (FW v$fw, API v$api)!")
                        _uiState.update {
                            it.copy(
                                firmwareVersion = fw,
                                apiVersion = api,
                                lastGattMessage = "Boas-vindas recebidas de $name (FW v$fw)!"
                            )
                        }
                    }
                    "sensor.worn" -> {
                        _uiState.update { it.copy(isHeadsetWorn = true, lastGattMessage = "HeadSet colocado na cabeça (Sensor óptico APDS-9930)") }
                    }
                    "sensor.removed" -> {
                        _uiState.update { it.copy(isHeadsetWorn = false, lastGattMessage = "HeadSet retirado da cabeça") }
                    }
                    "sensor.battery" -> {
                        _uiState.update {
                            it.copy(
                                batteryPercent = data.optInt("pct", it.batteryPercent),
                                batteryMillivolts = data.optInt("mv", it.batteryMillivolts),
                                isCharging = data.optBoolean("chg", it.isCharging),
                                lastGattMessage = "Telemetria de bateria: ${data.optInt("pct")}% (${data.optInt("mv")} mV)"
                            )
                        }
                    }
                    "audio.volume" -> {
                        // Anti-Echo Check: ignore incoming echo if altered locally < 300ms ago
                        if (System.currentTimeMillis() - lastUserActionTimestamp > 300) {
                            _uiState.update {
                                it.copy(
                                    audioVolume = data.optInt("vol", it.audioVolume),
                                    audioMuted = data.optBoolean("mute", it.audioMuted)
                                )
                            }
                        }
                    }
                    "audio.mode" -> {
                        val mode = data.optInt("mode", 1)
                        val rate = data.optInt("rate", 44100)
                        _uiState.update { it.copy(audioMode = mode, audioSampleRate = rate) }
                    }
                    "display.wallpaper_changed" -> {
                        val activeIdx = data.optInt("active_idx", 0)
                        val name = data.optString("name", "Foto $activeIdx")
                        _uiState.update {
                            it.copy(activeWallpaperIdx = activeIdx, lastGattMessage = "Display: Wallpaper '$name' selecionado")
                        }
                    }
                    "display.gif_changed" -> {
                        val activeIdx = data.optInt("active_idx", 0)
                        val name = data.optString("name", "GIF $activeIdx")
                        _uiState.update {
                            it.copy(activeGifIdx = activeIdx, lastGattMessage = "Display: Animação GIF '$name' selecionada")
                        }
                    }
                    "cfg.changed" -> {
                        val key = data.optString("key")
                        val valInt = data.optInt("val", 0)
                        when (key) {
                            "disp_bright" -> _uiState.update { it.copy(displayBrightness = valInt) }
                            "disp_to" -> _uiState.update { it.copy(displayTimeoutSec = valInt) }
                            "disp_img_idx" -> _uiState.update { it.copy(activeWallpaperIdx = valInt) }
                            "disp_gif_idx" -> _uiState.update { it.copy(activeGifIdx = valInt) }
                            "vib_intens" -> _uiState.update { it.copy(vibracallIntensity = valInt) }
                            "ears_angle" -> _uiState.update { it.copy(earsAngle = valInt) }
                            "bt_autosw" -> _uiState.update { it.copy(autoSwitchEnabled = data.optBoolean("val", true)) }
                        }
                    }
                    "bt.link_up" -> {
                        val slot = data.optInt("slot", 0)
                        _uiState.update {
                            if (slot == 0) it.copy(slot0Connected = true) else it.copy(slot1Connected = true)
                        }
                    }
                    "bt.link_down" -> {
                        val slot = data.optInt("slot", 0)
                        _uiState.update {
                            if (slot == 0) it.copy(slot0Connected = false) else it.copy(slot1Connected = false)
                        }
                    }
                    "bt.peer_name" -> {
                        val slot = data.optInt("slot", 0)
                        val name = data.optString("name", "")
                        _uiState.update {
                            if (slot == 0) it.copy(slot0Name = name) else it.copy(slot1Name = name)
                        }
                    }
                    "bt.slot_changed" -> {
                        val active = data.optInt("active_slot", 0)
                        _uiState.update { it.copy(activeSlot = active) }
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(tag, "JSON parsing error: ${e.message} para string: $jsonStr")
        }
    }

    private fun setCommandFeedback(cmd: String, status: CommandStatus, message: String) {
        clearFeedbackJob?.cancel()
        _uiState.update {
            it.copy(
                activeCommand = CommandFeedback(cmd, status, message),
                lastGattMessage = message
            )
        }
        clearFeedbackJob = scope.launch {
            delay(3500)
            _uiState.update { it.copy(activeCommand = null) }
        }
    }

    private fun logPacket(direction: String, content: String, isSuccess: Boolean = false, isError: Boolean = false) {
        val entry = GattPacketLog(
            direction = direction,
            content = content,
            isSuccess = isSuccess,
            isError = isError
        )
        _uiState.update { state ->
            val updated = (listOf(entry) + state.recentPackets).take(40)
            state.copy(recentPackets = updated)
        }
    }

    // ========================================================================
    // COMMAND DISPATCHER (APP -> HEADSET)
    // ========================================================================

    fun sendJsonCommand(jsonPayload: String) {
        val rx = charCmdRx
        val queue = gattQueue

        lastUserActionTimestamp = System.currentTimeMillis()

        // Injeta automaticamente o Correlation ID caso não exista no payload JSON
        val (formattedWithId, cmdName, correlationId) = try {
            val obj = JSONObject(jsonPayload)
            val id = if (obj.has("id")) obj.getInt("id") else nextCorrelationId.getAndIncrement()
            obj.put("id", id)
            val name = obj.optString("cmd", "cmd")
            Triple(obj.toString() + "\n", name, id)
        } catch (e: Exception) {
            val fallbackId = nextCorrelationId.getAndIncrement()
            val clean = jsonPayload.trim()
            val withNl = if (clean.endsWith("\n")) clean else "$clean\n"
            Triple(withNl, "cmd", fallbackId)
        }

        pendingCommands[correlationId] = PendingCommand(correlationId, cmdName)

        setCommandFeedback(cmdName, CommandStatus.PENDING, "Enviando '$cmdName' (#$correlationId) para o HeadSet4...")
        logPacket("TX", formattedWithId.trim())

        if (_uiState.value.isSimulationMode) {
            // Simula resposta instantânea do ESP32 no modo de teste
            scope.launch {
                delay(80)
                processCompleteJson("""{"status":"ok","cmd":"$cmdName","id":$correlationId}""")
            }
            return
        }

        if (rx != null && queue != null) {
            scope.launch(Dispatchers.IO) {
                val bytes = formattedWithId.toByteArray(StandardCharsets.UTF_8)
                val success = queue.writeCharacteristic(rx, bytes)
                if (!success) {
                    Log.w(tag, "Falha de escrita no GATT para: $formattedWithId")
                    setCommandFeedback(cmdName, CommandStatus.ERROR, "Falha ao transmitir '$cmdName' (#$correlationId) via BLE")
                    logPacket("TX", "FALHA: $cmdName (#$correlationId)", isError = true)
                }
            }
        } else {
            Log.w(tag, "GATT não conectado. Comando não transmitido: $formattedWithId")
            setCommandFeedback(cmdName, CommandStatus.ERROR, "HeadSet4 desconectado. Conecte para executar.")
        }
    }

    private fun startHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(10000)
                if (_uiState.value.connectionStatus == BleConnectionStatus.CONNECTED && !_uiState.value.isSimulationMode) {
                    lastPingSendTimestamp = System.currentTimeMillis()
                    sendJsonCommand("""{"cmd":"ping"}""")
                }
            }
        }
    }

    // ========================================================================
    // DIRECT USER ACTIONS WITH AUTHENTIC FEEDBACK
    // ========================================================================

    fun setVolume(vol: Int) {
        val clamped = vol.coerceIn(0, 100)
        _uiState.update { it.copy(audioVolume = clamped, audioMuted = clamped == 0) }
        sendJsonCommand("""{"cmd":"set_volume","val":$clamped}""")

        try {
            audioManager?.let { am ->
                val max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
                val target = kotlin.math.round((clamped / 100f) * max).toInt().coerceIn(0, max)
                am.setStreamVolume(AudioManager.STREAM_MUSIC, target, 0)
            }
        } catch (e: Exception) {
            Log.w(tag, "Audio manager set volume: ${e.message}")
        }
    }

    fun setMute(muted: Boolean) {
        _uiState.update { it.copy(audioMuted = muted) }
        sendJsonCommand("""{"cmd":"set_mute","mute":$muted}""")

        try {
            audioManager?.let { am ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    val direction = if (muted) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE
                    am.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0)
                }
            }
        } catch (e: Exception) {
            Log.w(tag, "Audio manager mute sync: ${e.message}")
        }
    }

    fun setBrightness(brightness: Int) {
        val clamped = brightness.coerceIn(0, 100)
        _uiState.update { it.copy(displayBrightness = clamped) }
        sendJsonCommand("""{"cmd":"set_brightness","val":$clamped}""")
    }

    fun setDisplayState(enable: Boolean) {
        _uiState.update { it.copy(displayOn = enable) }
        sendJsonCommand("""{"cmd":"set_display_state","enable":$enable}""")
    }

    fun setDisplayTimeout(sec: Int) {
        _uiState.update { it.copy(displayTimeoutSec = sec) }
        sendJsonCommand("""{"cmd":"set_display_timeout","val":$sec}""")
    }

    fun setAutoSwitch(enable: Boolean) {
        _uiState.update { it.copy(autoSwitchEnabled = enable) }
        sendJsonCommand("""{"cmd":"set_auto_switch","enable":$enable}""")
    }

    fun selectSlot(slot: Int) {
        _uiState.update { it.copy(activeSlot = slot) }
        sendJsonCommand("""{"cmd":"select_slot","slot":$slot}""")
    }

    fun disconnectSlot(slot: Int) {
        _uiState.update {
            if (slot == 0) it.copy(slot0Connected = false) else it.copy(slot1Connected = false)
        }
        sendJsonCommand("""{"cmd":"disconnect_slot","slot":$slot}""")
    }

    fun startPairing() {
        sendJsonCommand("""{"cmd":"start_pairing"}""")
    }

    private fun dispatchMediaKey(keyCode: Int) {
        try {
            audioManager?.let { am ->
                val eventDown = KeyEvent(KeyEvent.ACTION_DOWN, keyCode)
                am.dispatchMediaKeyEvent(eventDown)
                val eventUp = KeyEvent(KeyEvent.ACTION_UP, keyCode)
                am.dispatchMediaKeyEvent(eventUp)
            }
        } catch (e: Exception) {
            Log.w(tag, "dispatchMediaKey error: ${e.message}")
        }
    }

    fun togglePlayback() {
        val currentlyActive = audioManager?.isMusicActive == true
        val nextPlaying = !currentlyActive
        _uiState.update { it.copy(isPlaying = nextPlaying, isMusicActive = nextPlaying) }
        dispatchMediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
        sendJsonCommand("""{"cmd":"media_play_pause"}""")
    }

    fun mediaNext() {
        dispatchMediaKey(KeyEvent.KEYCODE_MEDIA_NEXT)
        sendJsonCommand("""{"cmd":"media_next"}""")
    }

    fun mediaPrev() {
        dispatchMediaKey(KeyEvent.KEYCODE_MEDIA_PREVIOUS)
        sendJsonCommand("""{"cmd":"media_prev"}""")
    }

    fun setProximityEnable(enable: Boolean) {
        _uiState.update { it.copy(proximityEnabled = enable) }
        sendJsonCommand("""{"cmd":"set_prox_enable","enable":$enable}""")
    }

    fun setProximitySensitivity(sens: Int) {
        val clamped = sens.coerceIn(0, 1023)
        _uiState.update { it.copy(proximitySensitivity = clamped) }
        sendJsonCommand("""{"cmd":"set_prox_sensitivity","val":$clamped}""")
    }

    fun setVibracall(enable: Boolean, intensity: Int) {
        val clamped = intensity.coerceIn(0, 100)
        _uiState.update { it.copy(vibracallEnabled = enable, vibracallIntensity = clamped) }
        sendJsonCommand("""{"cmd":"set_vibracall","enable":$enable,"val":$clamped}""")
    }

    fun triggerVibrationTest(durationMs: Int = 300, intensity: Int = 90) {
        sendJsonCommand("""{"cmd":"trigger_vibrate","duration_ms":$durationMs,"intensity":$intensity}""")
    }

    fun setEarsEnable(enable: Boolean) {
        _uiState.update { it.copy(earsEnabled = enable) }
        sendJsonCommand("""{"cmd":"set_ears_enable","enable":$enable}""")
    }

    fun setEarsAngle(angle: Int) {
        val clamped = angle.coerceIn(0, 180)
        _uiState.update { it.copy(earsAngle = clamped) }
        sendJsonCommand("""{"cmd":"set_ears_angle","val":$clamped}""")
    }

    fun setEqPreset(preset: Int) {
        val gains = when (preset) {
            0 -> listOf(0, 0, 0, 0, 0) // Flat
            1 -> listOf(10, 6, 0, -2, -4) // Bass Boost
            2 -> listOf(-2, 2, 8, 4, 0) // Vocal Boost
            3 -> listOf(-4, -2, 2, 6, 10) // Treble Boost
            else -> _uiState.value.eqGains
        }
        _uiState.update { it.copy(eqPreset = preset, eqGains = gains) }
        sendJsonCommand("""{"cmd":"set_eq_preset","preset":$preset}""")
    }

    fun setEqGains(gains: List<Int>) {
        _uiState.update { it.copy(eqPreset = 255, eqGains = gains) }
        val gainsArrayStr = gains.joinToString(prefix = "[", postfix = "]")
        sendJsonCommand("""{"cmd":"set_eq_gains","gains":$gainsArrayStr}""")
    }

    fun setWallpaperIdx(idx: Int) {
        _uiState.update { it.copy(activeWallpaperIdx = idx) }
        sendJsonCommand("""{"cmd":"set_wallpaper_idx","val":$idx}""")
        sendJsonCommand("""{"cmd":"trigger_vibrate","duration_ms":60,"intensity":70}""")
    }

    fun setGifIdx(idx: Int) {
        _uiState.update { it.copy(activeGifIdx = idx) }
        sendJsonCommand("""{"cmd":"set_gif_idx","val":$idx}""")
        sendJsonCommand("""{"cmd":"trigger_vibrate","duration_ms":60,"intensity":70}""")
    }

    fun fetchMediaInfo() {
        sendJsonCommand("""{"cmd":"get_media_info"}""")
    }

    fun deleteMediaSlot(isGif: Boolean, slot: Int) {
        if (isGif) deleteGifSlot(slot) else deleteImageSlot(slot)
    }

    fun cancelUpload() {
        activeUploadJob?.cancel()
        activeUploadJob = null
        _uiState.update { it.copy(uploadProgress = null) }
        logPacket("TX", "UPLOAD_CANCELADO")
    }

    // ========================================================================
    // MEDIA TRANSFER PROTOCOL (RGB565 / CRC32 / AWAITING CHUNKS)
    // ========================================================================

    fun uploadImage(
        slot: Int,
        name: String,
        bitmap: Bitmap,
        setAsActive: Boolean = true,
        onComplete: () -> Unit = {}
    ) {
        activeUploadJob?.cancel()
        activeUploadJob = scope.launch(Dispatchers.IO) {
            try {
                val cleanName = name.trim().take(24).ifEmpty { "Wallpaper $slot" }
                _uiState.update {
                    it.copy(
                        uploadProgress = MediaUploadProgress(
                            isUploading = true,
                            type = "image",
                            slot = slot,
                            name = cleanName,
                            statusText = "Convertendo imagem para RGB565 Little-Endian (240×296 px)...",
                            previewBitmap = bitmap
                        )
                    )
                }

                // 1. Convert to RGB565 Little-Endian (exact 142,080 bytes)
                val rgb565Bytes = ImageConverter.convertToRgb565ByteArray(bitmap, 240, 296)
                val totalBytes = rgb565Bytes.size
                val crc32Hex = ImageConverter.calculateCrc32Hex(rgb565Bytes)
                val previewThumb = ImageConverter.rgb565ToBitmap(rgb565Bytes)

                _uiState.update {
                    it.copy(
                        uploadProgress = it.uploadProgress?.copy(
                            totalBytes = totalBytes,
                            crc32 = crc32Hex,
                            previewBitmap = previewThumb,
                            statusText = "Iniciando sessão com ESP32 (CRC32: $crc32Hex)..."
                        )
                    )
                }

                // 2. Handshake: media_upload_start
                val startCmd = """{"cmd":"media_upload_start","type":"image","slot":$slot,"size":$totalBytes,"crc32":"$crc32Hex","name":"$cleanName"}"""
                sendJsonCommand(startCmd)
                delay(150) // Wait for buffer allocation on ESP32 PSRAM

                // 3. Chunk streaming: 360 bytes raw per chunk (Base64 is 480 chars, fits MTU 512)
                val rawChunkSize = 360
                val totalChunks = (totalBytes + rawChunkSize - 1) / rawChunkSize
                var bytesSent = 0
                var seq = 0
                val startTime = System.currentTimeMillis()

                val rx = charCmdRx
                val queue = gattQueue

                for (offset in 0 until totalBytes step rawChunkSize) {
                    if (!isActive) break
                    val end = minOf(offset + rawChunkSize, totalBytes)
                    val chunkRaw = rgb565Bytes.copyOfRange(offset, end)
                    val base64Data = Base64.encodeToString(chunkRaw, Base64.NO_WRAP)

                    val chunkCmd = """{"cmd":"media_chunk","seq":$seq,"data":"$base64Data"}""" + "\n"

                    // If connected to real hardware, use suspending writeCharacteristic to wait for ACK!
                    if (rx != null && queue != null && !_uiState.value.isSimulationMode) {
                        val bytes = chunkCmd.toByteArray(StandardCharsets.UTF_8)
                        val ok = queue.writeCharacteristic(rx, bytes)
                        if (!ok) {
                            Log.w(tag, "Chunk $seq write timeout ou falha!")
                        }
                    }

                    bytesSent = end
                    seq++
                    val elapsedSec = (System.currentTimeMillis() - startTime) / 1000f
                    val speed = if (elapsedSec > 0.05f) (bytesSent / 1024f) / elapsedSec else 16.0f
                    val progressFraction = bytesSent.toFloat() / totalBytes.toFloat()

                    _uiState.update {
                        it.copy(
                            uploadProgress = it.uploadProgress?.copy(
                                currentChunk = seq,
                                totalChunks = totalChunks,
                                bytesSent = bytesSent,
                                progress = progressFraction,
                                speedKbps = speed,
                                statusText = "Transmitindo: pacote $seq de $totalChunks (${(progressFraction * 100).toInt()}%)"
                            )
                        )
                    }

                    delay(35) // 35ms throttling per chunk to protect ESP32 FreeRTOS tasks
                }

                delay(100)

                // 4. Finish and Commit to Flash NVS: media_upload_finish
                _uiState.update {
                    it.copy(
                        uploadProgress = it.uploadProgress?.copy(
                            statusText = "Validando CRC32 ($crc32Hex) e gravando na Flash NVS..."
                        )
                    )
                }
                val finishCmd = """{"cmd":"media_upload_finish","slot":$slot}"""
                sendJsonCommand(finishCmd)
                delay(150)

                // 5. If selected as active wallpaper, switch LCD display
                if (setAsActive) {
                    setWallpaperIdx(slot)
                }

                // 6. Haptic confirmation (60ms @ 70% intensity)
                sendJsonCommand("""{"cmd":"trigger_vibrate","duration_ms":60,"intensity":70}""")

                // 7. Update UI local slot representation
                _uiState.update { state ->
                    val updatedImages = state.imageSlots.map { s ->
                        if (s.id == slot) {
                            s.copy(
                                title = cleanName,
                                subtitle = "RGB565 142 KB (CRC: $crc32Hex)",
                                isEmpty = false,
                                sizeBytes = totalBytes,
                                hasData = true,
                                crc32 = crc32Hex,
                                previewBitmap = previewThumb,
                                type = "image"
                            )
                        } else s
                    }
                    state.copy(
                        imageSlots = updatedImages,
                        activeWallpaperIdx = if (setAsActive) slot else state.activeWallpaperIdx,
                        uploadProgress = state.uploadProgress?.copy(
                            progress = 1f,
                            isComplete = true,
                            statusText = "Upload Concluído! Imagem gravada no Slot #$slot"
                        )
                    )
                }

                setCommandFeedback("media_upload_finish", CommandStatus.SUCCESS, "Upload concluído! Slot #$slot gravado com sucesso.")
                onComplete()
                delay(1200)
                _uiState.update { it.copy(uploadProgress = null) }

            } catch (e: Exception) {
                Log.e(tag, "Upload image failed: ${e.message}", e)
                _uiState.update {
                    it.copy(
                        uploadProgress = it.uploadProgress?.copy(
                            isUploading = false,
                            isError = true,
                            errorMessage = "Erro na transferência: ${e.message}",
                            statusText = "Falha no upload BLE"
                        )
                    )
                }
                setCommandFeedback("media_upload", CommandStatus.ERROR, "Erro no upload: ${e.message}")
            }
        }
    }

    fun deleteImageSlot(slotId: Int) {
        _uiState.update { state ->
            val updated = state.imageSlots.map { slot ->
                if (slot.id == slotId) {
                    slot.copy(title = "Foto $slotId", subtitle = "Disponível", imgUrl = null, isEmpty = true, sizeBytes = 0, hasData = false, previewBitmap = null)
                } else slot
            }
            val nextActive = if (state.activeWallpaperIdx == slotId) {
                updated.firstOrNull { !it.isEmpty }?.id ?: 0
            } else state.activeWallpaperIdx
            state.copy(imageSlots = updated, activeWallpaperIdx = nextActive)
        }
        sendJsonCommand("""{"cmd":"delete_media_slot","slot":$slotId,"type":"image"}""")
    }

    fun deleteGifSlot(slotId: Int) {
        _uiState.update { state ->
            val updated = state.gifSlots.map { slot ->
                if (slot.id == slotId) {
                    slot.copy(title = "GIF $slotId", subtitle = "Disponível", isEmpty = true, sizeBytes = 0, hasData = false)
                } else slot
            }
            val nextActive = if (state.activeGifIdx == slotId) {
                updated.firstOrNull { !it.isEmpty }?.id ?: 0
            } else state.activeGifIdx
            state.copy(gifSlots = updated, activeGifIdx = nextActive)
        }
        sendJsonCommand("""{"cmd":"delete_media_slot","slot":$slotId,"type":"gif"}""")
    }
}
