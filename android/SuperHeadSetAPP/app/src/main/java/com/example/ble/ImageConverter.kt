package com.example.ble

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

/**
 * Utility for converting and formatting images for the HeadSet4 display.
 *
 * Target hardware specification:
 * - Dimensions: 240 x 296 pixels (exact area under 24px status bar: 320 - 24 = 296)
 * - Format: RGB565 Raw Bitmap (Little-Endian)
 * - Total size: 142,080 bytes (240 * 296 * 2)
 * - Byte 0 (LSB): [G2 G1 G0 B4 B3 B2 B1 B0]
 * - Byte 1 (MSB): [R4 R3 R2 R1 R0 G5 G4 G3]
 */
object ImageConverter {

    const val TARGET_WIDTH = 240
    const val TARGET_HEIGHT = 296
    const val RGB565_TOTAL_BYTES = TARGET_WIDTH * TARGET_HEIGHT * 2 // Exactly 142,080 bytes

    /**
     * Resizes and center-crops the input Bitmap to exactly 240x296 pixels,
     * maintaining aspect ratio without distortion, then converts it to
     * a 142,080-byte Little-Endian RGB565 raw byte array.
     */
    fun convertToRgb565ByteArray(
        source: Bitmap,
        targetW: Int = TARGET_WIDTH,
        targetH: Int = TARGET_HEIGHT
    ): ByteArray {
        val scaled = createAspectFillScaledBitmap(source, targetW, targetH)
        val byteBuffer = ByteBuffer.allocate(targetW * targetH * 2)
        byteBuffer.order(ByteOrder.LITTLE_ENDIAN)

        val pixels = IntArray(targetW * targetH)
        scaled.getPixels(pixels, 0, targetW, 0, 0, targetW, targetH)

        for (color in pixels) {
            val r = (color shr 16 and 0xFF) shr 3 // 5 bits red (0..31)
            val g = (color shr 8 and 0xFF) shr 2  // 6 bits green (0..63)
            val b = (color and 0xFF) shr 3         // 5 bits blue (0..31)

            val rgb565 = ((r shl 11) or (g shl 5) or b).toShort()
            byteBuffer.putShort(rgb565)
        }

        if (scaled != source) {
            scaled.recycle()
        }

        return byteBuffer.array()
    }

    /**
     * Calculates the standard 8-character uppercase hexadecimal CRC32 hash.
     */
    fun calculateCrc32Hex(data: ByteArray): String {
        val crc = CRC32()
        crc.update(data)
        return String.format("%08X", crc.value)
    }

    /**
     * Center-crops and scales a bitmap to target dimensions.
     */
    fun createAspectFillScaledBitmap(
        source: Bitmap,
        targetW: Int = TARGET_WIDTH,
        targetH: Int = TARGET_HEIGHT
    ): Bitmap {
        val result = Bitmap.createBitmap(targetW, targetH, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(result)

        val srcW = source.width.toFloat()
        val srcH = source.height.toFloat()
        val scale = maxOf(targetW / srcW, targetH / srcH)

        val scaledW = srcW * scale
        val scaledH = srcH * scale

        val left = (targetW - scaledW) / 2f
        val top = (targetH - scaledH) / 2f

        val destRect = Rect(left.toInt(), top.toInt(), (left + scaledW).toInt(), (top + scaledH).toInt())
        val srcRect = Rect(0, 0, source.width, source.height)

        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        canvas.drawBitmap(source, srcRect, destRect, paint)

        return result
    }

    /**
     * Decodes a Little-Endian RGB565 byte array back to an Android ARGB_8888 Bitmap
     * for local preview verification.
     */
    fun rgb565ToBitmap(
        bytes: ByteArray,
        width: Int = TARGET_WIDTH,
        height: Int = TARGET_HEIGHT
    ): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val pixels = IntArray(width * height)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)

        for (i in pixels.indices) {
            if (buffer.remaining() < 2) break
            val rgb565 = buffer.short.toInt() and 0xFFFF
            val r = ((rgb565 shr 11) and 0x1F) * 255 / 31
            val g = ((rgb565 shr 5) and 0x3F) * 255 / 63
            val b = (rgb565 and 0x1F) * 255 / 31
            pixels[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
        }

        bitmap.setPixels(pixels, 0, width, 0, 0, width, height)
        return bitmap
    }

