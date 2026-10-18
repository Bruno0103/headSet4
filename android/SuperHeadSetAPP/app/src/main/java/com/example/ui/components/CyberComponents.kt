package com.example.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import coil.compose.AsyncImage
import com.example.model.BleConnectionStatus
import com.example.model.CommandFeedback
import com.example.model.CommandStatus
import com.example.model.DiscoveredBluetoothDevice
import com.example.model.GattPacketLog
import com.example.model.NavTab
import com.example.ui.theme.CyberBackground
import com.example.ui.theme.CyberError
import com.example.ui.theme.CyberOutline
import com.example.ui.theme.CyberOutlineVariant
import com.example.ui.theme.CyberPrimary
import com.example.ui.theme.CyberPrimaryContainer
import com.example.ui.theme.CyberSecondaryContainer
import com.example.ui.theme.CyberSecondaryFixed
import com.example.ui.theme.CyberSurfaceContainer
import com.example.ui.theme.CyberSurfaceContainerHigh
import com.example.ui.theme.CyberSurfaceContainerHighest
import com.example.ui.theme.CyberSurfaceContainerLow
import com.example.ui.theme.CyberSurfaceContainerLowest
import com.example.ui.theme.JetBrainsMono
import com.example.ui.theme.SpaceGrotesk
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun CyberTopBar(
    subtitle: String,
    connectionStatus: BleConnectionStatus,
    batteryPercent: Int,
    isCharging: Boolean,
    onConnectToggle: () -> Unit,
    onOpenConsole: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(64.dp)
            .background(CyberSurfaceContainerLowest.copy(alpha = 0.95f))
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        // Logo & Title
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainer)
                    .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.35f), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Default.Headphones,
                    contentDescription = "HeadSet4 Logo",
                    tint = CyberPrimaryContainer,
                    modifier = Modifier.size(22.dp)
                )
            }

            Column {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = "HeadSet4",
                        style = MaterialTheme.typography.titleMedium.copy(
                            fontFamily = SpaceGrotesk,
                            color = CyberPrimary,
                            fontWeight = FontWeight.Bold
                        )
                    )
                    Text(
                        text = "ESP32",
                        style = MaterialTheme.typography.labelSmall.copy(
                            color = CyberOutlineVariant,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 12.sp
                    )
                )
            }
        }

        // Connection, Battery & Terminal Actions
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Live Battery Chip (Shows real BAS / JSON battery)
            if (connectionStatus == BleConnectionStatus.CONNECTED) {
                Row(
                    modifier = Modifier
                        .clip(RoundedCornerShape(9999.dp))
                        .background(CyberSurfaceContainerHigh)
                        .border(1.dp, CyberSecondaryFixed.copy(alpha = 0.3f), RoundedCornerShape(9999.dp))
                        .padding(horizontal = 8.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Icon(
                        imageVector = if (isCharging) Icons.Default.BatteryChargingFull else Icons.Default.Bolt,
                        contentDescription = "Bateria",
                        tint = if (batteryPercent > 20) CyberSecondaryFixed else CyberError,
                        modifier = Modifier.size(13.dp)
                    )
                    Text(
                        text = "$batteryPercent%",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = if (batteryPercent > 20) CyberSecondaryFixed else CyberError,
                            fontSize = 10.sp
                        )
                    )
                }
            }

            // GATT Protocol Terminal Button
            IconButton(
                onClick = onOpenConsole,
                modifier = Modifier
                    .size(34.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainerHigh)
                    .border(1.dp, CyberOutlineVariant.copy(alpha = 0.35f), RoundedCornerShape(8.dp))
                    .testTag("btn_open_gatt_console")
            ) {
                Icon(
                    imageVector = Icons.Default.Terminal,
                    contentDescription = "Console GATT",
                    tint = CyberPrimaryContainer,
                    modifier = Modifier.size(18.dp)
                )
            }

            // BLE Connection Badge
            val isConnected = connectionStatus == BleConnectionStatus.CONNECTED
            val isConnecting = connectionStatus == BleConnectionStatus.CONNECTING
            val isScanning = connectionStatus == BleConnectionStatus.SCANNING

            val infiniteTransition = rememberInfiniteTransition(label = "pulse")
            val pulseAlpha by infiniteTransition.animateFloat(
                initialValue = 0.4f,
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    animation = tween(800, easing = FastOutSlowInEasing),
                    repeatMode = RepeatMode.Reverse
                ),
                label = "alpha"
            )

            val badgeBorderColor = when {
                isConnected -> CyberSecondaryFixed.copy(alpha = 0.4f)
                isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.4f)
                else -> CyberError.copy(alpha = 0.35f)
            }

            val badgeTextColor = when {
                isConnected -> CyberSecondaryFixed
                isConnecting || isScanning -> CyberPrimaryContainer
                else -> CyberError
            }

            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(9999.dp))
                    .background(CyberSurfaceContainerHigh.copy(alpha = 0.9f))
                    .border(1.dp, badgeBorderColor, RoundedCornerShape(9999.dp))
                    .clickable { onConnectToggle() }
                    .padding(horizontal = 10.dp, vertical = 6.dp)
                    .testTag("ble_status_badge"),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(
                            when {
                                isConnected -> CyberSecondaryFixed.copy(alpha = pulseAlpha)
                                isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = pulseAlpha)
                                else -> CyberError.copy(alpha = 0.85f)
                            }
                        )
                )
                Text(
                    text = when (connectionStatus) {
                        BleConnectionStatus.CONNECTED -> "GATT ON"
                        BleConnectionStatus.CONNECTING -> "LIGANDO..."
                        BleConnectionStatus.SCANNING -> "SCAN BLE"
                        BleConnectionStatus.DISCONNECTED -> "CONECTAR"
                    },
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = badgeTextColor,
                        fontWeight = FontWeight.Bold,
                        fontSize = 10.sp
                    )
                )
            }
        }
    }
}

