package com.example

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.lifecycleScope
import com.example.ble.HeadSetBleManager
import com.example.model.BleConnectionStatus
import com.example.model.NavTab
import com.example.ui.components.BleScannerDialog
import com.example.ui.components.CommandFeedbackBanner
import com.example.ui.components.CyberBottomNav
import com.example.ui.components.CyberTopBar
import com.example.ui.components.GattConsoleDialog
import com.example.ui.screens.DashboardScreen
import com.example.ui.screens.DisplayGalleryScreen
import com.example.ui.screens.EqualizerScreen
import com.example.ui.screens.HardwareScreen
import com.example.ui.theme.CyberBackground
import com.example.ui.theme.MyApplicationTheme
import com.example.viewmodel.HeadSetViewModel

class MainActivity : ComponentActivity() {

    private lateinit var bleManager: HeadSetBleManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        bleManager = HeadSetBleManager(applicationContext, lifecycleScope)

        setContent {
            MyApplicationTheme {
                val viewModel = remember { HeadSetViewModel(bleManager) }
                HeadSetApp(viewModel = viewModel)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (::bleManager.isInitialized) {
            bleManager.populateBondedDevices()
            bleManager.syncAudioState()
        }
    }
}

@Composable
fun HeadSetApp(viewModel: HeadSetViewModel) {
    val uiState by viewModel.uiState.collectAsState()
    val currentTab by viewModel.currentTab.collectAsState()
    val galleryActiveTab by viewModel.galleryActiveTab.collectAsState()

    var showConsoleDialog by remember { mutableStateOf(false) }
    var showScannerDialog by remember { mutableStateOf(false) }

    // Request Bluetooth permissions on Android 12+ (API 31+)
    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val scanGranted = permissions[Manifest.permission.BLUETOOTH_SCAN] ?: true
        val connectGranted = permissions[Manifest.permission.BLUETOOTH_CONNECT] ?: true
        if (scanGranted && connectGranted) {
            viewModel.autoConnect()
        }
    }

