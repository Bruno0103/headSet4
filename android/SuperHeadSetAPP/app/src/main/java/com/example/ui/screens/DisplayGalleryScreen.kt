package com.example.ui.screens

import android.graphics.Bitmap
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AddPhotoAlternate
import androidx.compose.material.icons.filled.AllInclusive
import androidx.compose.material.icons.filled.Animation
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.GifBox
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.RotateRight
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import coil.compose.AsyncImage
import com.example.ble.ImageConverter
import com.example.model.BleConnectionStatus
import com.example.model.HeadSetUiState
import com.example.model.MediaSlot
import com.example.model.MediaUploadProgress
import com.example.ui.theme.CyberError
import com.example.ui.theme.CyberOutline
import com.example.ui.theme.CyberOutlineVariant
import com.example.ui.theme.CyberPrimary
import com.example.ui.theme.CyberPrimaryContainer
import com.example.ui.theme.CyberSecondaryFixed
import com.example.ui.theme.CyberSurfaceContainer
import com.example.ui.theme.CyberSurfaceContainerHigh
import com.example.ui.theme.CyberSurfaceContainerHighest
import com.example.ui.theme.CyberSurfaceContainerLow
import com.example.ui.theme.CyberSurfaceContainerLowest
import com.example.ui.theme.HankenGrotesk
import com.example.ui.theme.JetBrainsMono
import com.example.ui.theme.SpaceGrotesk