/**
 * Animated High-Tech Command Feedback Banner.
 * Displays immediate visual acknowledgment when the headphone responds.
 */
@Composable
fun CommandFeedbackBanner(
    feedback: CommandFeedback?,
    modifier: Modifier = Modifier
) {
    AnimatedVisibility(
        visible = feedback != null && feedback.status != CommandStatus.IDLE,
        enter = slideInVertically() + fadeIn(),
        exit = slideOutVertically() + fadeOut(),
        modifier = modifier
    ) {
        if (feedback != null) {
            val bg = when (feedback.status) {
                CommandStatus.PENDING -> Color(0xFF262010)
                CommandStatus.SUCCESS -> Color(0xFF0A2B1D)
                CommandStatus.ERROR -> Color(0xFF331015)
                else -> CyberSurfaceContainerHigh
            }
            val border = when (feedback.status) {
                CommandStatus.PENDING -> Color(0xFFFFB800)
                CommandStatus.SUCCESS -> CyberSecondaryFixed
                CommandStatus.ERROR -> CyberError
                else -> CyberOutline
            }
            val textColor = when (feedback.status) {
                CommandStatus.PENDING -> Color(0xFFFFD566)
                CommandStatus.SUCCESS -> CyberSecondaryFixed
                CommandStatus.ERROR -> CyberError
                else -> MaterialTheme.colorScheme.onSurface
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(bg)
                    .border(1.dp, border.copy(alpha = 0.6f), RoundedCornerShape(10.dp))
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                when (feedback.status) {
                    CommandStatus.PENDING -> {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            color = Color(0xFFFFB800),
                            strokeWidth = 2.dp
                        )
                    }
                    CommandStatus.SUCCESS -> {
                        Icon(
                            imageVector = Icons.Default.CheckCircle,
                            contentDescription = "Confirmado",
                            tint = CyberSecondaryFixed,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    CommandStatus.ERROR -> {
                        Icon(
                            imageVector = Icons.Default.Error,
                            contentDescription = "Falha",
                            tint = CyberError,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    else -> {}
                }

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = when (feedback.status) {
                            CommandStatus.PENDING -> "ENVIANDO COMANDO BLE: ${feedback.cmd.uppercase()}"
                            CommandStatus.SUCCESS -> "CONFIRMAÇÃO DO HEADSET4: SUCESSO"
                            CommandStatus.ERROR -> "RESPOSTA DO HEADSET4: FALHA"
                            else -> ""
                        },
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = textColor,
                            fontSize = 9.sp,
                            letterSpacing = 0.8.sp
                        )
                    )
                    Text(
                        text = feedback.message,
                        style = MaterialTheme.typography.bodySmall.copy(
                            fontFamily = SpaceGrotesk,
                            color = MaterialTheme.colorScheme.onSurface,
                            fontWeight = FontWeight.Medium
                        ),
                        maxLines = 2
                    )
                }
            }
        }
    }
}

