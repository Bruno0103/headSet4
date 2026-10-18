package com.example.model

enum class NavTab {
    DASHBOARD,
    EQUALIZER,
    HARDWARE,
    DISPLAY
}

enum class BleConnectionStatus {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED
}

data class DiscoveredBluetoothDevice(
    val name: String,
    val address: String,
    val rssi: Int = -60,
    val isBonded: Boolean = false,
    val isHeadSetCompatible: Boolean = true,
    val isConnecting: Boolean = false,
    val isConnected: Boolean = false
)

data class MediaUploadProgress(
    val isUploading: Boolean = false,
    val type: String = "image", // "image" or "gif"
    val slot: Int = 0,
    val name: String = "",
    val currentChunk: Int = 0,
    val totalChunks: Int = 0,
    val bytesSent: Int = 0,
    val totalBytes: Int = 142080,
    val progress: Float = 0f,
    val speedKbps: Float = 0f,
    val crc32: String = "",
    val statusText: String = "",
    val previewBitmap: android.graphics.Bitmap? = null,
    val isComplete: Boolean = false,
    val isError: Boolean = false,
    val errorMessage: String? = null
)

data class MediaSlot(
    val id: Int,
    val title: String,
    val subtitle: String,
    val type: String = "image", // "image", "matrix", "wave", "pulse", "matrix_anim", "radar", "skull"
    val imgUrl: String? = null,
    val isEmpty: Boolean = false,
    val sizeBytes: Int = 142080,
    val hasData: Boolean = true,
    val crc32: String? = null,
    val previewBitmap: android.graphics.Bitmap? = null
)

enum class CommandStatus {
    IDLE,
    PENDING,
    SUCCESS,
    ERROR
}

data class CommandFeedback(
    val cmd: String,
    val status: CommandStatus = CommandStatus.IDLE,
    val message: String = "",
    val timestamp: Long = System.currentTimeMillis()
)

data class GattPacketLog(
    val id: Long = System.currentTimeMillis() + (0..999).random(),
    val timestamp: Long = System.currentTimeMillis(),
    val direction: String, // "TX" (App -> Fone) or "RX" (Fone -> App)
    val content: String,
    val isSuccess: Boolean = false,
    val isError: Boolean = false
)

data class HeadSetUiState(
    // BLE Link & Telemetry
    val connectionStatus: BleConnectionStatus = BleConnectionStatus.DISCONNECTED,
    val deviceName: String = "HeadSet4",
    val deviceAddress: String = "CC:50:E3:8A:2F:10",
    val negotiatedMtu: Int = 512,
    val latencyMs: Int = 12,
    val lastGattMessage: String = "Aguardando conexão BLE com o HeadSet4...",
    val discoveredDevices: List<DiscoveredBluetoothDevice> = emptyList(),
    val isScanningDevices: Boolean = false,
    val lastPingPongMs: Long = 0L,
    val isBonded: Boolean = false,
    val isSimulationMode: Boolean = false,
    val firmwareVersion: String = "1.0.0",
    val apiVersion: Int = 1,
    val features: List<String> = emptyList(),

    // Command Feedback & Protocol Console
    val activeCommand: CommandFeedback? = null,
    val recentPackets: List<GattPacketLog> = emptyList(),

    // Audio Route & Connected Headphone Detection
    val isAudioConnected: Boolean = false,
    val isGattConnected: Boolean = false,
    val audioDeviceType: String = "BLE GATT (ESP32)",
    val isMusicActive: Boolean = false,
    val connectedHeadphoneName: String = "HeadSet4",

    // Battery Telemetry (sensor.battery & Service 0x180F)
    val batteryPercent: Int = 0,
    val batteryMillivolts: Int = 0,
    val isCharging: Boolean = false,
    val batteryCapacityMwh: String = "Li-Ion 5200mAh",

    // Wear Detection (APDS-9930 - sensor.worn / sensor.removed)
    val isHeadsetWorn: Boolean = false,
    val proximityEnabled: Boolean = true,
    val proximitySensitivity: Int = 160,

    // Multipoint Bluetooth Dual Slots (bt.link_up / bt.link_down / bt.slot_changed / bt.peer_name)
    val slot0Name: String = "Dispositivo 1",
    val slot0Connected: Boolean = false,
    val slot1Name: String = "Dispositivo 2",
    val slot1Connected: Boolean = false,
    val activeSlot: Int = 0,
    val autoSwitchEnabled: Boolean = true,

    // Media & Audio Playback (audio.volume, audio.mode)
    val trackTitle: String = "HeadSet4 Audio Stream",
    val trackArtist: String = "ESP32-WROVER-E WM8960",
    val trackAlbumArtUrl: String? = null,
    val isPlaying: Boolean = false,
    val audioVolume: Int = 50,
    val audioMuted: Boolean = false,
    val audioMode: Int = 0, // 0: IDLE, 1: MUSIC_A2DP, 2: CALL_HFP, 3: TONE_SFX
    val audioSampleRate: Int = 44100,

    // 5-Band Hardware Equalizer (WM8960 Codec)
    // Band gains in 0.5 dB steps (-24 to +24 for -12 dB to +12 dB)
    val eqPreset: Int = 0, // 0: Flat, 1: Bass Boost, 2: Vocal Boost, 3: Treble Boost, 255: Custom
    val eqGains: List<Int> = listOf(0, 0, 0, 0, 0),

    // Hardware Actuators
    val vibracallEnabled: Boolean = true,
    val vibracallIntensity: Int = 80,
    val earsEnabled: Boolean = true,
    val earsAngle: Int = 90,

    // Physical Headset LCD Screen (240x296)
    val displayOn: Boolean = true,
    val displayBrightness: Int = 80,
    val displayTimeoutSec: Int = 30,
    val activeWallpaperIdx: Int = 0,
    val activeGifIdx: Int = 0,

    // Display Media Gallery Slots (6 images & 6 GIFs)
    val imageSlots: List<MediaSlot> = emptyList(),
    val gifSlots: List<MediaSlot> = emptyList(),

    // Real-Time Media Transfer Progress Engine
    val uploadProgress: MediaUploadProgress? = null
)
