package com.example.ui.screens

import android.content.Intent
import android.provider.Settings
import androidx.compose.ui.platform.LocalContext

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AllInclusive
import androidx.compose.material.icons.filled.Animation
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Cached
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.GifBox
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.LaptopMac
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.RotateRight
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.Smartphone
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import com.example.model.BleConnectionStatus
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.example.model.HeadSetUiState
import com.example.model.MediaSlot
import com.example.ui.components.CyberBatteryGauge
import com.example.ui.components.CyberCard
import com.example.ui.components.CyberSlider
import com.example.ui.components.CyberSwitch
import com.example.ui.theme.CyberBackground
import com.example.ui.theme.CyberError
import com.example.ui.theme.CyberOutline
import com.example.ui.theme.CyberOutlineVariant
import com.example.ui.theme.CyberPrimary
import com.example.ui.theme.CyberPrimaryContainer
import com.example.ui.theme.CyberSecondaryContainer
import com.example.ui.theme.CyberSecondaryFixed
import com.example.ui.theme.CyberSecondaryFixedDim
import com.example.ui.theme.CyberSurfaceContainer
import com.example.ui.theme.CyberSurfaceContainerHigh
import com.example.ui.theme.CyberSurfaceContainerHighest
import com.example.ui.theme.CyberSurfaceContainerLow
import com.example.ui.theme.CyberSurfaceContainerLowest
import com.example.ui.theme.HankenGrotesk
import com.example.ui.theme.JetBrainsMono
import com.example.ui.theme.SpaceGrotesk