/**
 * Real-Time BLE GATT Packet Terminal Dialog.
 * Shows every raw JSON packet exchanged between smartphone and ESP32.
 */
@Composable
fun GattConsoleDialog(
    packets: List<GattPacketLog>,
    mtu: Int,
    connectionStatus: BleConnectionStatus,
    onSendPing: () -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(Color(0xFF090D14))
                .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.4f), RoundedCornerShape(16.dp))
                .padding(16.dp)
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Terminal,
                            contentDescription = "Console",
                            tint = CyberPrimaryContainer,
                            modifier = Modifier.size(20.dp)
                        )
                        Text(
                            text = "GATT Console · JSON Log",
                            style = MaterialTheme.typography.titleMedium.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        )
                    }

                    IconButton(onClick = onDismiss, modifier = Modifier.size(28.dp)) {
                        Icon(
                            imageVector = Icons.Default.Close,
                            contentDescription = "Fechar",
                            tint = CyberOutline,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }

                // Status info strip
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(6.dp))
                        .background(CyberSurfaceContainerLow)
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "STATUS: ${connectionStatus.name}",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed,
                            fontSize = 9.sp
                        )
                    )
                    Text(
                        text = "MTU: $mtu BYTES",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberPrimaryContainer,
                            fontSize = 9.sp
                        )
                    )
                    Text(
                        text = "CANAL: 5E0A1C01 / 02",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutline,
                            fontSize = 9.sp
                        )
                    )
                }

                // Packet List Stream
                val timeFormat = remember { SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()) }

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(280.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF04060A))
                        .border(1.dp, CyberOutlineVariant.copy(alpha = 0.2f), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    if (packets.isEmpty()) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                text = "Nenhum pacote BLE registrado ainda.\nConecte ao HeadSet4 para ver os comandos JSON em tempo real.",
                                style = MaterialTheme.typography.bodySmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline,
                                    fontSize = 11.sp
                                ),
                                textAlign = androidx.compose.ui.text.style.TextAlign.Center
                            )
                        }
                    } else {
                        LazyColumn(
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                            reverseLayout = false
                        ) {
                            items(packets, key = { it.id }) { pkt ->
                                val isTx = pkt.direction == "TX"
                                val tagColor = if (isTx) CyberPrimaryContainer else CyberSecondaryFixed
                                val textColor = when {
                                    pkt.isError -> CyberError
                                    pkt.isSuccess -> CyberSecondaryFixed
                                    else -> Color(0xFFD4E3ED)
                                }

                                Column(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(if (isTx) Color(0xFF0E1A22) else Color(0xFF0A1814))
                                        .padding(6.dp)
                                ) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceBetween
                                    ) {
                                        Text(
                                            text = if (isTx) "▲ [APP -> ESP32]" else "▼ [ESP32 -> APP]",
                                            style = MaterialTheme.typography.labelSmall.copy(
                                                fontFamily = JetBrainsMono,
                                                color = tagColor,
                                                fontWeight = FontWeight.Bold,
                                                fontSize = 9.sp
                                            )
                                        )
                                        Text(
                                            text = timeFormat.format(Date(pkt.timestamp)),
                                            style = MaterialTheme.typography.labelSmall.copy(
                                                fontFamily = JetBrainsMono,
                                                color = CyberOutline,
                                                fontSize = 9.sp
                                            )
                                        )
                                    }
                                    Spacer(modifier = Modifier.height(2.dp))
                                    Text(
                                        text = pkt.content,
                                        style = MaterialTheme.typography.labelSmall.copy(
                                            fontFamily = JetBrainsMono,
                                            color = textColor,
                                            fontSize = 10.sp
                                        )
                                    )
                                }
                            }
                        }
                    }
                }

                // Actions: Ping Test & Dismiss
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = onSendPing,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberPrimaryContainer,
                            contentColor = CyberSurfaceContainerLowest
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text(
                            text = "ENVIAR PING DE TESTE",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    OutlinedButton(
                        onClick = onDismiss,
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text(
                            text = "FECHAR",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                    }
                }
            }
        }
    }
}

