package com.example.ble

import java.util.UUID

object HeadSetGattAttributes {
    // Service 1: Control & Telemetry (Proprietary 128-bit)
    val UUID_SERVICE_CONTROL: UUID = UUID.fromString("5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_COMMAND_RX: UUID = UUID.fromString("5E0A1C01-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_NOTIFY_TX: UUID = UUID.fromString("5E0A1C02-4D2F-4A7B-9C31-0E5F2A6B7C8D")

    // Service 2: Hardware Parametric Equalizer (WM8960)
    val UUID_SERVICE_EQ: UUID = UUID.fromString("5E0A1B00-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_EQ_PRESET: UUID = UUID.fromString("5E0A1B01-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_EQ_GAINS: UUID = UUID.fromString("5E0A1B02-4D2F-4A7B-9C31-0E5F2A6B7C8D")

    // Service 3: Battery Service (SIG Standard)
    val UUID_SERVICE_BATTERY: UUID = UUID.fromString("0000180F-0000-1000-8000-00805F9B34FB")
    val UUID_CHAR_BATTERY_LEVEL: UUID = UUID.fromString("00002A19-0000-1000-8000-00805F9B34FB")

    // Service 4: Google Fast Pair Service (GFPS)
    val UUID_SERVICE_FASTPAIR: UUID = UUID.fromString("0000FE2C-0000-1000-8000-00805F9B34FB")

    // Standard Client Characteristic Configuration Descriptor (CCCD)
    val UUID_DESCRIPTOR_CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")
}