@Composable
fun DisplayGalleryScreen(
    uiState: HeadSetUiState,
    activeTab: String,
    onTabSwitch: (String) -> Unit,
    onSelectSlot: (String, Int) -> Unit,
    onDeleteSlot: (String, Int) -> Unit,
    onRefreshCatalog: () -> Unit,
    onUploadImage: (slot: Int, name: String, bitmap: Bitmap, setAsActive: Boolean) -> Unit,
    onUploadPreset: (presetName: String, slot: Int, setAsActive: Boolean) -> Unit,
    onCancelUpload: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var slotToDelete by remember { mutableStateOf<Pair<String, Int>?>(null) }
    var showUploadDialog by remember { mutableStateOf(false) }
    var uploadTargetSlot by remember { mutableIntStateOf(0) }

    // Photo picker for selecting device images without permissions
    var pickedBitmap by remember { mutableStateOf<Bitmap?>(null) }
    val photoPickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.PickVisualMedia()
    ) { uri: Uri? ->
        if (uri != null) {
            val bmp = ImageConverter.loadBitmapFromUri(context, uri)
            if (bmp != null) {
                pickedBitmap = bmp
                showUploadDialog = true
            }
        }
    }

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
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // Title & Header with Sync Button
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
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
                        imageVector = Icons.Default.GridView,
                        contentDescription = "Galeria",
                        tint = CyberPrimaryContainer,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = "Display 240×296 px",
                        style = MaterialTheme.typography.titleLarge.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    )
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    IconButton(
                        onClick = onRefreshCatalog,
                        modifier = Modifier
                            .size(32.dp)
                            .clip(CircleShape)
                            .background(CyberSurfaceContainerHigh)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = "Sincronizar com ESP32",
                            tint = CyberPrimary,
                            modifier = Modifier.size(16.dp)
                        )
                    }

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(9999.dp))
                            .background(CyberSurfaceContainerHigh)
                            .border(1.dp, CyberOutlineVariant.copy(alpha = 0.4f), RoundedCornerShape(9999.dp))
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = "RGB565 · 142 KB",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberPrimary,
                                fontWeight = FontWeight.Bold,
                                fontSize = 9.sp
                            )
                        )
                    }
                }
            }

            Text(
                text = "Transferência BLE direta para a memória Flash NVS e renderização na PSRAM de 8 MB.",
                style = MaterialTheme.typography.bodyMedium.copy(
                    fontFamily = HankenGrotesk,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            )
        }

        if (uiState.connectionStatus != BleConnectionStatus.CONNECTED) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainerLow)
                    .border(1.dp, CyberOutlineVariant.copy(alpha = 0.35f), RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.CloudUpload,
                    contentDescription = "Desconectado",
                    tint = CyberOutline,
                    modifier = Modifier.size(20.dp)
                )
                Column {
                    Text(
                        text = "MEMÓRIA OFFLINE · ESP32 DESCONECTADO",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = CyberOutline
                        )
                    )
                    Text(
                        text = "Conecte o HeadSet4 via BLE para consultar papéis de parede e GIFs gravados na Flash NVS.",
                        style = MaterialTheme.typography.bodySmall.copy(
                            fontFamily = HankenGrotesk,
                            color = CyberOutlineVariant
                        )
                    )
                }
            }
        }

        // GATT Toast Notification Bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(CyberSurfaceContainerLow)
                .border(1.dp, CyberOutlineVariant.copy(alpha = 0.3f), RoundedCornerShape(8.dp))
                .padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(CyberSecondaryFixed.copy(alpha = pulseAlpha))
                )
                Text(
                    text = uiState.lastGattMessage,
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberSecondaryFixed,
                        fontWeight = FontWeight.Medium
                    ),
                    maxLines = 1
                )
            }
            Text(
                text = "MTU ${uiState.negotiatedMtu}B",
                style = MaterialTheme.typography.labelSmall.copy(
                    fontFamily = JetBrainsMono,
                    color = CyberOutline,
                    fontSize = 10.sp
                )
            )
        }

        // Tab Selector (Imagens / GIFs)
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(12.dp))
                .background(CyberSurfaceContainerLowest)
                .border(1.dp, CyberOutlineVariant.copy(alpha = 0.3f), RoundedCornerShape(12.dp))
                .padding(4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            val filledImages = uiState.imageSlots.count { !it.isEmpty }
            val filledGifs = uiState.gifSlots.count { !it.isEmpty }

            // Tab Imagens
            Row(
                modifier = Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (activeTab == "images") CyberSurfaceContainerHigh else Color.Transparent)
                    .clickable { onTabSwitch("images") }
                    .padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = Icons.Default.Image,
                    contentDescription = "Imagens",
                    tint = if (activeTab == "images") CyberPrimary else CyberOutline,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.size(6.dp))
                Text(
                    text = "Wallpapers",
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.Bold,
                        color = if (activeTab == "images") CyberPrimary else CyberOutline
                    )
                )
                Spacer(modifier = Modifier.size(6.dp))
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .background(CyberSurfaceContainerLowest)
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = "$filledImages/6",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutlineVariant,
                            fontSize = 10.sp
                        )
                    )
                }
            }

            // Tab GIFs
            Row(
                modifier = Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (activeTab == "gifs") CyberSurfaceContainerHigh else Color.Transparent)
                    .clickable { onTabSwitch("gifs") }
                    .padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = Icons.Default.GifBox,
                    contentDescription = "GIFs",
                    tint = if (activeTab == "gifs") CyberPrimaryContainer else CyberOutline,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.size(6.dp))
                Text(
                    text = "GIFs Loop",
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.Bold,
                        color = if (activeTab == "gifs") CyberPrimaryContainer else CyberOutline
                    )
                )
                Spacer(modifier = Modifier.size(6.dp))
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .background(CyberSurfaceContainerLowest)
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = "$filledGifs/6",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberOutlineVariant,
                            fontSize = 10.sp
                        )
                    )
                }
            }
        }

        // Action Buttons: Transfer / Presets
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Button(
                onClick = {
                    val target = uiState.imageSlots.firstOrNull { it.isEmpty }?.id ?: 0
                    uploadTargetSlot = target
                    photoPickerLauncher.launch(
                        PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)
                    )
                },
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyberPrimaryContainer,
                    contentColor = Color(0xFF00363D)
                ),
                shape = RoundedCornerShape(10.dp),
                modifier = Modifier
                    .weight(1f)
                    .height(44.dp)
                    .testTag("btn_upload_photo")
            ) {
                Icon(
                    imageVector = Icons.Default.AddPhotoAlternate,
                    contentDescription = "Galeria",
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.size(6.dp))
                Text(
                    text = "Foto da Galeria",
                    style = MaterialTheme.typography.labelMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.Bold
                    )
                )
            }

            Button(
                onClick = {
                    val target = uiState.imageSlots.firstOrNull { it.isEmpty }?.id ?: 0
                    uploadTargetSlot = target
                    pickedBitmap = ImageConverter.generatePresetBitmap("Cyber Grid")
                    showUploadDialog = true
                },
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyberSurfaceContainerHigh,
                    contentColor = CyberPrimary
                ),
                shape = RoundedCornerShape(10.dp),
                modifier = Modifier
                    .weight(1f)
                    .height(44.dp)
                    .border(1.dp, CyberPrimary.copy(alpha = 0.4f), RoundedCornerShape(10.dp))
                    .testTag("btn_upload_preset")
            ) {
                Icon(
                    imageVector = Icons.Default.CloudUpload,
                    contentDescription = "Transferência BLE",
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.size(6.dp))
                Text(
                    text = "Transferência BLE",
                    style = MaterialTheme.typography.labelMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.Bold
                    )
                )
            }
        }

        // 2x3 Grid of 240x296 Media Slots
        val itemsList = if (activeTab == "images") uiState.imageSlots else uiState.gifSlots
        val activeSlotId = if (activeTab == "images") uiState.activeWallpaperIdx else uiState.activeGifIdx

        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(itemsList) { slot ->
                MediaSlotCard(
                    slot = slot,
                    isGif = activeTab == "gifs",
                    isActive = !slot.isEmpty && slot.id == activeSlotId,
                    onSelect = { onSelectSlot(activeTab, slot.id) },
                    onDelete = { slotToDelete = activeTab to slot.id },
                    onFill = {
                        uploadTargetSlot = slot.id
                        pickedBitmap = ImageConverter.generatePresetBitmap("Obsidian Core")
                        showUploadDialog = true
                    }
                )
            }
        }
    }

    // Modal Dialog: Media Transfer Preparation
    if (showUploadDialog) {
        MediaTransferModal(
            initialBitmap = pickedBitmap,
            initialSlot = uploadTargetSlot,
            onDismiss = {
                showUploadDialog = false
                pickedBitmap = null
            },
            onPickNewImage = {
                photoPickerLauncher.launch(
                    PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)
                )
            },
            onSelectPreset = { presetName ->
                pickedBitmap = ImageConverter.generatePresetBitmap(presetName)
            },
            onStartTransfer = { slot, name, bitmap, setAsActive ->
                showUploadDialog = false
                pickedBitmap = null
                onUploadImage(slot, name, bitmap, setAsActive)
            }
        )
    }

    // Live Upload Progress Modal (Cyberpunk HUD Overlay)
    uiState.uploadProgress?.let { progress ->
        MediaUploadHudModal(
            progress = progress,
            onCancel = onCancelUpload
        )
    }

    // Confirmation Dialog for Slot Deletion
    slotToDelete?.let { (tab, id) ->
        AlertDialog(
            onDismissRequest = { slotToDelete = null },
            title = {
                Text(
                    text = "Remover Mídia da Flash NVS",
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontFamily = SpaceGrotesk,
                        fontWeight = FontWeight.Bold
                    )
                )
            },
            text = {
                Text(
                    text = "Deseja limpar o Slot #$id? O comando delete_media_slot será enviado ao ESP32 e o slot voltará a ficar disponível.",
                    style = MaterialTheme.typography.bodyMedium.copy(fontFamily = HankenGrotesk)
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        onDeleteSlot(tab, id)
                        slotToDelete = null
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyberError,
                        contentColor = Color.Black
                    )
                ) {
                    Text("Excluir", fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = {
                TextButton(onClick = { slotToDelete = null }) {
                    Text("Cancelar", color = CyberOutline)
                }
            },
            containerColor = CyberSurfaceContainerHigh
        )
    }
}

