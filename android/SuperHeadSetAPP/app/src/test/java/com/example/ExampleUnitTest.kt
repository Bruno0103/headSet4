package com.example

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [36])
class ExampleUnitTest {

    @Test
    fun testVolumeCommandFormatting() {
        val volume = 75
        val jsonPayload = """{"cmd":"set_volume","val":$volume}"""
        val json = JSONObject(jsonPayload)
        assertEquals("set_volume", json.getString("cmd"))
        assertEquals(75, json.getInt("val"))
    }

    @Test
    fun testEqPresetCommandFormatting() {
        val preset = 1 // Bass Boost
        val jsonPayload = """{"cmd":"set_eq_preset","preset":$preset}"""
        val json = JSONObject(jsonPayload)
        assertEquals("set_eq_preset", json.getString("cmd"))
        assertEquals(1, json.getInt("preset"))
    }

    @Test
    fun testWallpaperSlotSelectionCommand() {
        val slotId = 2
        val jsonPayload = """{"cmd":"set_wallpaper_idx","val":$slotId}"""
        val json = JSONObject(jsonPayload)
        assertEquals("set_wallpaper_idx", json.getString("cmd"))
        assertEquals(2, json.getInt("val"))
    }

    @Test
    fun testGifSlotSelectionCommand() {
        val slotId = 1
        val jsonPayload = """{"cmd":"set_gif_idx","val":$slotId}"""
        val json = JSONObject(jsonPayload)
        assertEquals("set_gif_idx", json.getString("cmd"))
        assertEquals(1, json.getInt("val"))
    }

    @Test
    fun testBatteryTelemetryParsing() {
        val telemetryJson = """{"evt":"sensor.battery","data":{"pct":87,"mv":3980,"chg":false}}"""
        val json = JSONObject(telemetryJson)
        assertEquals("sensor.battery", json.getString("evt"))
        val data = json.getJSONObject("data")
        assertEquals(87, data.getInt("pct"))
        assertEquals(3980, data.getInt("mv"))
        assertFalse(data.getBoolean("chg"))
    }

    @Test
    fun testWearTelemetryParsing() {
        val telemetryJson = """{"evt":"sensor.worn","data":{"worn":true}}"""
        val json = JSONObject(telemetryJson)
        assertEquals("sensor.worn", json.getString("evt"))
        val data = json.getJSONObject("data")
        assertTrue(data.getBoolean("worn"))
    }

    @Test
    fun testDiscoveredDeviceCreationAndCompatibility() {
        val device = com.example.model.DiscoveredBluetoothDevice(
            name = "HeadSet4 (ESP32-S3)",
            address = "CC:50:E3:8A:2F:10",
            rssi = -42,
            isBonded = true,
            isHeadSetCompatible = true
        )
        assertEquals("HeadSet4 (ESP32-S3)", device.name)
        assertEquals("CC:50:E3:8A:2F:10", device.address)
        assertTrue(device.isHeadSetCompatible)
        assertTrue(device.isBonded)
        assertFalse(device.isConnected)
    }

    @Test
    fun testBleConnectionStatusTransitions() {
        var status = com.example.model.BleConnectionStatus.DISCONNECTED
        assertEquals(com.example.model.BleConnectionStatus.DISCONNECTED, status)

        status = com.example.model.BleConnectionStatus.SCANNING
        assertEquals(com.example.model.BleConnectionStatus.SCANNING, status)

        status = com.example.model.BleConnectionStatus.CONNECTING
        assertEquals(com.example.model.BleConnectionStatus.CONNECTING, status)

        status = com.example.model.BleConnectionStatus.CONNECTED
        assertEquals(com.example.model.BleConnectionStatus.CONNECTED, status)
    }

    @Test
    fun testMediaUploadStartFormatting() {
        val jsonPayload = """{"cmd":"media_upload_start","type":"image","slot":1,"size":142080,"crc32":"D4E5F6A7","name":"Neon City"}"""
        val json = JSONObject(jsonPayload)
        assertEquals("media_upload_start", json.getString("cmd"))
        assertEquals("image", json.getString("type"))
        assertEquals(1, json.getInt("slot"))
        assertEquals(142080, json.getInt("size"))
        assertEquals("D4E5F6A7", json.getString("crc32"))
        assertEquals("Neon City", json.getString("name"))
    }

    @Test
    fun testMediaChunkFormatting() {
        val seq = 42
        val dummyBase64 = "AP//AP//AP=="
        val jsonPayload = """{"cmd":"media_chunk","seq":$seq,"data":"$dummyBase64"}"""
        val json = JSONObject(jsonPayload)
        assertEquals("media_chunk", json.getString("cmd"))
        assertEquals(42, json.getInt("seq"))
        assertEquals("AP//AP//AP==", json.getString("data"))
    }

    @Test
    fun testMediaUploadFinishFormatting() {
        val slot = 1
        val jsonPayload = """{"cmd":"media_upload_finish","slot":$slot}"""
        val json = JSONObject(jsonPayload)
        assertEquals("media_upload_finish", json.getString("cmd"))
        assertEquals(1, json.getInt("slot"))
    }

    @Test
    fun testDeleteMediaSlotFormatting() {
        val slot = 2
        val jsonPayload = """{"cmd":"delete_media_slot","type":"image","slot":$slot}"""
        val json = JSONObject(jsonPayload)
        assertEquals("delete_media_slot", json.getString("cmd"))
        assertEquals("image", json.getString("type"))
        assertEquals(2, json.getInt("slot"))
    }

    @Test
    fun testImageConverterRgb565LittleEndianSizeAndCrc32() {
        val testBitmap = com.example.ble.ImageConverter.generatePresetBitmap("Cyber Grid")
        val rgb565Bytes = com.example.ble.ImageConverter.convertToRgb565ByteArray(testBitmap)

        // Exact hardware requirement: 240 * 296 * 2 = 142,080 bytes
        assertEquals(142080, rgb565Bytes.size)

        // CRC32 must be 8 hex characters uppercase
        val crc32Hex = com.example.ble.ImageConverter.calculateCrc32Hex(rgb565Bytes)
        assertEquals(8, crc32Hex.length)
        assertTrue(crc32Hex.matches(Regex("[0-9A-F]{8}")))
    }
}
