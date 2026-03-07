
![icon](https://github.com/user-attachments/assets/2bea60cd-29d8-4864-8e1d-db7cdcf4aa26)


# IconSwap

A Nintendo Switch homebrew NRO companion tool for [sys-icon](https://github.com/eradicatinglove/sys-icon).

Assign custom icons to your installed games directly on your Switch — no PC needed. Browse your SD card for images, pick a game, and IconSwap handles all cropping, resizing, and file creation automatically.

---

## Features

- Lists all installed games with Title IDs
- Browse any folder on the SD card for images
- Supports **JPG, PNG, BMP, TGA** input — any format, any resolution
- Auto **center-crops** non-square images — no stretching
- Auto-resizes and converts to both required sizes:
  - `icon.jpg` — 256x256 JPEG (HOME menu)
  - `icon174.jpg` — 174x174 JPEG (All Software / Options view)
- Creates `atmosphere/contents/<titleid>/` folder automatically if it doesn't exist
- Shows `[*]` next to games that already have icons assigned
- Works alongside sys-icon sysmodule

---

## How It Works

IconSwap creates the icon files that sys-icon reads at runtime. After applying an icon and rebooting, sys-icon intercepts the icon request and serves your custom image instead of the original.

```
Your image (any size/format)
        ↓
  IconSwap on Switch
        ↓
  Center crop to square
        ↓
  Resize to 256x256 → icon.jpg
  Resize to 174x174 → icon174.jpg
        ↓
  Saved to atmosphere/contents/<titleid>/
        ↓
  sys-icon serves them at runtime
```

---

## Controls

| Button | Action |
|---|---|
| Up / Down | Move selection by 1 |
| Left / Right | Move selection by 10 |
| A | Select game / Open folder / Confirm |
| B | Back |
| + | Exit |
| Y | Deletes custom icons from games |
| - | reboot to payload |

---

## Usage

1. Copy your image files anywhere on your SD card
2. Launch **IconSwap** from the homebrew menu
3. Scroll to the game you want to customize
4. Press **A** to open the file browser
5. Navigate to your image and press **A** to select it
6. Review the confirm screen — press **A** to apply
7. Reboot your Switch to see the icon change on the HOME menu

> A 1920x1080 screenshot, a square PNG, a BMP — any image works. IconSwap center-crops and converts everything automatically.

---

## Installation

Copy `IconSwap.nro` to your SD card:

```
SD:/switch/IconSwap/IconSwap.nro
```

Launch from the homebrew menu (hold R while launching a game, or via album shortcut).

---

## Requirements

- Nintendo Switch with custom firmware
- Atmosphère
- [sys-icon](https://github.com/eradicatinglove/sys-icon) installed and running

---

## Building

### 1. Install devkitPro

Make sure devkitA64 and libnx are installed via dkp-pacman.

### 2. Clone the repository

```bash
git clone https://github.com/eradicatinglove/IconSwap.git
cd IconSwap
```

### 3. Download stb headers (one-time setup)

```bash
cd src/
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
cd ..
```

> The stb headers are not included in the repo. Download them once before your first build.

### 4. Build

```bash
make clean && make
```

Output: `IconSwap.nro`

---

## Notes

- **Reboot required** after assigning icons for changes to appear on the HOME menu
- Existing icons are silently overwritten — no confirmation prompt per file
- Source images can be any resolution — center crop prevents stretching on non-square images
- Up to 512 games and 512 files per directory are supported

---

## License

GPL-2.0 — see [LICENSE](LICENSE)