/**
 * Modal dialog for configuring the media transfer (240x296, RGB565 Little-Endian, CRC32).
 */
@Composable
fun MediaTransferModal(
    initialBitmap: Bitmap?,
    initialSlot: Int,
    onDismiss: () -> Unit,
    onPickNewImage: () -> Unit,
    onSelectPreset: (String) -> Unit,
    onStartTransfer: (slot: Int, name: String, bitmap: Bitmap, setAsActive: Boolean) -> Unit
) {
    var selectedSlot by remember { mutableIntStateOf(initialSlot) }
    var wallpaperName by remember { mutableStateOf("Neon Cyber") }
    var setAsActive by remember { mutableStateOf(true) }
    var currentBitmap by remember { mutableStateOf(initialBitmap) }

    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(CyberSurfaceContainerLow)
                .border(1.dp, CyberPrimaryContainer.copy(alpha = 0.5f), RoundedCornerShape(16.dp))
                .padding(16.dp)
        ) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.FileUpload,
                            contentDescription = "Transferência",
                            tint = CyberPrimaryContainer,
                            modifier = Modifier.size(20.dp)
                        )
                        Text(
                            text = "Transferência de Mídia BLE",
                            style = MaterialTheme.typography.titleMedium.copy(
                                fontFamily = SpaceGrotesk,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                    IconButton(onClick = onDismiss, modifier = Modifier.size(28.dp)) {
                        Icon(imageVector = Icons.Default.Close, contentDescription = "Fechar", tint = CyberOutline)
                    }
                }

                // Preset selector chips
                Text(
                    text = "PRESETS CYBERPUNK 240×296",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = JetBrainsMono,
                        color = CyberPrimary,
                        fontWeight = FontWeight.Bold
                    )
                )

                LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    val presets = listOf("Cyber Grid", "Obsidian Core", "Neon Matrix", "Quantum Skull")
                    items(presets) { preset ->
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(CyberSurfaceContainerHigh)
                                .border(1.dp, CyberOutlineVariant.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
                                .clickable {
                                    wallpaperName = preset
                                    onSelectPreset(preset)
                                }
                                .padding(horizontal = 10.dp, vertical = 6.dp)
                        ) {
                            Text(
                                text = preset,
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            )
                        }
                    }
                }

                // Preview & Target Specs Row
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    // Exact 240x296 Preview Box
                    Box(
                        modifier = Modifier
                            .width(110.dp)
                            .aspectRatio(240f / 296f)
                            .clip(RoundedCornerShape(8.dp))
                            .background(Color.Black)
                            .border(1.dp, CyberSecondaryFixed.copy(alpha = 0.6f), RoundedCornerShape(8.dp)),
                        contentAlignment = Alignment.Center
                    ) {
                        if (currentBitmap != null) {
                            Image(
                                bitmap = currentBitmap!!.asImageBitmap(),
                                contentDescription = "Preview",
                                contentScale = ContentScale.Crop,
                                modifier = Modifier.fillMaxSize()
                            )
                        } else {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Icon(
                                    imageVector = Icons.Default.Image,
                                    contentDescription = "Vazio",
                                    tint = CyberOutline,
                                    modifier = Modifier.size(24.dp)
                                )
                                Text(
                                    text = "240×296",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberOutline,
                                        fontSize = 9.sp
                                    )
                                )
                            }
                        }

                        // Badge 240x296
                        Box(
                            modifier = Modifier
                                .align(Alignment.BottomEnd)
                                .padding(4.dp)
                                .clip(RoundedCornerShape(3.dp))
                                .background(Color.Black.copy(alpha = 0.75f))
                                .padding(horizontal = 4.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = "240×296",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberSecondaryFixed,
                                    fontSize = 8.sp,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }

                    // Tech details & Change button
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Text(
                            text = "FORMATO DE HARDWARE",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutlineVariant,
                                fontSize = 9.sp
                            )
                        )
                        Text(
                            text = "• 240 × 296 Little-Endian\n• RGB565 (16 bits/pixel)\n• 142.080 Bytes exatos\n• Checksum CRC32 NVS",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                fontSize = 10.sp,
                                lineHeight = 15.sp
                            )
                        )

                        Spacer(modifier = Modifier.height(2.dp))
                        Button(
                            onClick = onPickNewImage,
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CyberSurfaceContainerHigh,
                                contentColor = CyberPrimary
                            ),
                            shape = RoundedCornerShape(8.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(32.dp)
                        ) {
                            Icon(imageVector = Icons.Default.AddPhotoAlternate, contentDescription = "Trocar", modifier = Modifier.size(14.dp))
                            Spacer(modifier = Modifier.size(4.dp))
                            Text("Abrir Galeria", fontSize = 11.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }

                // Slot Selector (#0..#5)
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = "SLOT DE DESTINO NA FLASH NVS",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberPrimary,
                            fontWeight = FontWeight.Bold
                        )
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        (0..5).forEach { slotIdx ->
                            val isChosen = selectedSlot == slotIdx
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isChosen) CyberPrimaryContainer else CyberSurfaceContainerHigh)
                                    .clickable { selectedSlot = slotIdx }
                                    .padding(vertical = 6.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = "#$slotIdx",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = if (isChosen) Color(0xFF00363D) else CyberOutline,
                                        fontWeight = FontWeight.Bold
                                    )
                                )
                            }
                        }
                    }
                }

                // Name field
                OutlinedTextField(
                    value = wallpaperName,
                    onValueChange = { if (it.length <= 24) wallpaperName = it },
                    label = { Text("Nome na Galeria (máx. 24 carac.)", fontSize = 11.sp) },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = CyberPrimaryContainer,
                        unfocusedBorderColor = CyberOutlineVariant.copy(alpha = 0.5f),
                        focusedLabelColor = CyberPrimaryContainer,
                        cursorColor = CyberPrimaryContainer
                    ),
                    modifier = Modifier.fillMaxWidth()
                )

                // Checkbox: set as active wallpaper immediately
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Checkbox(
                        checked = setAsActive,
                        onCheckedChange = { setAsActive = it },
                        colors = CheckboxDefaults.colors(checkedColor = CyberPrimaryContainer)
                    )
                    Spacer(modifier = Modifier.size(4.dp))
                    Text(
                        text = "Definir como Papel de Parede Ativo após envio",
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = HankenGrotesk)
                    )
                }

                // Submit Transfer Button
                Button(
                    onClick = {
                        val bmp = currentBitmap ?: ImageConverter.generatePresetBitmap(wallpaperName)
                        onStartTransfer(selectedSlot, wallpaperName, bmp, setAsActive)
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyberSecondaryFixed,
                        contentColor = Color.Black
                    ),
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(44.dp)
                        .testTag("btn_submit_media_transfer")
                ) {
                    Icon(imageVector = Icons.Default.CloudUpload, contentDescription = "Enviar", modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.size(6.dp))
                    Text(
                        text = "Transferir para o Fone (142 KB BLE)",
                        style = MaterialTheme.typography.bodyMedium.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            }
        }
    }
}