/**
 * Dedicated BLE Scanner & Device Picker Dialog.
 * Allows discovering nearby ESP32 HeadSet4 devices with signal strength.
 */
@Composable
fun BleScannerDialog(
    discoveredDevices: List<DiscoveredBluetoothDevice>,
    isScanning: Boolean,
    isSimulationMode: Boolean,
    onStartScan: () -> Unit,
    onConnectDevice: (String) -> Unit,
    onToggleSimulation: (Boolean) -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(Color(0xFF090D14))
                .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.45f), RoundedCornerShape(16.dp))
                .padding(16.dp)
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.BluetoothSearching,
                            contentDescription = "Scan",
                            tint = CyberPrimaryContainer,
                            modifier = Modifier.size(22.dp)
                        )
                        Text(
                            text = "Dispositivos BLE",
                            style = MaterialTheme.typography.titleMedium.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        )
                    }

                    IconButton(onClick = onDismiss, modifier = Modifier.size(28.dp)) {
                        Icon(
                            imageVector = Icons.Default.Close,
                            contentDescription = "Fechar",
                            tint = CyberOutline,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }

                Text(
                    text = "O aplicativo procura dispositivos Bluetooth Low Energy anunciando 'HeadSet4' ou o serviço customizado 5E0A1C00.",
                    style = MaterialTheme.typography.bodySmall.copy(
                        fontFamily = SpaceGrotesk,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                )

                // Scan Action Trigger
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = onStartScan,
                        enabled = !isScanning,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberPrimaryContainer,
                            contentColor = CyberSurfaceContainerLowest
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        if (isScanning) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                color = CyberSurfaceContainerLowest,
                                strokeWidth = 2.dp
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "ESCANEANDO...",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        } else {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = "Escanear",
                                modifier = Modifier.size(16.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "BUSCAR DISPOSITIVOS",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }
                }

                // Device List
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 240.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerLow)
                        .border(1.dp, CyberOutlineVariant.copy(alpha = 0.2f), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    if (discoveredDevices.isEmpty()) {
                        Box(modifier = Modifier.fillMaxWidth().padding(16.dp), contentAlignment = Alignment.Center) {
                            Text(
                                text = if (isScanning) "Procurando HeadSet4 por perto..." else "Nenhum dispositivo encontrado.\nToque em 'Buscar Dispositivos'.",
                                style = MaterialTheme.typography.bodySmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline,
                                    fontSize = 11.sp
                                ),
                                textAlign = androidx.compose.ui.text.style.TextAlign.Center
                            )
                        }
                    } else {
                        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            items(discoveredDevices) { dev ->
                                val isHs = dev.isHeadSetCompatible
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clip(RoundedCornerShape(6.dp))
                                        .background(if (isHs) Color(0xFF0F1E19) else CyberSurfaceContainer)
                                        .border(
                                            1.dp,
                                            if (isHs) CyberSecondaryFixed.copy(alpha = 0.4f) else Color.Transparent,
                                            RoundedCornerShape(6.dp)
                                        )
                                        .padding(8.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Row(
                                            verticalAlignment = Alignment.CenterVertically,
                                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                                        ) {
                                            Text(
                                                text = dev.name,
                                                style = MaterialTheme.typography.bodyMedium.copy(
                                                    fontFamily = SpaceGrotesk,
                                                    fontWeight = FontWeight.Bold,
                                                    color = if (isHs) CyberSecondaryFixed else MaterialTheme.colorScheme.onSurface
                                                ),
                                                maxLines = 1
                                            )
                                            if (isHs) {
                                                Box(
                                                    modifier = Modifier
                                                        .clip(RoundedCornerShape(4.dp))
                                                        .background(CyberSecondaryFixed.copy(alpha = 0.15f))
                                                        .padding(horizontal = 4.dp, vertical = 2.dp)
                                                ) {
                                                    Text(
                                                        text = "ESP32",
                                                        style = MaterialTheme.typography.labelSmall.copy(
                                                            fontFamily = JetBrainsMono,
                                                            color = CyberSecondaryFixed,
                                                            fontSize = 8.sp,
                                                            fontWeight = FontWeight.Bold
                                                        )
                                                    )
                                                }
                                            }
                                        }
                                        Text(
                                            text = "${dev.address} · ${dev.rssi} dBm ${if (dev.isBonded) "· Pareado" else ""}",
                                            style = MaterialTheme.typography.labelSmall.copy(
                                                fontFamily = JetBrainsMono,
                                                color = CyberOutline,
                                                fontSize = 9.sp
                                            )
                                        )
                                    }

                                    Button(
                                        onClick = {
                                            onConnectDevice(dev.address)
                                            onDismiss()
                                        },
                                        colors = ButtonDefaults.buttonColors(
                                            containerColor = if (isHs) CyberSecondaryFixed else CyberSurfaceContainerHigh,
                                            contentColor = if (isHs) Color(0xFF002016) else MaterialTheme.colorScheme.onSurface
                                        ),
                                        shape = RoundedCornerShape(6.dp),
                                        modifier = Modifier.height(32.dp)
                                    ) {
                                        Text(
                                            text = "CONECTAR",
                                            style = MaterialTheme.typography.labelSmall.copy(
                                                fontFamily = JetBrainsMono,
                                                fontWeight = FontWeight.Bold,
                                                fontSize = 10.sp
                                            )
                                        )
                                    }
                                }
                            }
                        }
                    }
                }

                // Simulation Mode Toggle for testing without physical ESP32
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerLow)
                        .padding(horizontal = 10.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "Modo Simulação ESP32",
                            style = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        )
                        Text(
                            text = "Testa todas as telas e comandos com respostas simuladas da API",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline,
                                fontSize = 9.sp
                            )
                        )
                    }

                    CyberSwitch(
                        checked = isSimulationMode,
                        onCheckedChange = {
                            onToggleSimulation(it)
                            onDismiss()
                        }
                    )
                }
            }
        }
    }
}