    /**
     * Safely loads and downsamples a bitmap from a Uri.
     */
    fun loadBitmapFromUri(context: Context, uri: Uri): Bitmap? {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val source = android.graphics.ImageDecoder.createSource(context.contentResolver, uri)
                android.graphics.ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
                    decoder.allocator = android.graphics.ImageDecoder.ALLOCATOR_SOFTWARE
                    decoder.isMutableRequired = true
                }
            } else {
                @Suppress("DEPRECATION")
                MediaStore.Images.Media.getBitmap(context.contentResolver, uri)
            }
        } catch (e: Exception) {
            try {
                context.contentResolver.openInputStream(uri)?.use { stream ->
                    BitmapFactory.decodeStream(stream)
                }
            } catch (ex: Exception) {
                null
            }
        }
    }

    /**
     * Generates programmatic cyber-style preset bitmaps (240x296) for testing
     * or instant gallery filling without needing files.
     */
    fun generatePresetBitmap(presetName: String): Bitmap {
        val bitmap = Bitmap.createBitmap(TARGET_WIDTH, TARGET_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        when (presetName) {
            "Cyber Grid" -> {
                // Dark background
                paint.color = android.graphics.Color.rgb(10, 16, 26)
                canvas.drawRect(0f, 0f, TARGET_WIDTH.toFloat(), TARGET_HEIGHT.toFloat(), paint)

                // Cyan grid lines
                paint.color = android.graphics.Color.argb(90, 0, 229, 255)
                paint.strokeWidth = 1f
                for (x in 0..TARGET_WIDTH step 24) {
                    canvas.drawLine(x.toFloat(), 0f, x.toFloat(), TARGET_HEIGHT.toFloat(), paint)
                }
                for (y in 0..TARGET_HEIGHT step 24) {
                    canvas.drawLine(0f, y.toFloat(), TARGET_WIDTH.toFloat(), y.toFloat(), paint)
                }

                // Glowing center accent
                paint.color = android.graphics.Color.argb(180, 0, 229, 255)
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 3f
                canvas.drawCircle(TARGET_WIDTH / 2f, TARGET_HEIGHT / 2f, 60f, paint)

                paint.color = android.graphics.Color.argb(120, 255, 179, 0)
                canvas.drawCircle(TARGET_WIDTH / 2f, TARGET_HEIGHT / 2f, 40f, paint)
            }

            "Obsidian Core" -> {
                // Deep obsidian background
                paint.color = android.graphics.Color.rgb(12, 14, 18)
                canvas.drawRect(0f, 0f, TARGET_WIDTH.toFloat(), TARGET_HEIGHT.toFloat(), paint)

                // Geometric glowing diamond
                paint.color = android.graphics.Color.argb(220, 255, 61, 0)
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 3f
                val path = android.graphics.Path().apply {
                    moveTo(TARGET_WIDTH / 2f, 70f)
                    lineTo(TARGET_WIDTH - 40f, TARGET_HEIGHT / 2f)
                    lineTo(TARGET_WIDTH / 2f, TARGET_HEIGHT - 70f)
                    lineTo(40f, TARGET_HEIGHT / 2f)
                    close()
                }
                canvas.drawPath(path, paint)

                paint.color = android.graphics.Color.argb(160, 255, 215, 64)
                paint.strokeWidth = 2f
                val innerPath = android.graphics.Path().apply {
                    moveTo(TARGET_WIDTH / 2f, 100f)
                    lineTo(TARGET_WIDTH - 65f, TARGET_HEIGHT / 2f)
                    lineTo(TARGET_WIDTH / 2f, TARGET_HEIGHT - 100f)
                    lineTo(65f, TARGET_HEIGHT / 2f)
                    close()
                }
                canvas.drawPath(innerPath, paint)
            }

            "Neon Matrix" -> {
                paint.color = android.graphics.Color.rgb(5, 18, 12)
                canvas.drawRect(0f, 0f, TARGET_WIDTH.toFloat(), TARGET_HEIGHT.toFloat(), paint)

                paint.color = android.graphics.Color.argb(180, 0, 255, 136)
                paint.textSize = 14f
                paint.typeface = android.graphics.Typeface.MONOSPACE
                var y = 24f
                val textLines = listOf(
                    "01011001 HEADSET4",
                    "ESP32-WROVER-E 16M",
                    "PSRAM 8MB ALLOC",
                    "LVGL 9 LCD 240x296",
                    "CRC32 VERIFIED",
                    "BT DUAL-MODE A2DP",
                    "WM8960 5-BAND EQ",
                    "HAPTIC DRIVER PWM",
                    "APDS-9930 PROX ON",
                    "11001010 00110101"
                )
                for (line in textLines) {
                    canvas.drawText(line, 16f, y, paint)
                    y += 26f
                }
            }

            "Quantum Skull" -> {
                paint.color = android.graphics.Color.rgb(18, 10, 28)
                canvas.drawRect(0f, 0f, TARGET_WIDTH.toFloat(), TARGET_HEIGHT.toFloat(), paint)

                // Neon magenta & cyan rings
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 4f
                paint.color = android.graphics.Color.argb(200, 255, 0, 128)
                canvas.drawCircle(TARGET_WIDTH / 2f, 120f, 50f, paint)

                paint.color = android.graphics.Color.argb(200, 0, 229, 255)
                canvas.drawRect(70f, 170f, TARGET_WIDTH - 70f, 210f, paint)
                canvas.drawLine(100f, 170f, 100f, 210f, paint)
                canvas.drawLine(120f, 170f, 120f, 210f, paint)
                canvas.drawLine(140f, 170f, 140f, 210f, paint)
            }

            else -> { // Default Cyber Pulse
                paint.color = android.graphics.Color.rgb(12, 20, 32)
                canvas.drawRect(0f, 0f, TARGET_WIDTH.toFloat(), TARGET_HEIGHT.toFloat(), paint)

                paint.color = android.graphics.Color.argb(200, 0, 229, 255)
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 3f

                val wavePath = android.graphics.Path().apply {
                    moveTo(0f, TARGET_HEIGHT / 2f)
                    var x = 0f
                    while (x < TARGET_WIDTH) {
                        val y = (TARGET_HEIGHT / 2f) + kotlin.math.sin(x * 0.05).toFloat() * 45f
                        lineTo(x, y)
                        x += 4f
                    }
                }
                canvas.drawPath(wavePath, paint)
            }
        }

        return bitmap
    }
}
