# Plan (Revised): Pure Bitmap Display Synchronization & Instant BLE Transmission

A streamlined, unified media synchronization system between the **Dashboard** and **Display Gallery**, delivering pure 240×296 bitmap rendering with zero overlays, automatic slot naming (`Foto X` / `GIF X`), and immediate BLE transmission upon media selection.

---

## User Review & Addressed Feedback

> [!IMPORTANT]
> The implementation plan has been revised according to your exact specifications:

1. **Pure Bitmap Visual Rendering (No HUD Overlay)**:
   - *User Feedback*: "Quero que apenas converta para bit map, não coloque nada por cima da imagem. Quero a imagem e gif puro em bit map renderizada visualmente."
   - *Resolution*: The 240×296 screen preview will render the **pure visual bitmap** (clean image or animation) without any HUD telemetry overlay, labels, or icons covering the artwork.
2. **Standardized Automatic Naming (`Foto X` / `GIF X`)**:
   - *User Feedback*: "Sempre eu carregar uma foto de minha galeria, ela deve ser renomeada como foto X ou gif X, onde X é o número na lista."
   - *Resolution*: Any image or GIF loaded from the device gallery via the Photo Picker will automatically be named **`Foto X`** (for photo slots) or **`GIF X`** (for GIF slots), where `X` is the slot index (0 to 5).
3. **Instant BLE API Transmission on Selection**:
   - *User Feedback*: "Só de escolher a tela principal, ela já deve ser enviada de acordo com os protocolos da api para o visor."
   - *Resolution*: Selecting a photo or GIF (either directly from the Dashboard cards or by pressing "Escolher" in the Gallery) will **instantly transmit** the corresponding BLE command (`{"cmd":"set_wallpaper_idx","val":X}` or `{"cmd":"set_gif_idx","val":X}`) to the physical ESP32 headset.

---

## 1. Overview & Core Architecture

- **Dashboard Integrated Display Card**:
  - Displays the active media in the 240×296 physical aspect ratio IPS screen container.
  - Two prominent interactive selector cards below the screen:
    - **Slot Foto Ativa** (`Foto X: <Título>` · Status: Ativo / Escolher)
    - **Slot GIF Ativo** (`GIF Y: <Título>` · Status: Ativo / Escolher)
  - Tapping either card immediately renders that media pure on the 240×296 screen **and immediately sends the BLE transmission** to the headset.
- **Bi-directional State Synchronization**:
  - The Dashboard directly consumes the `HeadSetUiState` (observing `activeWallpaperIdx`, `activeGifIdx`, `imageSlots`, and `gifSlots`).
  - When the user selects a slot, adds an image, or deletes a media in the Display tab, the Dashboard updates instantly in real time.
  - When the user taps a slot card on the Dashboard, it updates the active index, sends the BLE GATT command, and reflects on both screens.

---

## 2. User Experience & Visual Design

```
┌────────────────────────────────────────────────────────────┐
│ DISPLAY INTEGRADO                          Slot Ativo: #0  │
│ ┌────────────────────────────────────────────────────────┐ │
│ │ ┌─────────────── 240×296 PURE BITMAP ────────────┐     │ │
│ │ │                                                │     │ │
│ │ │                                                │     │ │
│ │ │       [ PURE 240×296 VISUAL BITMAP RENDER ]    │     │ │
│ │ │          (Zero text or icons on top)           │     │ │
│ │ │                                                │     │ │
│ │ └────────────────────────────────────────────────┘     │ │
│ │                  240×296 PX IPS SCREEN                 │ │
│ └────────────────────────────────────────────────────────┘ │
│                                                            │
│ ┌───────────────────────────┐  ┌─────────────────────────┐ │
│ │ ● FOTO ATIVA (#0)         │  │   GIF ATIVO (#1)        │ │
│ │   Foto 0 · Em Exibição    │  │   GIF 1 · Alternar      │ │
│ └───────────────────────────┘  └─────────────────────────┘ │
│                                                            │
│ ┌────────────────────────────────────────────────────────┐ │
│ │ 🗂 Biblioteca de Mídias                5 Fotos · 5 GIFs >│
│ └────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────┘
```

- **Clean Screen Frame**: Sleek dark bezel with precise 240:296 aspect ratio, displaying the raw visual content without any text or indicators drawn over the image.
- **Active Slot Selector Cards**:
  - **Left Card**: Shows the active photo (`Foto X`), slot index `#X`, and active status indicator.
  - **Right Card**: Shows the active GIF (`GIF Y`), slot index `#Y`, and active status indicator.
  - Tapping switches preview and immediately dispatches the BLE command.
- **Photo Picker Naming**:
  - Picking an image into Slot #3 automatically labels it **`Foto 3`** (or **`GIF 3`**).
  - Clean subtitle `Estático Flash` or `Animação Loop`.

---

## 3. Technical Changes to Implement

1. **`app/src/main/java/com/example/ui/screens/DashboardScreen.kt`**:
   - Remove the HUD overlay text from inside the 240×296 container, rendering pure bitmap content (`AsyncImage` or pure Canvas animation).
   - Add toggle state between `PHOTO` and `GIF`.
   - Implement the dual selector cards for the active Photo and active GIF.
   - Wire slot selection to instant BLE transmission callbacks (`onSelectWallpaper` and `onSelectGif`).
2. **`app/src/main/java/com/example/viewmodel/HeadSetViewModel.kt` & `HeadSetBleManager.kt`**:
   - Update `addCustomMediaSlot` / `updateImageSlot` / `updateGifSlot` so that whenever media is picked, its title is strictly set to `"Foto $slotId"` or `"GIF $slotId"`.
   - Ensure `onSelectWallpaper` immediately transmits `set_wallpaper_idx` and `onSelectGif` immediately transmits `set_gif_idx`.
3. **`app/src/main/java/com/example/MainActivity.kt`**:
   - Connect `onSelectGif` from `DashboardScreen` to `viewModel.bleManager.setGifIdx`.

---

## 4. Verification

- Build applet via `compile_applet`.
- Verify clean 240×296 bitmap without any HUD text over the image.
- Verify photo picker loads images with the exact `"Foto X"` / `"GIF X"` naming format.
- Verify selecting a slot on either screen immediately reflects on the Dashboard and sends the BLE command.
