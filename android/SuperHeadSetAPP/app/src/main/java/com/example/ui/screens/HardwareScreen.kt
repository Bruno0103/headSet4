package com.example.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BrightnessMedium
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Toys
import androidx.compose.material.icons.filled.Vibration
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.HeadSetUiState
import com.example.ui.components.CyberCard
import com.example.ui.components.CyberSlider
import com.example.ui.components.CyberSwitch
import com.example.ui.theme.CyberOutline
import com.example.ui.theme.CyberOutlineVariant
import com.example.ui.theme.CyberPrimary
import com.example.ui.theme.CyberPrimaryContainer
import com.example.ui.theme.CyberSecondaryFixed
import com.example.ui.theme.CyberSurfaceContainer
import com.example.ui.theme.CyberSurfaceContainerHigh
import com.example.ui.theme.CyberSurfaceContainerLowest
import com.example.ui.theme.JetBrainsMono
import com.example.ui.theme.SpaceGrotesk

@Composable
fun HardwareScreen(
    uiState: HeadSetUiState,
    onProxToggle: (Boolean) -> Unit,
    onProxSensitivityChange: (Int) -> Unit,
    onVibracallToggle: (Boolean) -> Unit,
    onVibracallIntensityChange: (Int) -> Unit,
    onVibracallTest: () -> Unit,
    onEarsToggle: (Boolean) -> Unit,
    onEarsAngleChange: (Int) -> Unit,
    onBrightnessChange: (Int) -> Unit,
    onDisplayToggle: (Boolean) -> Unit,
    onTimeoutChange: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Title Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Memory,
                        contentDescription = "Hardware",
                        tint = CyberPrimaryContainer,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = "Sensores & Atuadores",
                        style = MaterialTheme.typography.titleLarge.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    )
                }
                Text(
                    text = "ESP32-WROVER-E · 240 MHz · FreeRTOS SMP",
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberOutline,
                        fontSize = 12.sp
                    )
                )
            }
        }

        // Section 1: APDS-9930 Proximity & Wear Detection
        CyberCard {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(34.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Sensors,
                                contentDescription = "Sensor Óptico",
                                tint = CyberPrimaryContainer,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                        Column {
                            Text(
                                text = "SENSOR DE USO NO OUVIDO",
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    fontFamily = SpaceGrotesk,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            )
                            Text(
                                text = "APDS-9930 Infravermelho",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                )
                            )
                        }
                    }

                    CyberSwitch(
                        checked = uiState.proximityEnabled,
                        onCheckedChange = onProxToggle,
                        modifier = Modifier.testTag("switch_proximity_sensor")
                    )
                }

                // Status & Hysteresis Display
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(CyberSurfaceContainerLowest)
                        .padding(10.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(if (uiState.isHeadsetWorn) CyberSecondaryFixed else CyberOutline)
                        )
                        Text(
                            text = if (uiState.isHeadsetWorn) "DETECTADO: NO OUVIDO (WORN)" else "DETECTADO: FORA DA CABEÇA",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = if (uiState.isHeadsetWorn) CyberSecondaryFixed else CyberOutline,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    val offThreshold = (uiState.proximitySensitivity * 0.6f).toInt()
                    Text(
                        text = "Th: ${uiState.proximitySensitivity} / Off: $offThreshold",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutline
                        )
                    )
                }

                // Sensitivity Slider
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Limiar de Detecção (Sensibilidade)",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                        Text(
                            text = "${uiState.proximitySensitivity} counts",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                    CyberSlider(
                        value = uiState.proximitySensitivity.toFloat(),
                        onValueChange = { onProxSensitivityChange(it.toInt()) },
                        valueRange = 50f..600f,
                        modifier = Modifier.testTag("slider_prox_sensitivity")
                    )
                }
            }
        }

        // Section 2: Vibracall Haptic Feedback
        CyberCard {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(34.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Vibration,
                                contentDescription = "Vibracall",
                                tint = CyberPrimaryContainer,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                        Column {
                            Text(
                                text = "MOTOR HÁPTICO (VIBRACALL)",
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    fontFamily = SpaceGrotesk,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            )
                            Text(
                                text = "Alerta de Notificações & Chamadas",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                )
                            )
                        }
                    }

                    CyberSwitch(
                        checked = uiState.vibracallEnabled,
                        onCheckedChange = onVibracallToggle,
                        modifier = Modifier.testTag("switch_vibracall")
                    )
                }

                // Intensity Slider
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Intensidade PWM",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                        Text(
                            text = "${uiState.vibracallIntensity}%",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                    CyberSlider(
                        value = uiState.vibracallIntensity.toFloat(),
                        onValueChange = { onVibracallIntensityChange(it.toInt()) },
                        valueRange = 0f..100f,
                        modifier = Modifier.testTag("slider_vibracall")
                    )
                }

                // Vibration Test Trigger
                Button(
                    onClick = onVibracallTest,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyberSurfaceContainerHigh,
                        contentColor = CyberPrimary
                    ),
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag("btn_test_vibration")
                ) {
                    Text(
                        text = "DISPARAR TESTE DE VIBRAÇÃO (250ms)",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            }
        }

        // Section 3: Animatronic Ears Servos
        CyberCard {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(34.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Toys,
                                contentDescription = "Orelhas Servos",
                                tint = CyberPrimaryContainer,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                        Column {
                            Text(
                                text = "ORELHAS ANIMATRÔNICAS",
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    fontFamily = SpaceGrotesk,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            )
                            Text(
                                text = "Servomotores PWM de Expressividade",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                )
                            )
                        }
                    }

                    CyberSwitch(
                        checked = uiState.earsEnabled,
                        onCheckedChange = onEarsToggle,
                        modifier = Modifier.testTag("switch_ears")
                    )
                }

                // Angle Slider
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Ângulo do Servo",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                        Text(
                            text = "${uiState.earsAngle}°",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                    CyberSlider(
                        value = uiState.earsAngle.toFloat(),
                        onValueChange = { onEarsAngleChange(it.toInt()) },
                        valueRange = 0f..180f,
                        modifier = Modifier.testTag("slider_ears_angle")
                    )
                }

                // Angle Presets
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    val angles = listOf("Relaxado (45°)" to 45, "Alerta (90°)" to 90, "Expressivo (145°)" to 145)
                    for ((label, ang) in angles) {
                        val isSelected = uiState.earsAngle == ang
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(6.dp))
                                .background(if (isSelected) CyberPrimaryContainer else CyberSurfaceContainer)
                                .clickable { onEarsAngleChange(ang) }
                                .padding(vertical = 8.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = label,
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = if (isSelected) CyberSurfaceContainerLowest else MaterialTheme.colorScheme.onSurface,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }
                }
            }
        }

        // Section 4: Display & Power Management
        CyberCard {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(34.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.BrightnessMedium,
                                contentDescription = "Brilho Display",
                                tint = CyberPrimaryContainer,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                        Column {
                            Text(
                                text = "BACKLIGHT & ENERGIA DO LCD",
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    fontFamily = SpaceGrotesk,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            )
                            Text(
                                text = "PWM 5 kHz (GPIO 13)",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberOutline
                                )
                            )
                        }
                    }

                    CyberSwitch(
                        checked = uiState.displayOn,
                        onCheckedChange = onDisplayToggle,
                        modifier = Modifier.testTag("switch_display_state")
                    )
                }

                // Brightness Slider
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Brilho do Visor",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline
                            )
                        )
                        Text(
                            text = "${uiState.displayBrightness}%",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimaryContainer,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                    CyberSlider(
                        value = uiState.displayBrightness.toFloat(),
                        onValueChange = { onBrightnessChange(it.toInt()) },
                        valueRange = 0f..100f,
                        modifier = Modifier.testTag("slider_brightness")
                    )
                }

                // Display Timeout
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(
                        text = "Tempo de Inatividade (Sleep)",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutline
                        )
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        val timeouts = listOf("15s" to 15, "30s" to 30, "60s" to 60, "Sempre Ligada" to 0)
                        for ((txt, sec) in timeouts) {
                            val isSelected = uiState.displayTimeoutSec == sec
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isSelected) CyberPrimaryContainer else CyberSurfaceContainer)
                                    .clickable { onTimeoutChange(sec) }
                                    .padding(vertical = 8.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = txt,
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isSelected) CyberSurfaceContainerLowest else MaterialTheme.colorScheme.onSurface,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Bold
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
