package com.example.viewmodel

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.ble.HeadSetBleManager
import com.example.ble.ImageConverter
import com.example.model.NavTab
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(FlowPreview::class)
class HeadSetViewModel(
    val bleManager: HeadSetBleManager
) : ViewModel() {

    val uiState = bleManager.uiState

    private val _currentTab = MutableStateFlow(NavTab.DASHBOARD)
    val currentTab = _currentTab.asStateFlow()

    private val _galleryActiveTab = MutableStateFlow("images") // "images" or "gifs"
    val galleryActiveTab = _galleryActiveTab.asStateFlow()

    fun autoConnect() {
        bleManager.startScanAndConnect()
    }

    fun startBleScan() {
        bleManager.startScanAndConnect()
    }

    fun stopBleScan() {
        bleManager.stopScan()
    }

    fun connectToAddress(address: String) {
        val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter()
        val device = try {
            adapter?.getRemoteDevice(address)
        } catch (e: Exception) {
            null
        }
        if (device != null) {
            bleManager.connectToDevice(device)
        }
    }

    fun toggleSimulation(enable: Boolean) {
        bleManager.toggleSimulationMode(enable)
    }

    fun disconnect() {
        bleManager.disconnect()
    }

    // Debounced slider control flows to protect BLE radio throughput
    private val volumeFlow = MutableSharedFlow<Int>(extraBufferCapacity = 1)
    private val brightnessFlow = MutableSharedFlow<Int>(extraBufferCapacity = 1)
    private val proxSensitivityFlow = MutableSharedFlow<Int>(extraBufferCapacity = 1)
    private val vibracallFlow = MutableSharedFlow<Int>(extraBufferCapacity = 1)
    private val earsAngleFlow = MutableSharedFlow<Int>(extraBufferCapacity = 1)

    init {
        viewModelScope.launch {
            volumeFlow.debounce(50).collect { bleManager.setVolume(it) }
        }
        viewModelScope.launch {
            brightnessFlow.debounce(150).collect { bleManager.setBrightness(it) }
        }
        viewModelScope.launch {
            proxSensitivityFlow.debounce(150).collect { bleManager.setProximitySensitivity(it) }
        }
        viewModelScope.launch {
            vibracallFlow.debounce(150).collect { intensity ->
                bleManager.setVibracall(uiState.value.vibracallEnabled, intensity)
            }
        }
        viewModelScope.launch {
            earsAngleFlow.debounce(120).collect { bleManager.setEarsAngle(it) }
        }
    }

    fun selectTab(tab: NavTab) {
        _currentTab.value = tab
    }

    fun setGalleryTab(tab: String) {
        _galleryActiveTab.value = tab
    }

    fun onVolumeChange(vol: Int) {
        volumeFlow.tryEmit(vol)
    }

    fun onBrightnessChange(brightness: Int) {
        brightnessFlow.tryEmit(brightness)
    }

    fun onProxSensitivityChange(sens: Int) {
        proxSensitivityFlow.tryEmit(sens)
    }

    fun onVibracallIntensityChange(intensity: Int) {
        vibracallFlow.tryEmit(intensity)
    }

    fun onEarsAngleChange(angle: Int) {
        earsAngleFlow.tryEmit(angle)
    }

    fun onEqBandChange(bandIndex: Int, gainSteps: Int) {
        val currentGains = uiState.value.eqGains.toMutableList()
        if (bandIndex in currentGains.indices) {
            currentGains[bandIndex] = gainSteps
            bleManager.setEqGains(currentGains)
        }
    }

    fun refreshMediaCatalog() {
        bleManager.fetchMediaInfo()
    }

    /**
     * Starts image transfer with the full 240x296 RGB565 protocol.
     */
    fun startImageUpload(
        slot: Int,
        name: String,
        bitmap: Bitmap,
        setAsActive: Boolean = true
    ) {
        bleManager.uploadImage(
            slot = slot,
            name = name,
            bitmap = bitmap,
            setAsActive = setAsActive
        )
    }

    /**
     * Loads a Bitmap from device storage/gallery Uri and initiates media transfer.
     */
    fun uploadImageFromUri(
        context: Context,
        uri: Uri,
        slot: Int,
        name: String = "Galeria Celular",
        setAsActive: Boolean = true
    ) {
        viewModelScope.launch(Dispatchers.IO) {
            val bitmap = ImageConverter.loadBitmapFromUri(context, uri)
            if (bitmap != null) {
                withContext(Dispatchers.Main) {
                    startImageUpload(slot, name, bitmap, setAsActive)
                }
            }
        }
    }

    /**
     * Uploads a programmatic Cyberpunk Preset bitmap (Cyber Grid, Obsidian Core, Neon Matrix, Quantum Skull).
     */
    fun uploadCyberPreset(
        presetName: String,
        slot: Int,
        setAsActive: Boolean = true
    ) {
        viewModelScope.launch(Dispatchers.Default) {
            val bitmap = ImageConverter.generatePresetBitmap(presetName)
            withContext(Dispatchers.Main) {
                startImageUpload(slot, presetName, bitmap, setAsActive)
            }
        }
    }

    fun cancelMediaUpload() {
        bleManager.cancelUpload()
    }

    fun deleteMediaSlot(type: String, slotId: Int) {
        if (type == "images") {
            bleManager.deleteImageSlot(slotId)
        } else {
            bleManager.deleteGifSlot(slotId)
        }
    }
}