/**
 * Real-time Cyberpunk HUD Progress Dialog for BLE media transmission.
 */
@Composable
fun MediaUploadHudModal(
    progress: MediaUploadProgress,
    onCancel: () -> Unit
) {
    Dialog(onDismissRequest = { /* Modal during transfer */ }) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(18.dp))
                .background(CyberSurfaceContainerLowest)
                .border(2.dp, CyberPrimaryContainer, RoundedCornerShape(18.dp))
                .padding(20.dp)
        ) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(14.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // HUD Title & Icon
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        imageVector = if (progress.isComplete) Icons.Default.CheckCircle else Icons.Default.FileUpload,
                        contentDescription = "Status",
                        tint = if (progress.isComplete) CyberSecondaryFixed else CyberPrimaryContainer,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = if (progress.isComplete) "TRANSFERÊNCIA CONCLUÍDA" else "ENVIANDO MÍDIA BLE",
                        style = MaterialTheme.typography.titleMedium.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.Bold,
                            color = if (progress.isComplete) CyberSecondaryFixed else CyberPrimaryContainer
                        )
                    )
                }

                // Thumbnail & Percentage
                Box(
                    modifier = Modifier
                        .size(width = 96.dp, height = 118.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color.Black)
                        .border(1.dp, CyberOutlineVariant, RoundedCornerShape(8.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    if (progress.previewBitmap != null) {
                        Image(
                            bitmap = progress.previewBitmap.asImageBitmap(),
                            contentDescription = "Preview",
                            contentScale = ContentScale.Crop,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(Color.Black.copy(alpha = 0.35f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "${(progress.progress * 100).toInt()}%",
                            style = MaterialTheme.typography.titleLarge.copy(
                                fontFamily = JetBrainsMono,
                                color = Color.White,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                }

                // Animated Progress Bar
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    LinearProgressIndicator(
                        progress = { progress.progress },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(RoundedCornerShape(4.dp)),
                        color = CyberPrimaryContainer,
                        trackColor = CyberSurfaceContainerHigh
                    )
                    Text(
                        text = progress.statusText,
                        style = MaterialTheme.typography.bodySmall.copy(
                            fontFamily = JetBrainsMono,
                            color = CyberSecondaryFixed,
                            fontSize = 11.sp
                        )
                    )
                }

                // Hardware telemetry HUD grid
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(CyberSurfaceContainerLow)
                        .padding(10.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    HudRow(label = "Slot de Destino:", value = "Slot #${progress.slot} (${progress.name})")
                    HudRow(label = "Bytes Transmitidos:", value = "${progress.bytesSent} / ${progress.totalBytes} B")
                    HudRow(label = "Sequência de Chunks:", value = "Seq #${progress.currentChunk} / ${progress.totalChunks}")
                    HudRow(label = "Taxa de Throughput:", value = "${String.format("%.1f", progress.speedKbps)} KB/s")
                    if (progress.crc32.isNotEmpty()) {
                        HudRow(label = "Checksum CRC32:", value = "0x${progress.crc32}")
                    }
                }

                // Cancel button (or Finished)
                if (!progress.isComplete && !progress.isError) {
                    Button(
                        onClick = onCancel,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberSurfaceContainerHigh,
                            contentColor = CyberOutline
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("Cancelar Transferência")
                    }
                }
            }
        }
    }
}

@Composable
private fun HudRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                fontFamily = JetBrainsMono,
                color = CyberOutline,
                fontSize = 10.sp
            )
        )
        Text(
            text = value,
            style = MaterialTheme.typography.labelSmall.copy(
                fontFamily = JetBrainsMono,
                color = MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp
            )
        )
    }
}