    // Auto-search HeadSet4 on startup if permission is available
    LaunchedEffect(Unit) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissionLauncher.launch(
                arrayOf(
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
                )
            )
        }
        viewModel.autoConnect()
    }

    // BackHandler: If user is on a secondary tab, Back returns to Dashboard
    BackHandler(enabled = currentTab != NavTab.DASHBOARD) {
        viewModel.selectTab(NavTab.DASHBOARD)
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .background(CyberBackground),
        topBar = {
            CyberTopBar(
                subtitle = when (currentTab) {
                    NavTab.DASHBOARD -> "Dashboard"
                    NavTab.EQUALIZER -> "Equalizador de Hardware"
                    NavTab.HARDWARE -> "Sensores & Atuadores"
                    NavTab.DISPLAY -> "Galeria do Display"
                },
                connectionStatus = uiState.connectionStatus,
                batteryPercent = uiState.batteryPercent,
                isCharging = uiState.isCharging,
                onConnectToggle = {
                    showScannerDialog = true
                },
                onOpenConsole = {
                    showConsoleDialog = true
                },
                modifier = Modifier.windowInsetsPadding(WindowInsets.statusBars)
            )
        },
        bottomBar = {
            CyberBottomNav(
                currentTab = currentTab,
                onTabSelected = { viewModel.selectTab(it) },
                modifier = Modifier.windowInsetsPadding(WindowInsets.navigationBars)
            )
        },
        containerColor = CyberBackground
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            // Live Command Feedback Strip (Fidedigno: confirms success / error of each command from ESP32)
            CommandFeedbackBanner(feedback = uiState.activeCommand)

            Box(modifier = Modifier.weight(1f)) {
                when (currentTab) {
                    NavTab.DASHBOARD -> {
                        DashboardScreen(
                            uiState = uiState,
                            onVolumeChange = { viewModel.onVolumeChange(it) },
                            onMuteToggle = { viewModel.bleManager.setMute(it) },
                            onPlaybackToggle = { viewModel.bleManager.togglePlayback() },
                            onMediaNext = { viewModel.bleManager.mediaNext() },
                            onMediaPrev = { viewModel.bleManager.mediaPrev() },
                            onSlotSwitch = { viewModel.bleManager.selectSlot(it) },
                            onAutoSwitchToggle = { viewModel.bleManager.setAutoSwitch(it) },
                            onSelectWallpaper = { viewModel.bleManager.setWallpaperIdx(it) },
                            onSelectGif = { viewModel.bleManager.setGifIdx(it) },
                            onOpenGallery = { viewModel.selectTab(NavTab.DISPLAY) },
                            onAutoConnect = {
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                    permissionLauncher.launch(
                                        arrayOf(
                                            Manifest.permission.BLUETOOTH_SCAN,
                                            Manifest.permission.BLUETOOTH_CONNECT
                                        )
                                    )
                                }
                                viewModel.startBleScan()
                            },
                            onDisconnect = { viewModel.disconnect() },
                            onOpenScanner = { showScannerDialog = true },
                            onOpenConsole = { showConsoleDialog = true }
                        )
                    }

                    NavTab.EQUALIZER -> {
                        EqualizerScreen(
                            uiState = uiState,
                            onPresetSelect = { viewModel.bleManager.setEqPreset(it) },
                            onBandGainChange = { band, gain -> viewModel.onEqBandChange(band, gain) }
                        )
                    }

                    NavTab.HARDWARE -> {
                        HardwareScreen(
                            uiState = uiState,
                            onProxToggle = { viewModel.bleManager.setProximityEnable(it) },
                            onProxSensitivityChange = { viewModel.onProxSensitivityChange(it) },
                            onVibracallToggle = { viewModel.bleManager.setVibracall(it, uiState.vibracallIntensity) },
                            onVibracallIntensityChange = { viewModel.onVibracallIntensityChange(it) },
                            onVibracallTest = { viewModel.bleManager.triggerVibrationTest() },
                            onEarsToggle = { viewModel.bleManager.setEarsEnable(it) },
                            onEarsAngleChange = { viewModel.onEarsAngleChange(it) },
                            onBrightnessChange = { viewModel.onBrightnessChange(it) },
                            onDisplayToggle = { viewModel.bleManager.setDisplayState(it) },
                            onTimeoutChange = { viewModel.bleManager.setDisplayTimeout(it) }
                        )
                    }

                    NavTab.DISPLAY -> {
                        DisplayGalleryScreen(
                            uiState = uiState,
                            activeTab = galleryActiveTab,
                            onTabSwitch = { viewModel.setGalleryTab(it) },
                            onSelectSlot = { type, id ->
                                if (type == "images") viewModel.bleManager.setWallpaperIdx(id)
                                else viewModel.bleManager.setGifIdx(id)
                            },
                            onDeleteSlot = { type, id ->
                                viewModel.deleteMediaSlot(type, id)
                            },
                            onRefreshCatalog = {
                                viewModel.refreshMediaCatalog()
                            },
                            onUploadImage = { slot, name, bitmap, setAsActive ->
                                viewModel.startImageUpload(slot, name, bitmap, setAsActive)
                            },
                            onUploadPreset = { presetName, slot, setAsActive ->
                                viewModel.uploadCyberPreset(presetName, slot, setAsActive)
                            },
                            onCancelUpload = {
                                viewModel.cancelMediaUpload()
                            }
                        )
                    }
                }
            }
        }
    }

    // Modal GATT Console Dialog
    if (showConsoleDialog) {
        GattConsoleDialog(
            packets = uiState.recentPackets,
            mtu = uiState.negotiatedMtu,
            connectionStatus = uiState.connectionStatus,
            onSendPing = { viewModel.bleManager.sendJsonCommand("""{"cmd":"ping"}""") },
            onDismiss = { showConsoleDialog = false }
        )
    }

    // Modal BLE Device Scanner Dialog
    if (showScannerDialog) {
        BleScannerDialog(
            discoveredDevices = uiState.discoveredDevices,
            isScanning = uiState.isScanningDevices,
            isSimulationMode = uiState.isSimulationMode,
            onStartScan = {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    permissionLauncher.launch(
                        arrayOf(
                            Manifest.permission.BLUETOOTH_SCAN,
                            Manifest.permission.BLUETOOTH_CONNECT
                        )
                    )
                }
                viewModel.startBleScan()
            },
            onConnectDevice = { address ->
                viewModel.connectToAddress(address)
            },
            onToggleSimulation = { enable ->
                viewModel.toggleSimulation(enable)
            },
            onDismiss = { showScannerDialog = false }
        )
    }
}
