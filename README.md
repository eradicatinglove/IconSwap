
![icon](https://github.com/user-attachments/assets/9d259c73-746b-446f-baaf-19b4a6eeecb6)


![2026030721371800-68C370F3B4A0DB855DFC57E1427942CF](https://github.com/user-attachments/assets/45e1b8ad-e857-4d97-bcfd-bd7f89019532)

![2026030721220200-68C370F3B4A0DB855DFC57E1427942CF](https://github.com/user-attachments/assets/bee16ec0-1839-4049-83a9-03da2239360c)

![2026030714393200-68C370F3B4A0DB855DFC57E1427942CF](https://github.com/user-attachments/assets/050b5c71-381a-4e48-819f-effff17462a4)


# IconSwap
**Nintendo Switch Homebrew — Custom Game Icon Manager**
*by eradicatinglove*

---

## What is IconSwap?

IconSwap is a Nintendo Switch homebrew NRO that lets you assign custom icons to any installed game, directly from your Switch — no PC required. It works alongside **sys-icon** to replace icons visible in the HOME menu after a reboot.

---

## Requirements

- Nintendo Switch running **Atmosphere CFW**
- **sys-icon** installed and enabled
- Wi-Fi connection (for FTP image upload)
- Images stored on your SD card under `sdmc:/icon-manager/`

---

## Installation

1. Copy `IconSwap.nro` to your SD card (e.g. `sdmc:/switch/IconSwap.nro`)
2. Make sure **sys-ticon** is installed
3. Launch IconSwap from the Homebrew Menu

---

## How to Use

### Step 1 — Select a Game
- Use **Up / Down** to move one game at a time
- Use **Left / Right** to jump 10 games at a time
- Games with a custom icon already assigned are marked with `[*]`
- Press **A** to open the image browser for the selected game

### Step 2 — Choose Your Scale Mode
Before selecting an image, press **Y** to toggle between the two scale modes:

| Mode | Best for |
|------|----------|
| **CROP** | Square or landscape images with a standard square icon theme |
| **SQUEEZE** | 2:3 ratio images (e.g. 600×900) with a vertical icon theme |

> A hint is displayed on screen explaining each mode as you toggle.

### Step 3 — Select an Image
- Browse your SD card folders with **Up / Down** and **A**
- Supported formats: **JPG, PNG, BMP, TGA** (UTF-8 filenames supported)
- Press **A** on an image to proceed to the confirm screen

### Step 4 — Confirm and Apply
- The confirm screen shows the game, image path, and active scale mode
- Press **A** to apply — IconSwap will generate:
  - `icon.jpg` (256×256)
  - `icon174.jpg` (174×174)
- Press **B** to go back and choose a different image

### Step 5 — Reboot
Reboot your Switch to see the new icon appear on the HOME menu.

---

## Scale Modes Explained

### CROP (default)
Takes the largest centred square from your source image and scales it to 256×256. Nothing outside the centre square is kept.

- ✅ Works with any image size or ratio
- ✅ Ideal for square icon themes (standard HOME menu layout)
- ⚠️ Will cut the top and bottom of tall vertical images

### SQUEEZE
Scales the entire image — width and height — directly into 256×256 with no cropping and no padding. The image will look squished as a raw file, but a **vertical icon theme** stretches it back to 2:3 on screen, restoring the original composition perfectly.

- ✅ Ideal for 2:3 source images (e.g. 600×900 cover art)
- ✅ Designed for use with vertical icon themes (e.g. SwitchDeck, Lava Lamp)
- ⚠️ Will look distorted on a standard square theme
- ⚠️ Not recommended for square or landscape source images

> **Tip:** For vertical themes, always use 2:3 source images (600×900 is the standard). SQUEEZE + a vertical theme = perfect result.

---

## Uploading Images via FTP

1. On the Game List screen, press **X** to open the FTP menu
2. Connect your FTP client to the displayed IP address on port **5000**
3. Upload images to `sdmc:/icon-manager/`
4. Press **B** to return to the game list and browse your uploaded images

FTP accepts any username and password.

---

## Removing Icons

On the Game List screen, select a game and press **Y** to remove its custom icons. The game will revert to its original icon after a reboot.

---

## Other Controls

| Screen | Button | Action |
|--------|--------|--------|
| Game List | **A** | Open image browser |
| Game List | **Y** | Remove assigned icon |
| Game List | **X** | Open FTP menu |
| Game List | **−** | Reboot to payload |
| Game List | **+** | Exit IconSwap |
| File Browser | **A** | Open folder / select image |
| File Browser | **B** | Back to game list |
| File Browser | **Y** | Toggle CROP / SQUEEZE mode |
| Confirm | **A** | Apply icon |
| Confirm | **B** | Cancel |
| Result | **A** | Back to game list |
| Result | **−** | Reboot to payload |

---

## Icon File Details

IconSwap writes two files per game into `sdmc:/atmosphere/contents/<TitleID>/`:

| File | Size | Size limit |
|------|------|------------|
| `icon.jpg` | 256×256 | 100 KB |
| `icon174.jpg` | 174×174 | 64 KB |

If a generated file exceeds the size limit, IconSwap automatically retries at a lower JPEG quality to stay within bounds.

---

## Troubleshooting

**Icon didn't change after reboot**
- Make sure sys-ticon is correctly installed and enabled
- Confirm the files were written to the correct TitleID folder (marked with `[*]` in IconSwap)

**Question mark appears instead of icon**
- The JPEG may be too large or in an unsupported format
- Try a smaller or simpler source image

**FTP won't start**
- Make sure Wi-Fi is connected before launching IconSwap
- Check the FTP status message on screen for details

**Vertical icon looks squished without a theme**
- This is expected — SQUEEZE mode intentionally squishes the image so the vertical theme can stretch it back correctly on screen

---

## Credits

- **eradicatinglove** — IconSwap development
- **masagrator** — sys-ticon sysmodule
- **nothings/stb** — stb_image, stb_image_resize2, stb_image_write