@Composable
private fun MediaSlotCard(
    slot: MediaSlot,
    isGif: Boolean,
    isActive: Boolean,
    onSelect: () -> Unit,
    onDelete: () -> Unit,
    onFill: () -> Unit
) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(CyberSurfaceContainerLow)
            .border(
                width = if (isActive) 2.dp else 1.dp,
                color = if (isActive) CyberSecondaryFixed else CyberOutlineVariant.copy(alpha = 0.3f),
                shape = RoundedCornerShape(12.dp)
            )
            .padding(10.dp)
    ) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Preview Container (240x296 aspect ratio)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(240f / 296f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(CyberSurfaceContainerLowest),
                contentAlignment = Alignment.Center
            ) {
                if (slot.isEmpty) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Icon(
                            imageVector = if (isGif) Icons.Default.GifBox else Icons.Default.AddPhotoAlternate,
                            contentDescription = "Slot Livre",
                            tint = CyberOutline,
                            modifier = Modifier.size(32.dp)
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = "SLOT LIVRE",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline,
                                fontSize = 10.sp
                            )
                        )
                    }
                    Box(
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(6.dp)
                            .clip(RoundedCornerShape(4.dp))
                            .background(CyberSurfaceContainerLow.copy(alpha = 0.8f))
                            .padding(horizontal = 6.dp, vertical = 2.dp)
                    ) {
                        Text(
                            text = "#${slot.id} · VAZIO",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = CyberOutline,
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                } else {
                    // Content Render based on slot content / preview bitmap
                    when {
                        slot.previewBitmap != null -> {
                            Image(
                                bitmap = slot.previewBitmap.asImageBitmap(),
                                contentDescription = slot.title,
                                contentScale = ContentScale.Crop,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        slot.imgUrl != null -> {
                            AsyncImage(
                                model = slot.imgUrl,
                                contentDescription = slot.title,
                                contentScale = ContentScale.Crop,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        slot.type == "matrix" || slot.type == "matrix_anim" -> {
                            Column(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .background(Color(0xFF0C1815)),
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center
                            ) {
                                Text(
                                    text = "1011001",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberSecondaryFixed,
                                        fontSize = 11.sp
                                    )
                                )
                                Text(
                                    text = "0100110",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberSecondaryFixed.copy(alpha = 0.4f),
                                        fontSize = 11.sp
                                    )
                                )
                                Text(
                                    text = "0010110",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberSecondaryFixed.copy(alpha = 0.9f),
                                        fontSize = 11.sp
                                    )
                                )
                                Text(
                                    text = "1101001",
                                    style = MaterialTheme.typography.labelSmall.copy(
                                        fontFamily = JetBrainsMono,
                                        color = CyberSecondaryFixed.copy(alpha = 0.6f),
                                        fontSize = 11.sp
                                    )
                                )
                            }
                        }
                        slot.type == "pulse" -> {
                            Box(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .background(Color(0xFF12182B)),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    imageVector = Icons.Default.AllInclusive,
                                    contentDescription = "Pulse",
                                    tint = CyberPrimaryContainer,
                                    modifier = Modifier.size(28.dp)
                                )
                            }
                        }
                        slot.type == "radar" -> {
                            Box(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .background(Color(0xFF0D1B1C)),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    imageVector = Icons.Default.RotateRight,
                                    contentDescription = "Radar",
                                    tint = CyberPrimaryContainer,
                                    modifier = Modifier.size(32.dp)
                                )
                            }
                        }
                        else -> {
                            Box(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .background(Color(0xFF14232C)),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Animation,
                                    contentDescription = "Wave",
                                    tint = CyberPrimary,
                                    modifier = Modifier.size(32.dp)
                                )
                            }
                        }
                    }

                    // Top Left Tag: ATIVO or IMG/GIF
                    if (isActive) {
                        Row(
                            modifier = Modifier
                                .align(Alignment.TopStart)
                                .padding(6.dp)
                                .clip(RoundedCornerShape(9999.dp))
                                .background(CyberSecondaryFixed)
                                .padding(horizontal = 8.dp, vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(5.dp)
                                    .clip(CircleShape)
                                    .background(Color.Black)
                            )
                            Text(
                                text = "NO VISOR",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = Color.Black,
                                    fontWeight = FontWeight.Bold,
                                    fontSize = 9.sp
                                )
                            )
                        }
                    } else {
                        Row(
                            modifier = Modifier
                                .align(Alignment.TopStart)
                                .padding(6.dp)
                                .clip(RoundedCornerShape(9999.dp))
                                .background(CyberSurfaceContainerLowest.copy(alpha = 0.85f))
                                .border(1.dp, CyberPrimary.copy(alpha = 0.25f), RoundedCornerShape(9999.dp))
                                .padding(horizontal = 6.dp, vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(3.dp)
                        ) {
                            Icon(
                                imageVector = if (isGif) Icons.Default.Animation else Icons.Default.Image,
                                contentDescription = "Type",
                                tint = CyberPrimary,
                                modifier = Modifier.size(10.dp)
                            )
                            Text(
                                text = if (isGif) "GIF" else "RGB565",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    fontFamily = JetBrainsMono,
                                    color = CyberPrimary,
                                    fontSize = 9.sp,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                    }

                    // Bottom Right Slot Index
                    Box(
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(6.dp)
                            .clip(RoundedCornerShape(4.dp))
                            .background(CyberSurfaceContainerLowest.copy(alpha = 0.85f))
                            .padding(horizontal = 6.dp, vertical = 2.dp)
                    ) {
                        Text(
                            text = "#${slot.id} · ${if (isGif) "GIF" else "IMG"}",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }
                }
            }

            // Title & Delete row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = slot.title,
                        style = MaterialTheme.typography.bodyMedium.copy(
                            fontFamily = SpaceGrotesk,
                            fontWeight = FontWeight.SemiBold,
                            color = if (slot.isEmpty) CyberOutline else MaterialTheme.colorScheme.onSurface
                        ),
                        maxLines = 1
                    )
                    Text(
                        text = if (isActive) "Ativo no HeadSet" else slot.subtitle,
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            color = if (isActive) CyberSecondaryFixed else CyberOutline,
                            fontSize = 10.sp,
                            fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal
                        ),
                        maxLines = 1
                    )
                }

                if (!slot.isEmpty) {
                    IconButton(
                        onClick = onDelete,
                        modifier = Modifier.size(28.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Delete,
                            contentDescription = "Excluir Mídia",
                            tint = CyberOutline,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }

            // Action Buttons
            if (slot.isEmpty) {
                Button(
                    onClick = onFill,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyberSurfaceContainerHigh,
                        contentColor = CyberPrimary
                    ),
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Add,
                        contentDescription = "Transferir",
                        modifier = Modifier.size(14.dp)
                    )
                    Spacer(modifier = Modifier.size(4.dp))
                    Text(
                        text = "Transferir Mídia",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            } else if (isActive) {
                Button(
                    onClick = {},
                    enabled = false,
                    colors = ButtonDefaults.buttonColors(
                        disabledContainerColor = CyberSurfaceContainerHigh,
                        disabledContentColor = CyberSecondaryFixed
                    ),
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.CheckCircle,
                        contentDescription = "No Visor",
                        modifier = Modifier.size(14.dp)
                    )
                    Spacer(modifier = Modifier.size(4.dp))
                    Text(
                        text = "Exibindo no Visor",
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold
                        )
                    )
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Button(
                        onClick = onSelect,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyberPrimaryContainer,
                            contentColor = Color(0xFF00363D)
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .weight(1f)
                            .height(34.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Visibility,
                            contentDescription = "Escolher",
                            modifier = Modifier.size(14.dp)
                        )
                        Spacer(modifier = Modifier.size(4.dp))
                        Text(
                            text = "Exibir",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold
                            )
                        )
                    }

                    IconButton(
                        onClick = onFill,
                        modifier = Modifier
                            .size(34.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(CyberSurfaceContainerHigh)
                    ) {
                        Icon(
                            imageVector = Icons.Default.FileUpload,
                            contentDescription = "Substituir",
                            tint = CyberPrimary,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }
        }
    }
}
