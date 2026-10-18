package com.example.ui.screens

import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.HeadSetUiState
import com.example.ui.components.CyberCard
import com.example.ui.components.CyberSlider
import com.example.ui.theme.CyberBackground
import com.example.ui.theme.CyberOutline
import com.example.ui.theme.CyberOutlineVariant
import com.example.ui.theme.CyberPrimary
import com.example.ui.theme.CyberPrimaryContainer
import com.example.ui.theme.CyberSecondaryFixed
import com.example.ui.theme.CyberSurfaceContainer
import com.example.ui.theme.CyberSurfaceContainerHigh
import com.example.ui.theme.CyberSurfaceContainerHighest
import com.example.ui.theme.CyberSurfaceContainerLowest
import com.example.ui.theme.JetBrainsMono
import com.example.ui.theme.SpaceGrotesk

@Composable
fun EqualizerScreen(
    uiState: HeadSetUiState,
    onPresetSelect: (Int) -> Unit,
    onBandGainChange: (Int, Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()

    val bands = listOf(
        Triple("100 Hz", "Sub-Bass", 0),
        Triple("300 Hz", "Médios-Graves", 1),
        Triple("1 kHz", "Vocal / Presença", 2),
        Triple("3 kHz", "Articulação", 3),
        Triple("10 kHz", "Brilho / Agudos", 4)
    )

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Title & Presets Header
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
                        imageVector = Icons.Default.GraphicEq,
                        contentDescription = "Equalizador",
                        tint = CyberPrimaryContainer,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = "Equalizador 5 Bandas",
                        style = MaterialTheme.typography.titleLarge.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    )
                }
                Text(
                    text = "Codec Cirrus WM8960 · Passos de 0,5 dB",
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberOutline,
                        fontSize = 12.sp
                    )
                )
            }

            IconButton(
                onClick = { onPresetSelect(0) },
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainerHigh)
            ) {
                Icon(
                    imageVector = Icons.Default.RestartAlt,
                    contentDescription = "Resetar Flat",
                    tint = CyberOutline
                )
            }
        }

        // Frequency Curve Visualizer Canvas
        CyberCard {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "RESPOSTA DE FREQUÊNCIA",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutline,
                            letterSpacing = 1.sp
                        )
                    )
                    Text(
                        text = when (uiState.eqPreset) {
                            0 -> "Preset: FLAT"
                            1 -> "Preset: BASS BOOST"
                            2 -> "Preset: VOCAL BOOST"
                            3 -> "Preset: TREBLE BOOST"
                            else -> "CUSTOMIZADO"
                        },
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberPrimaryContainer,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(130.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(CyberSurfaceContainerLowest)
                        .border(1.dp, CyberOutlineVariant.copy(alpha = 0.25f), RoundedCornerShape(10.dp))
                ) {
                    Canvas(modifier = Modifier.fillMaxSize().padding(horizontal = 16.dp, vertical = 12.dp)) {
                        val w = size.width
                        val h = size.height
                        val midY = h / 2f

                        // Draw Grid lines
                        drawLine(
                            color = CyberOutlineVariant.copy(alpha = 0.3f),
                            start = Offset(0f, midY),
                            end = Offset(w, midY),
                            strokeWidth = 1.dp.toPx()
                        )
                        drawLine(
                            color = CyberOutlineVariant.copy(alpha = 0.15f),
                            start = Offset(0f, midY - h * 0.35f),
                            end = Offset(w, midY - h * 0.35f),
                            strokeWidth = 1.dp.toPx()
                        )
                        drawLine(
                            color = CyberOutlineVariant.copy(alpha = 0.15f),
                            start = Offset(0f, midY + h * 0.35f),
                            end = Offset(w, midY + h * 0.35f),
                            strokeWidth = 1.dp.toPx()
                        )

                        // Calculate points for the 5 bands
                        val points = mutableListOf<Offset>()
                        val stepX = w / 4f

                        for (i in 0..4) {
                            val gainSteps = uiState.eqGains.getOrElse(i) { 0 }
                            // 24 steps = +12 dB
                            val gainRatio = gainSteps / 24f
                            val y = midY - (gainRatio * (h * 0.42f))
                            val x = i * stepX
                            points.add(Offset(x, y))
                        }

                        // Draw smooth curve through points
                        val path = Path()
                        val fillPath = Path()
                        if (points.isNotEmpty()) {
                            path.moveTo(points[0].x, points[0].y)
                            fillPath.moveTo(points[0].x, midY)
                            fillPath.lineTo(points[0].x, points[0].y)

                            for (i in 0 until points.size - 1) {
                                val p0 = points[i]
                                val p1 = points[i + 1]
                                val cx = (p0.x + p1.x) / 2f
                                path.cubicTo(cx, p0.y, cx, p1.y, p1.x, p1.y)
                                fillPath.cubicTo(cx, p0.y, cx, p1.y, p1.x, p1.y)
                            }
                            fillPath.lineTo(points.last().x, midY)
                            fillPath.close()

                            // Draw gradient fill under curve
                            drawPath(
                                path = fillPath,
                                brush = Brush.verticalGradient(
                                    colors = listOf(
                                        CyberPrimaryContainer.copy(alpha = 0.25f),
                                        Color.Transparent
                                    )
                                )
                            )

                            // Draw stroke
                            drawPath(
                                path = path,
                                color = CyberPrimaryContainer,
                                style = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round)
                            )

                            // Draw node dots
                            for (p in points) {
                                drawCircle(
                                    color = CyberBackground,
                                    radius = 5.dp.toPx(),
                                    center = p
                                )
                                drawCircle(
                                    color = CyberSecondaryFixed,
                                    radius = 3.dp.toPx(),
                                    center = p
                                )
                            }
                        }
                    }
                }
            }
        }

        // Hardware EQ Presets Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            val presets = listOf(
                "FLAT" to 0,
                "BASS" to 1,
                "VOCAL" to 2,
                "TREBLE" to 3
            )
            for ((name, id) in presets) {
                val isSelected = uiState.eqPreset == id
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (isSelected) CyberPrimaryContainer else CyberSurfaceContainer)
                        .clickable { onPresetSelect(id) }
                        .padding(vertical = 10.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = name,
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = if (isSelected) CyberSurfaceContainerLowest else MaterialTheme.colorScheme.onSurface,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            }
        }

        // 5 Parametric Sliders with dB Readouts
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(
                text = "AJUSTE FINO (-12 dB a +12 dB)",
                style = MaterialTheme.typography.labelSmall.copy(
                    fontFamily = JetBrainsMono,
                    color = CyberOutline,
                    letterSpacing = 1.sp
                )
            )

            bands.forEach { (freq, label, idx) ->
                val currentSteps = uiState.eqGains.getOrElse(idx) { 0 }
                val currentDb = currentSteps * 0.5f

                CyberCard {
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text(
                                    text = freq,
                                    style = MaterialTheme.typography.bodyLarge.copy(
                                        fontFamily = SpaceGrotesk,
                                        fontWeight = FontWeight.Bold,
                                        color = CyberPrimary
                                    )
                                )
                                Text(
                                    text = label,
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberOutline,
                                        fontSize = 10.sp
                                    )
                                )
                            }

                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(CyberSurfaceContainerHigh)
                                    .padding(horizontal = 8.dp, vertical = 4.dp)
                            ) {
                                Text(
                                    text = if (currentDb > 0) "+%.1f dB".format(currentDb) else "%.1f dB".format(currentDb),
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (currentDb > 0) CyberSecondaryFixed else if (currentDb < 0) CyberOutline else MaterialTheme.colorScheme.onSurface,
                                        fontWeight = FontWeight.Bold
                                    )
                                )
                            }
                        }

                        CyberSlider(
                            value = currentSteps.toFloat(),
                            onValueChange = { newVal ->
                                onBandGainChange(idx, newVal.toInt())
                            },
                            valueRange = -24f..24f,
                            modifier = Modifier.testTag("eq_band_slider_$idx")
                        )
                    }
                }
            }
        }
    }
}