@Composable
fun DashboardScreen(
    uiState: HeadSetUiState,
    onVolumeChange: (Int) -> Unit,
    onMuteToggle: (Boolean) -> Unit,
    onPlaybackToggle: () -> Unit,
    onMediaNext: () -> Unit,
    onMediaPrev: () -> Unit,
    onSlotSwitch: (Int) -> Unit,
    onAutoSwitchToggle: (Boolean) -> Unit,
    onSelectWallpaper: (Int) -> Unit,
    onSelectGif: (Int) -> Unit,
    onOpenGallery: () -> Unit,
    onAutoConnect: () -> Unit = {},
    onDisconnect: () -> Unit = {},
    onOpenScanner: () -> Unit = {},
    onOpenConsole: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()

    val pulseAnim = rememberInfiniteTransition(label = "pulse")
    val pulseAlpha by pulseAnim.animateFloat(
        initialValue = 0.5f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(900, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulseAlpha"
    )

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Minimal Hero Telemetry / State
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Dynamic HeadSet Connection Status Chip (Interactive: toggles auto-connection)
            val isConnected = uiState.connectionStatus == BleConnectionStatus.CONNECTED
            val isConnecting = uiState.connectionStatus == BleConnectionStatus.CONNECTING
            val isScanning = uiState.connectionStatus == BleConnectionStatus.SCANNING
            val isDisconnected = uiState.connectionStatus == BleConnectionStatus.DISCONNECTED

            val chipBorderColor = when {
                isConnected -> CyberSecondaryFixed.copy(alpha = 0.35f)
                isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.35f)
                else -> CyberError.copy(alpha = 0.3f)
            }

            val chipTextColor = when {
                isConnected -> CyberSecondaryFixedDim
                isConnecting || isScanning -> CyberPrimaryContainer
                else -> CyberError
            }

            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(9999.dp))
                    .background(CyberSurfaceContainerLow)
                    .border(1.dp, chipBorderColor, RoundedCornerShape(9999.dp))
                    .clickable {
                        if (isConnected) onDisconnect() else onAutoConnect()
                    }
                    .padding(horizontal = 12.dp, vertical = 6.dp)
                    .testTag("btn_headset_status_chip"),
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
                    text = uiState.deviceName,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                )
                Text(text = "·", color = CyberOutlineVariant)
                Text(
                    text = when {
                        isConnected -> "CONECTADO"
                        isConnecting -> "CONECTANDO..."
                        isScanning -> "BUSCANDO..."
                        else -> "DESCONECTADO"
                    },
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = chipTextColor,
                        fontWeight = FontWeight.Bold
                    )
                )
            }

            // APDS-9930 Worn Sensor Status Chip
            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(9999.dp))
                    .background(CyberSurfaceContainerLow)
                    .border(1.dp, CyberOutlineVariant.copy(alpha = 0.3f), RoundedCornerShape(9999.dp))
                    .padding(horizontal = 12.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Sensors,
                    contentDescription = "Sensor de Proximidade",
                    tint = if (uiState.isHeadsetWorn) CyberSecondaryFixedDim else CyberOutline,
                    modifier = Modifier.size(16.dp)
                )
                Text(
                    text = if (uiState.isHeadsetWorn) "EM USO (WORN)" else "REMOVIDO",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = if (uiState.isHeadsetWorn) CyberSecondaryFixedDim else CyberOutline,
                        fontWeight = FontWeight.Bold
                    )
                )
            }
        }

        // Dedicated Visual Connection Status Indicator Card
        ConnectionStatusHeroCard(
            uiState = uiState,
            pulseAlpha = pulseAlpha,
            onAutoConnect = onAutoConnect,
            onDisconnect = onDisconnect,
            onOpenScanner = onOpenScanner,
            onOpenConsole = onOpenConsole
        )

        // Battery & Cell Status Card (Minimalist Monolith)
        CyberCard {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "NÍVEL DE ENERGIA",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            letterSpacing = 1.2.sp
                        )
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = "${uiState.batteryPercent}",
                            style = MaterialTheme.typography.displayLarge.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Light,
                                color = MaterialTheme.colorScheme.onSurface,
                                fontSize = 44.sp,
                                lineHeight = 48.sp
                            )
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "%",
                            style = MaterialTheme.typography.headlineMedium.copy(
                                fontFamily = SpaceGrotesk,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Light
                            ),
                            modifier = Modifier.padding(bottom = 6.dp)
                        )
                    }

                    Spacer(modifier = Modifier.height(4.dp))
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(3.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Bolt,
                                contentDescription = "Voltagem",
                                tint = CyberSecondaryFixed,
                                modifier = Modifier.size(14.dp)
                            )
                            val volts = String.format("%.2f V", uiState.batteryMillivolts / 1000f)
                            Text(
                                text = volts,
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            )
                        }
                        Text(text = "·", color = CyberOutline)
                        Text(
                            text = uiState.batteryCapacityMwh,
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                    }
                }

                // Precision Minimal Gauge Ring
                CyberBatteryGauge(percent = uiState.batteryPercent)
            }
        }

        // Bluetooth Multipoint Dual Slots
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "BLUETOOTH DUAL-SLOT (1-CLIQUE)",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberOutline,
                        letterSpacing = 1.sp
                    )
                )
                Button(
                    onClick = {
                        val nextSlot = if (uiState.activeSlot == 0) 1 else 0
                        onSlotSwitch(nextSlot)
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyberSurfaceContainerHigh,
                        contentColor = CyberPrimary
                    ),
                    shape = RoundedCornerShape(9999.dp),
                    modifier = Modifier.height(28.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Cached,
                        contentDescription = "Alternar Slot",
                        modifier = Modifier.size(16.dp),
                        tint = CyberPrimary
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = "ALTERNAR",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            fontSize = 10.sp
                        )
                    )
                }
            }

            val isBleConnected = uiState.connectionStatus == BleConnectionStatus.CONNECTED

            if (!isBleConnected) {
                // Estado fidedigno quando fone está desconectado: sem mocks ou slots fictícios
                CyberCard {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 14.dp, horizontal = 12.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Devices,
                            contentDescription = "Multipoint Offline",
                            tint = CyberOutline,
                            modifier = Modifier.size(28.dp)
                        )
                        Text(
                            text = "MULTIPOINT OFFLINE",
                            style = MaterialTheme.typography.labelMedium.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline,
                                fontWeight = FontWeight.Bold
                            )
                        )
                        Text(
                            text = "Conecte o HeadSet4 via BLE para consultar e alternar os dispositivos pareados em tempo real.",
                            style = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = HankenGrotesk,
                                color = CyberOutlineVariant
                            )
                        )
                    }
                }
            } else {
                // Slot 0
                val isSlot0Active = uiState.activeSlot == 0 && uiState.slot0Connected
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(CyberSurfaceContainerLow)
                        .border(
                            1.dp,
                            if (isSlot0Active) CyberSecondaryFixed.copy(alpha = 0.45f) else CyberOutlineVariant.copy(alpha = 0.2f),
                            RoundedCornerShape(12.dp)
                        )
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(40.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainer)
                                .border(1.dp, CyberOutlineVariant.copy(alpha = 0.25f), RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Smartphone,
                                contentDescription = "Smartphone",
                                tint = if (uiState.slot0Connected) CyberPrimaryContainer else CyberOutline,
                                modifier = Modifier.size(20.dp)
                            )
                        }

                        Column {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(
                                    text = if (uiState.slot0Connected) uiState.slot0Name else "Slot 1 (Livre)",
                                    style = MaterialTheme.typography.bodyLarge.copy(
                                        fontFamily = HankenGrotesk,
                                        fontWeight = FontWeight.Medium,
                                        color = if (uiState.slot0Connected) MaterialTheme.colorScheme.onSurface else CyberOutline
                                    )
                                )
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(
                                            when {
                                                isSlot0Active -> CyberSecondaryContainer.copy(alpha = 0.25f)
                                                uiState.slot0Connected -> CyberSurfaceContainerHighest
                                                else -> CyberSurfaceContainer
                                            }
                                        )
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = when {
                                            isSlot0Active -> "ATIVO (ÁUDIO)"
                                            uiState.slot0Connected -> "CONECTADO"
                                            else -> "EM ESPERA"
                                        },
                                        style = MaterialTheme.typography.labelSmall.copy(
                                            fontFamily = JetBrainsMono,
                                            color = when {
                                                isSlot0Active -> CyberSecondaryFixed
                                                uiState.slot0Connected -> CyberPrimaryContainer
                                                else -> CyberOutline
                                            },
                                            fontSize = 9.sp,
                                            fontWeight = FontWeight.Bold
                                        )
                                    )
                                }
                            }
                        }
                    }

                    if (isSlot0Active) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(CyberSecondaryFixed.copy(alpha = pulseAlpha))
                        )
                    } else {
                        Button(
                            onClick = { onSlotSwitch(0) },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CyberSurfaceContainerHigh,
                                contentColor = CyberPrimary
                            ),
                            shape = RoundedCornerShape(9999.dp),
                            modifier = Modifier.height(32.dp)
                        ) {
                            Text(
                                text = "ATIVAR (1-CLIQUE)",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }
                }

                // Slot 1
                val isSlot1Active = uiState.activeSlot == 1 && uiState.slot1Connected
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(CyberSurfaceContainerLow.copy(alpha = 0.85f))
                        .border(
                            1.dp,
                            if (isSlot1Active) CyberSecondaryFixed.copy(alpha = 0.45f) else CyberOutlineVariant.copy(alpha = 0.2f),
                            RoundedCornerShape(12.dp)
                        )
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(40.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainer)
                                .border(1.dp, CyberOutlineVariant.copy(alpha = 0.25f), RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.LaptopMac,
                                contentDescription = "Laptop",
                                tint = if (uiState.slot1Connected) CyberPrimaryContainer else CyberOutline,
                                modifier = Modifier.size(20.dp)
                            )
                        }

                        Column {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(
                                    text = if (uiState.slot1Connected) uiState.slot1Name else "Slot 2 (Livre)",
                                    style = MaterialTheme.typography.bodyLarge.copy(
                                        fontFamily = HankenGrotesk,
                                        fontWeight = FontWeight.Medium,
                                        color = if (uiState.slot1Connected) MaterialTheme.colorScheme.onSurfaceVariant else CyberOutline
                                    )
                                )
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(
                                            when {
                                                isSlot1Active -> CyberSecondaryContainer.copy(alpha = 0.25f)
                                                uiState.slot1Connected -> CyberSurfaceContainerHighest
                                                else -> CyberSurfaceContainer
                                            }
                                        )
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = when {
                                            isSlot1Active -> "ATIVO (ÁUDIO)"
                                            uiState.slot1Connected -> "CONECTADO"
                                            else -> "EM ESPERA"
                                        },
                                        style = MaterialTheme.typography.labelSmall.copy(
                                            fontFamily = JetBrainsMono,
                                            color = when {
                                                isSlot1Active -> CyberSecondaryFixed
                                                uiState.slot1Connected -> CyberPrimaryContainer
                                                else -> CyberOutline
                                            },
                                            fontSize = 9.sp,
                                            fontWeight = FontWeight.Bold
                                        )
                                    )
                                }
                            }
                        }
                    }

                    if (isSlot1Active) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(CyberSecondaryFixed.copy(alpha = pulseAlpha))
                        )
                    } else {
                        Button(
                            onClick = { onSlotSwitch(1) },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CyberSurfaceContainerHigh,
                                contentColor = CyberPrimary
                            ),
                            shape = RoundedCornerShape(9999.dp),
                            modifier = Modifier.height(32.dp)
                        ) {
                            Text(
                                text = "ATIVAR (1-CLIQUE)",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }
                }
            }
        }

        // Dedicated Headset Volume Control Only (No playback or track info)
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "CONTROLE DE VOLUME",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberOutline,
                        letterSpacing = 1.sp
                    )
                )
                Text(
                    text = "DSP Bit-Perfect · BLE",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberPrimary
                    )
                )
            }

            CyberCard {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    // Header row: Mute button, status, and large percentage
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            IconButton(
                                onClick = { onMuteToggle(!uiState.audioMuted) },
                                modifier = Modifier
                                    .size(38.dp)
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(CyberSurfaceContainerHigh)
                            ) {
                                Icon(
                                    imageVector = if (uiState.audioMuted || uiState.audioVolume == 0) Icons.Default.VolumeOff else Icons.Default.VolumeUp,
                                    contentDescription = "Silenciar / Ativar Volume",
                                    tint = if (uiState.audioMuted) CyberError else CyberPrimaryContainer,
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                            Column {
                                Text(
                                    text = if (uiState.audioMuted) "ÁUDIO MUTADO" else "VOLUME DO FONE",
                                    style = MaterialTheme.typography.bodyMedium.copy(
                                        fontFamily = SpaceGrotesk,
                                        fontWeight = FontWeight.Bold,
                                        color = if (uiState.audioMuted) CyberError else MaterialTheme.colorScheme.onSurface
                                    )
                                )
                                Text(
                                    text = "Codec Cirrus Logic WM8960",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberOutline,
                                        fontSize = 10.sp
                                    )
                                )
                            }
                        }

                        Row(verticalAlignment = Alignment.Bottom) {
                            Text(
                                text = if (uiState.audioMuted) "0" else "${uiState.audioVolume}",
                                style = MaterialTheme.typography.displayLarge.copy(
                                    fontFamily = SpaceGrotesk,
                                    fontWeight = FontWeight.Light,
                                    color = if (uiState.audioMuted) CyberError else MaterialTheme.colorScheme.onSurface,
                                    fontSize = 38.sp,
                                    lineHeight = 40.sp
                                )
                            )
                            Text(
                                text = "%",
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                ),
                                modifier = Modifier.padding(bottom = 4.dp, start = 2.dp)
                            )
                        }
                    }

                    // Tactile Slider with - and + step buttons
                    var localVol by remember(uiState.audioVolume) { mutableFloatStateOf(uiState.audioVolume.toFloat()) }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        IconButton(
                            onClick = {
                                val nextVol = (uiState.audioVolume - 5).coerceAtLeast(0)
                                localVol = nextVol.toFloat()
                                onVolumeChange(nextVol)
                            },
                            modifier = Modifier
                                .size(36.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh)
                                .testTag("btn_volume_down")
                        ) {
                            Icon(
                                imageVector = Icons.Default.Remove,
                                contentDescription = "Diminuir Volume",
                                tint = CyberOutline,
                                modifier = Modifier.size(18.dp)
                            )
                        }

                        Box(modifier = Modifier.weight(1f)) {
                            CyberSlider(
                                value = if (uiState.audioMuted) 0f else localVol,
                                onValueChange = { newVal ->
                                    localVol = newVal
                                    onVolumeChange(newVal.toInt())
                                },
                                valueRange = 0f..100f,
                                modifier = Modifier.testTag("volume_slider")
                            )
                        }

                        IconButton(
                            onClick = {
                                val nextVol = (uiState.audioVolume + 5).coerceAtMost(100)
                                localVol = nextVol.toFloat()
                                onVolumeChange(nextVol)
                            },
                            modifier = Modifier
                                .size(36.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh)
                                .testTag("btn_volume_up")
                        ) {
                            Icon(
                                imageVector = Icons.Default.Add,
                                contentDescription = "Aumentar Volume",
                                tint = CyberOutline,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                    }

                    // Quick Presets Row (0%, 25%, 50%, 75%, 100%)
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        val presets = listOf("0%" to 0, "25%" to 25, "50%" to 50, "75%" to 75, "100%" to 100)
                        for ((label, volPreset) in presets) {
                            val isSelected = !uiState.audioMuted && uiState.audioVolume == volPreset
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isSelected) CyberPrimaryContainer else CyberSurfaceContainer)
                                    .clickable {
                                        localVol = volPreset.toFloat()
                                        onVolumeChange(volPreset)
                                        if (volPreset == 0) onMuteToggle(true) else if (uiState.audioMuted) onMuteToggle(false)
                                    }
                                    .padding(vertical = 7.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = label,
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isSelected) CyberSurfaceContainerLowest else MaterialTheme.colorScheme.onSurface,
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

        // =========================================================================
        // PHYSICAL HEADSET LCD SCREEN (240x296) - DYNAMIC PURE BITMAP PREVIEW
        // =========================================================================
        var previewMode by remember { mutableStateOf("photo") } // "photo" or "gif"

        val activePhoto = uiState.imageSlots.find { it.id == uiState.activeWallpaperIdx }
            ?: uiState.imageSlots.firstOrNull { !it.isEmpty }
        val activeGif = uiState.gifSlots.find { it.id == uiState.activeGifIdx }
            ?: uiState.gifSlots.firstOrNull { !it.isEmpty }

        val currentPreviewSlot = if (previewMode == "photo") activePhoto else activeGif

        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "DISPLAY INTEGRADO",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberOutline,
                        letterSpacing = 1.sp
                    )
                )
                Text(
                    text = if (previewMode == "photo") "Slot Ativo: #${activePhoto?.id ?: 0}" else "Slot GIF Ativo: #${activeGif?.id ?: 0}",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberPrimaryContainer
                    )
                )
            }

            CyberCard {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Physical LCD 240x296 Frame with Sleek Bezel
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .clip(RoundedCornerShape(16.dp))
                            .background(CyberSurfaceContainerLowest)
                            .border(1.dp, CyberOutlineVariant.copy(alpha = 0.35f), RoundedCornerShape(16.dp))
                            .padding(12.dp)
                    ) {
                        // PURE 240x296 Screen Aspect Container (zero HUD text or overlays on top)
                        Box(
                            modifier = Modifier
                                .width(180.dp)
                                .aspectRatio(240f / 296f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF090C12))
                                .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.35f), RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            PureMediaSurface(
                                slot = currentPreviewSlot,
                                isGif = previewMode == "gif"
                            )
                        }

                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "240×296 PX IPS SCREEN",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline,
                                fontSize = 9.sp,
                                letterSpacing = 1.2.sp
                            )
                        )
                    }

                    // Direct Interactive Slot Selectors (Active Photo vs Active GIF)
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        // Card Foto Ativa
                        val isPhotoActive = previewMode == "photo"
                        val photoTitle = activePhoto?.title ?: "Foto 0"
                        val photoSubtitle = activePhoto?.subtitle ?: "Sem Foto"

                        Row(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (isPhotoActive) CyberSurfaceContainerHigh else CyberSurfaceContainer)
                                .border(
                                    1.dp,
                                    if (isPhotoActive) CyberSecondaryFixed.copy(alpha = 0.45f) else Color.Transparent,
                                    RoundedCornerShape(8.dp)
                                )
                                .clickable {
                                    previewMode = "photo"
                                    activePhoto?.let { onSelectWallpaper(it.id) }
                                }
                                .padding(horizontal = 12.dp, vertical = 10.dp)
                                .testTag("btn_select_active_photo"),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = photoTitle,
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isPhotoActive) CyberPrimary else MaterialTheme.colorScheme.onSurface,
                                        fontWeight = FontWeight.Bold
                                    ),
                                    maxLines = 1
                                )
                                Text(
                                    text = if (isPhotoActive) "Foto · Ativo" else "Foto · Alternar",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isPhotoActive) CyberSecondaryFixed else CyberOutline,
                                        fontSize = 10.sp
                                    ),
                                    maxLines = 1
                                )
                            }
                            Icon(
                                imageVector = if (isPhotoActive) Icons.Default.CheckCircle else Icons.Default.Cached,
                                contentDescription = "Selecionar Foto",
                                tint = if (isPhotoActive) CyberSecondaryFixed else CyberOutline,
                                modifier = Modifier.size(16.dp)
                            )
                        }

                        // Card GIF Ativo
                        val isGifActive = previewMode == "gif"
                        val gifTitle = activeGif?.title ?: "GIF 0"
                        val gifSubtitle = activeGif?.subtitle ?: "Sem GIF"

                        Row(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (isGifActive) CyberSurfaceContainerHigh else CyberSurfaceContainer)
                                .border(
                                    1.dp,
                                    if (isGifActive) CyberSecondaryFixed.copy(alpha = 0.45f) else Color.Transparent,
                                    RoundedCornerShape(8.dp)
                                )
                                .clickable {
                                    previewMode = "gif"
                                    activeGif?.let { onSelectGif(it.id) }
                                }
                                .padding(horizontal = 12.dp, vertical = 10.dp)
                                .testTag("btn_select_active_gif"),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = gifTitle,
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isGifActive) CyberPrimaryContainer else MaterialTheme.colorScheme.onSurface,
                                        fontWeight = FontWeight.Bold
                                    ),
                                    maxLines = 1
                                )
                                Text(
                                    text = if (isGifActive) "GIF · Ativo" else "GIF · Alternar",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isGifActive) CyberSecondaryFixed else CyberOutline,
                                        fontSize = 10.sp
                                    ),
                                    maxLines = 1
                                )
                            }
                            Icon(
                                imageVector = if (isGifActive) Icons.Default.CheckCircle else Icons.Default.Cached,
                                contentDescription = "Selecionar GIF",
                                tint = if (isGifActive) CyberSecondaryFixed else CyberOutline,
                                modifier = Modifier.size(16.dp)
                            )
                        }
                    }

                    // Gallery Trigger CTA
                    val filledImages = uiState.imageSlots.count { !it.isEmpty }
                    val filledGifs = uiState.gifSlots.count { !it.isEmpty }

                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .background(CyberSurfaceContainer)
                            .clickable { onOpenGallery() }
                            .padding(horizontal = 12.dp, vertical = 10.dp)
                            .testTag("btn_open_gallery"),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.PhotoLibrary,
                                contentDescription = "Galeria",
                                tint = CyberOutline,
                                modifier = Modifier.size(18.dp)
                            )
                            Text(
                                text = "Biblioteca de Mídias",
                                style = MaterialTheme.typography.labelMedium.copy(
                                    fontFamily = JetBrainsMono,
                                    color = MaterialTheme.colorScheme.onSurface,
                                    fontWeight = FontWeight.Medium
                                )
                            )
                        }
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = "$filledImages Fotos · $filledGifs GIFs",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                )
                            )
                            Icon(
                                imageVector = Icons.Default.ChevronRight,
                                contentDescription = "Ir para Galeria",
                                tint = CyberOutline,
                                modifier = Modifier.size(16.dp)
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * Pure 240x296 Bitmap rendering surface without any text or HUD overlays.
 */
@Composable
fun PureMediaSurface(
    slot: MediaSlot?,
    isGif: Boolean,
    modifier: Modifier = Modifier
) {
    if (slot == null || slot.isEmpty) {
        Column(
            modifier = modifier.fillMaxSize(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Icon(
                imageVector = if (isGif) Icons.Default.GifBox else Icons.Default.Image,
                contentDescription = "Vazio",
                tint = CyberOutline,
                modifier = Modifier.size(36.dp)
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = "SLOT LIVRE",
                style = MaterialTheme.typography.labelSmall.copy(
                    fontFamily = JetBrainsMono,
                    color = CyberOutline,
                    fontSize = 10.sp
                )
            )
        }
    } else {
        when {
            slot.imgUrl != null -> {
                AsyncImage(
                    model = slot.imgUrl,
                    contentDescription = slot.title,
                    contentScale = ContentScale.Crop,
                    modifier = modifier.fillMaxSize()
                )
            }
            slot.type == "matrix" || slot.type == "matrix_anim" -> {
                Column(
                    modifier = modifier
                        .fillMaxSize()
                        .background(Color(0xFF081512)),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    Text(
                        text = "1011001",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed,
                            fontSize = 12.sp
                        )
                    )
                    Text(
                        text = "0100110",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed.copy(alpha = 0.4f),
                            fontSize = 12.sp
                        )
                    )
                    Text(
                        text = "0010110",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed.copy(alpha = 0.9f),
                            fontSize = 12.sp
                        )
                    )
                    Text(
                        text = "1101001",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed.copy(alpha = 0.6f),
                            fontSize = 12.sp
                        )
                    )
                }
            }
            slot.type == "pulse" -> {
                Box(
                    modifier = modifier
                        .fillMaxSize()
                        .background(Color(0xFF101726)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.AllInclusive,
                        contentDescription = "Pulse",
                        tint = CyberPrimaryContainer,
                        modifier = Modifier.size(36.dp)
                    )
                }
            }
            slot.type == "radar" -> {
                Box(
                    modifier = modifier
                        .fillMaxSize()
                        .background(Color(0xFF0C1718)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.RotateRight,
                        contentDescription = "Radar",
                        tint = CyberPrimaryContainer,
                        modifier = Modifier.size(40.dp)
                    )
                }
            }
            slot.type == "skull" -> {
                Box(
                    modifier = modifier
                        .fillMaxSize()
                        .background(Color(0xFF1B1022)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.Animation,
                        contentDescription = "Skull FX",
                        tint = Color(0xFFD9C8FF),
                        modifier = Modifier.size(40.dp)
                    )
                }
            }
            else -> {
                Box(
                    modifier = modifier
                        .fillMaxSize()
                        .background(Color(0xFF14232C)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.Animation,
                        contentDescription = "Wave",
                        tint = CyberPrimary,
                        modifier = Modifier.size(36.dp)
                    )
                }
            }
        }
    }
}