@Composable
fun CyberCard(
    modifier: Modifier = Modifier,
    borderColor: Color = CyberOutlineVariant.copy(alpha = 0.35f),
    content: @Composable () -> Unit
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(CyberSurfaceContainerLow)
            .border(1.dp, borderColor, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        content()
    }
}

@Composable
fun CyberSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float>,
    modifier: Modifier = Modifier
) {
    Slider(
        value = value,
        onValueChange = onValueChange,
        valueRange = valueRange,
        colors = SliderDefaults.colors(
            thumbColor = CyberPrimaryContainer,
            activeTrackColor = CyberPrimaryContainer,
            inactiveTrackColor = CyberSurfaceContainerHigh
        ),
        modifier = modifier
    )
}

@Composable
fun CyberSwitch(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .width(46.dp)
            .height(26.dp)
            .clip(RoundedCornerShape(9999.dp))
            .background(
                if (checked) CyberSecondaryFixed.copy(alpha = 0.25f)
                else CyberSurfaceContainerHighest
            )
            .border(
                1.dp,
                if (checked) CyberSecondaryFixed.copy(alpha = 0.6f)
                else CyberOutlineVariant.copy(alpha = 0.4f),
                RoundedCornerShape(9999.dp)
            )
            .clickable(
                indication = null,
                interactionSource = remember { MutableInteractionSource() }
            ) { onCheckedChange(!checked) }
            .padding(2.dp),
        contentAlignment = if (checked) Alignment.CenterEnd else Alignment.CenterStart
    ) {
        Box(
            modifier = Modifier
                .size(20.dp)
                .clip(CircleShape)
                .background(if (checked) CyberSecondaryFixed else CyberOutline)
        )
    }
}

