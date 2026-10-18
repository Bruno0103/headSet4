package com.example.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.os.Build
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Atomic serialized operation queue for Android BluetoothGatt.
 * Eliminates concurrent GATT requests that trigger GATT 133 / silent drops.
 */
@SuppressLint("MissingPermission")
class HeadSetGattQueue(private val gatt: BluetoothGatt) {

    private val tag = "HeadSetGattQueue"
    private val mutex = Mutex()

    private var activeMtuDeferred: CompletableDeferred<Int>? = null
    private var activeWriteDeferred: CompletableDeferred<Boolean>? = null
    private var activeDescriptorDeferred: CompletableDeferred<Boolean>? = null
    private var activeReadDeferred: CompletableDeferred<ByteArray>? = null

    suspend fun requestMtu(mtu: Int, timeoutMs: Long = 3000): Int? = mutex.withLock {
        val deferred = CompletableDeferred<Int>()
        activeMtuDeferred = deferred
        val initiated = gatt.requestMtu(mtu)
        if (!initiated) {
            Log.e(tag, "Failed to initiate gatt.requestMtu($mtu)")
            activeMtuDeferred = null
            return null
        }
        val result = withTimeoutOrNull(timeoutMs) { deferred.await() }
        activeMtuDeferred = null
        result
    }

    suspend fun writeCharacteristic(
        characteristic: BluetoothGattCharacteristic,
        data: ByteArray,
        timeoutMs: Long = 2000
    ): Boolean = mutex.withLock {
        val deferred = CompletableDeferred<Boolean>()
        activeWriteDeferred = deferred

        val initiated = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val status = gatt.writeCharacteristic(
                characteristic,
                data,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            )
            status == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            characteristic.value = data
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(characteristic)
        }

        if (!initiated) {
            Log.e(tag, "Failed to dispatch writeCharacteristic for ${characteristic.uuid}")
            activeWriteDeferred = null
            return false
        }

        val result = withTimeoutOrNull(timeoutMs) { deferred.await() } ?: false
        activeWriteDeferred = null
        result
    }

    suspend fun writeDescriptor(
        descriptor: BluetoothGattDescriptor,
        data: ByteArray,
        timeoutMs: Long = 2000
    ): Boolean = mutex.withLock {
        val deferred = CompletableDeferred<Boolean>()
        activeDescriptorDeferred = deferred

        val initiated = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val status = gatt.writeDescriptor(descriptor, data)
            status == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = data
            @Suppress("DEPRECATION")
            gatt.writeDescriptor(descriptor)
        }

        if (!initiated) {
            Log.e(tag, "Failed to dispatch writeDescriptor for ${descriptor.uuid}")
            activeDescriptorDeferred = null
            return false
        }

        val result = withTimeoutOrNull(timeoutMs) { deferred.await() } ?: false
        activeDescriptorDeferred = null
        result
    }

    suspend fun readCharacteristic(
        characteristic: BluetoothGattCharacteristic,
        timeoutMs: Long = 2000
    ): ByteArray? = mutex.withLock {
        val deferred = CompletableDeferred<ByteArray>()
        activeReadDeferred = deferred

        val initiated = gatt.readCharacteristic(characteristic)
        if (!initiated) {
            Log.e(tag, "Failed to dispatch readCharacteristic for ${characteristic.uuid}")
            activeReadDeferred = null
            return null
        }

        val result = withTimeoutOrNull(timeoutMs) { deferred.await() }
        activeReadDeferred = null
        result
    }

    fun notifyMtuChanged(mtu: Int, status: Int) {
        if (status == BluetoothGatt.GATT_SUCCESS) {
            activeMtuDeferred?.complete(mtu)
        } else {
            activeMtuDeferred?.complete(-1)
        }
    }

    fun notifyCharacteristicWrite(status: Int) {
        activeWriteDeferred?.complete(status == BluetoothGatt.GATT_SUCCESS)
    }

    fun notifyDescriptorWrite(status: Int) {
        activeDescriptorDeferred?.complete(status == BluetoothGatt.GATT_SUCCESS)
    }

    fun notifyCharacteristicRead(value: ByteArray, status: Int) {
        if (status == BluetoothGatt.GATT_SUCCESS) {
            activeReadDeferred?.complete(value)
        } else {
            activeReadDeferred?.complete(ByteArray(0))
        }
    }
}