@Composable
fun ConnectionStatusHeroCard(
    uiState: HeadSetUiState,
    pulseAlpha: Float,
    onAutoConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onOpenScanner: () -> Unit,
    onOpenConsole: () -> Unit
) {
    val isConnected = uiState.connectionStatus == BleConnectionStatus.CONNECTED
    val isConnecting = uiState.connectionStatus == BleConnectionStatus.CONNECTING
    val isScanning = uiState.connectionStatus == BleConnectionStatus.SCANNING
    val isDisconnected = uiState.connectionStatus == BleConnectionStatus.DISCONNECTED

    CyberCard(
        borderColor = when {
            isConnected -> CyberSecondaryFixed.copy(alpha = 0.45f)
            isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.45f)
            else -> CyberError.copy(alpha = 0.35f)
        }
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
            // Header Row: Status icon and title
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(44.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .background(CyberSurfaceContainerHigh)
                            .border(
                                1.dp,
                                when {
                                    isConnected -> CyberSecondaryFixed.copy(alpha = 0.5f)
                                    isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.5f)
                                    else -> CyberError.copy(alpha = 0.35f)
                                },
                                RoundedCornerShape(10.dp)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        when {
                            isConnected -> Icon(
                                imageVector = Icons.Default.BluetoothConnected,
                                contentDescription = "Conectado",
                                tint = CyberSecondaryFixed,
                                modifier = Modifier.size(24.dp)
                            )
                            isConnecting || isScanning -> CircularProgressIndicator(
                                modifier = Modifier.size(20.dp),
                                color = CyberPrimaryContainer,
                                strokeWidth = 2.dp
                            )
                            else -> Icon(
                                imageVector = Icons.Default.BluetoothDisabled,
                                contentDescription = "Desconectado",
                                tint = CyberError,
                                modifier = Modifier.size(22.dp)
                            )
                        }
                    }

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = when {
                                isConnected -> "${uiState.deviceName} Conectado"
                                isConnecting -> "Conectando ao HeadSet4..."
                                isScanning -> "Localizando HeadSet4..."
                                else -> "HeadSet4 Desconectado"
                            },
                            style = MaterialTheme.typography.titleMedium.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            ),
                            maxLines = 1
                        )
                        Text(
                            text = uiState.lastGattMessage,
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = if (isConnected) CyberSecondaryFixed else CyberOutline,
                                fontSize = 10.sp
                            ),
                            maxLines = 1
                        )
                    }
                }

                Spacer(modifier = Modifier.width(6.dp))

                // Status Pill
                Row(
                    modifier = Modifier
                        .clip(RoundedCornerShape(9999.dp))
                        .background(
                            when {
                                isConnected -> CyberSecondaryFixed.copy(alpha = 0.15f)
                                isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.15f)
                                else -> CyberError.copy(alpha = 0.15f)
                            }
                        )
                        .border(
                            1.dp,
                            when {
                                isConnected -> CyberSecondaryFixed.copy(alpha = 0.4f)
                                isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = 0.4f)
                                else -> CyberError.copy(alpha = 0.35f)
                            },
                            RoundedCornerShape(9999.dp)
                        )
                        .padding(horizontal = 10.dp, vertical = 5.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(5.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(
                                when {
                                    isConnected -> CyberSecondaryFixed.copy(alpha = pulseAlpha)
                                    isConnecting || isScanning -> CyberPrimaryContainer.copy(alpha = pulseAlpha)
                                    else -> CyberError
                                }
                            )
                    )
                    Text(
                        text = when {
                            isConnected -> "GATT ATIVO"
                            isConnecting -> "CONECTANDO"
                            isScanning -> "BUSCANDO"
                            else -> "DESCONECTADO"
                        },
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = when {
                                isConnected -> CyberSecondaryFixed
                                isConnecting || isScanning -> CyberPrimaryContainer
                                else -> CyberError
                            },
                            fontSize = 10.sp
                        )
                    )
                }
            }

            val context = LocalContext.current
            val isAudioConnected = uiState.isAudioConnected

            // Dual-Link Status Indicator (Áudio Classic A2DP + App BLE GATT)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainerLow)
                    .border(1.dp, CyberOutlineVariant.copy(alpha = 0.25f), RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Canal 1: Áudio de Mídia / Sistema (A2DP / HFP)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(if (isAudioConnected) CyberSecondaryFixed else CyberOutline.copy(alpha = 0.5f))
                    )
                    Text(
                        text = "ÁUDIO MÍDIA: ${if (isAudioConnected) "CONECTADO" else "DESCONECTADO"}",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = if (isAudioConnected) CyberSecondaryFixed else CyberOutline,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }

                // Canal 2: App de Controle (BLE GATT)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(if (isConnected) CyberPrimaryContainer else CyberOutline.copy(alpha = 0.5f))
                    )
                    Text(
                        text = "APP CONTROLE: ${if (isConnected) "CONECTADO" else "DESCONECTADO"}",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = if (isConnected) CyberPrimaryContainer else CyberOutline,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            }

            // Banner Interativo de Conexão Dividida (Esclarecimento e atalho)
            if (isConnected && !isAudioConnected) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerHigh)
                        .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.45f), RoundedCornerShape(8.dp))
                        .padding(10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "ÁUDIO DE MÍDIA DESCONECTADO",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                color = CyberPrimaryContainer
                            )
                        )
                        Text(
                            text = "O app está conectado enviando comandos, mas para ouvir músicas é necessário ativar o áudio Bluetooth nas configurações do Android.",
                            style = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = HankenGrotesk,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        )
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
                        onClick = {
                            try {
                                context.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS).apply {
                                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                                })
                            } catch (e: Exception) {
                                // Fallback silencioso
                            }
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberPrimaryContainer,
                            contentColor = Color(0xFF00363D)
                        ),
                        shape = RoundedCornerShape(6.dp),
                        modifier = Modifier.height(36.dp)
                    ) {
                        Text(
                            text = "CONECTAR ÁUDIO",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                fontSize = 10.sp
                            )
                        )
                    }
                }
            } else if (!isConnected && isAudioConnected) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerHigh)
                        .border(1.dp, CyberSecondaryFixed.copy(alpha = 0.45f), RoundedCornerShape(8.dp))
                        .padding(10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "FONE CONECTADO NO ANDROID",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                color = CyberSecondaryFixed
                            )
                        )
                        Text(
                            text = "O áudio do celular está pronto, mas o app ainda não sincronizou a telemetria com o fone.",
                            style = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = HankenGrotesk,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        )
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
                        onClick = onAutoConnect,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberSecondaryFixed,
                            contentColor = Color.Black
                        ),
                        shape = RoundedCornerShape(6.dp),
                        modifier = Modifier.height(36.dp)
                    ) {
                        Text(
                            text = "SINCRONIZAR APP",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                fontSize = 10.sp
                            )
                        )
                    }
                }
            }

            // Specs / Description row
            if (isConnected) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerLow)
                        .padding(horizontal = 10.dp, vertical = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "SERVIÇO GATT",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutlineVariant,
                                fontSize = 9.sp
                            )
                        )
                        Text(
                            text = "5E0A1C00 / 0x180F",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberSecondaryFixed,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text = "LATÊNCIA RTT",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutlineVariant,
                                fontSize = 9.sp
                            )
                        )
                        Text(
                            text = "${uiState.latencyMs} ms",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberSecondaryFixed,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    Column(horizontalAlignment = Alignment.End) {
                        Text(
                            text = "MTU NEGOCIADO",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutlineVariant,
                                fontSize = 9.sp
                            )
                        )
                        Text(
                            text = "${uiState.negotiatedMtu} Bytes",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                }
            } else {
                Text(
                    text = "Comunicação nativa BLE GATT com ESP32-WROVER-E (Telemetria push, ADC de bateria, upload de imagens RGB565 e controle de atuadores).",
                    style = MaterialTheme.typography.bodySmall.copy(
                        fontFamily = HankenGrotesk,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                )
            }

            // Direct Action Buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (isConnected) {
                    Button(
                        onClick = onOpenConsole,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberSurfaceContainerHigh,
                            contentColor = CyberPrimaryContainer
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .weight(1f)
                            .height(40.dp)
                    ) {
                        Text(
                            text = "CONSOLE GATT",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    OutlinedButton(
                        onClick = onDisconnect,
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = CyberError),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .weight(1f)
                            .height(40.dp)
                            .testTag("btn_disconnect_hero")
                    ) {
                        Text(
                            text = "DESCONECTAR",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                } else if (isConnecting || isScanning) {
                    OutlinedButton(
                        onClick = onDisconnect,
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = CyberOutline),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(40.dp)
                            .testTag("btn_cancel_connect")
                    ) {
                        Text(
                            text = "CANCELAR BUSCA",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                } else {
                    Button(
                        onClick = onOpenScanner,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberPrimaryContainer,
                            contentColor = CyberSurfaceContainerLowest
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .weight(1.3f)
                            .height(42.dp)
                            .testTag("btn_auto_connect_headphone")
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Bluetooth,
                                contentDescription = "Buscar",
                                modifier = Modifier.size(18.dp)
                            )
                            Text(
                                text = "BUSCAR HEADSET4",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }

                    Button(
                        onClick = onOpenConsole,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberSurfaceContainerHigh,
                            contentColor = CyberPrimaryContainer
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .weight(1f)
                            .height(42.dp)
                    ) {
                        Text(
                            text = "CONSOLE",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                }
            }
        }
    }
}