@Composable
fun CyberBatteryGauge(
    percent: Int,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier.size(68.dp),
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val strokeWidth = 5.dp.toPx()
            val radius = (size.minDimension - strokeWidth) / 2
            val center = this.center

            // Background Track Ring
            drawCircle(
                color = Color(0xFF1E2832),
                radius = radius,
                style = Stroke(width = strokeWidth)
            )

            // Progress Arc
            val sweep = (percent / 100f) * 360f
            val arcColor = when {
                percent > 40 -> CyberSecondaryFixed
                percent > 15 -> CyberPrimaryContainer
                else -> CyberError
            }

            drawArc(
                brush = Brush.sweepGradient(
                    listOf(arcColor.copy(alpha = 0.5f), arcColor)
                ),
                startAngle = -90f,
                sweepAngle = sweep,
                useCenter = false,
                style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
                imageVector = Icons.Default.Bolt,
                contentDescription = "Energia",
                tint = if (percent > 20) CyberSecondaryFixed else CyberError,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
fun CyberBottomNav(
    currentTab: NavTab,
    onTabSelected: (NavTab) -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(72.dp)
            .background(CyberSurfaceContainerLowest.copy(alpha = 0.95f))
            .border(
                width = 1.dp,
                color = CyberOutlineVariant.copy(alpha = 0.25f),
                shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp)
            )
            .padding(horizontal = 8.dp),
        horizontalArrangement = Arrangement.SpaceAround,
        verticalAlignment = Alignment.CenterVertically
    ) {
        NavItem(
            label = "STATUS",
            icon = Icons.Default.Headphones,
            selected = currentTab == NavTab.DASHBOARD,
            onClick = { onTabSelected(NavTab.DASHBOARD) },
            testTag = "nav_tab_dashboard"
        )
        NavItem(
            label = "EQUALIZADOR",
            icon = Icons.Default.GraphicEq,
            selected = currentTab == NavTab.EQUALIZER,
            onClick = { onTabSelected(NavTab.EQUALIZER) },
            testTag = "nav_tab_equalizer"
        )
        NavItem(
            label = "HARDWARE",
            icon = Icons.Default.Memory,
            selected = currentTab == NavTab.HARDWARE,
            onClick = { onTabSelected(NavTab.HARDWARE) },
            testTag = "nav_tab_hardware"
        )
        NavItem(
            label = "DISPLAY",
            icon = Icons.Default.GridView,
            selected = currentTab == NavTab.DISPLAY,
            onClick = { onTabSelected(NavTab.DISPLAY) },
            testTag = "nav_tab_display"
        )
    }
}

@Composable
private fun NavItem(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    onClick: () -> Unit,
    testTag: String
) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 6.dp)
            .testTag(testTag),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Box(
            modifier = Modifier
                .size(width = 48.dp, height = 30.dp)
                .clip(RoundedCornerShape(9999.dp))
                .background(
                    if (selected) CyberPrimaryContainer.copy(alpha = 0.18f) else Color.Transparent
                )
                .border(
                    width = 1.dp,
                    color = if (selected) CyberPrimaryContainer.copy(alpha = 0.45f) else Color.Transparent,
                    shape = RoundedCornerShape(9999.dp)
                ),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = if (selected) CyberPrimaryContainer else CyberOutline,
                modifier = Modifier.size(20.dp)
            )
        }
        Spacer(modifier = Modifier.height(3.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                fontFamily = JetBrainsMono,
                color = if (selected) CyberPrimaryContainer else CyberOutline,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
                fontSize = 10.sp
            )
        )
    }
}
