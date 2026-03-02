# IconSwap

Nintendo Switch homebrew NRO — browse JPEG files on your SD card and assign them as custom icons for sys-icon.

## Features

- Lists all installed games with their title IDs
- Shows which games already have icons assigned
- Browse any folder on SD card for JPEG files
- Auto-resizes to **256x256** (`icon.jpg`) and **174x174** (`icon174.jpg`)
- Creates `atmosphere/contents/<titleid>/` folder automatically
- Works alongside sys-icon sysmodule

---

## Controls

| Button | Action |
|---|---|
| ↑ / ↓ | Navigate list |
| A | Select / Confirm |
| B | Back / Cancel |
| + | Exit |

---

## Building

### 1. Install devkitPro with libnx

Make sure devkitA64 and libnx are installed via dkp-pacman.

### 2. Download stb headers (one-time setup)

Download these three single-header libraries into `src/`:

```bash
cd src/
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

### 3. Build

```bash
make
```

Output: `IconSwap.nro`

### 4. Install

Copy `IconSwap.nro` to:
```
SD:/switch/IconSwap/IconSwap.nro
```

---

## Usage

1. Launch from homebrew menu
2. Select a game from the list
3. Browse to your JPEG image on the SD card
4. Confirm — both `icon.jpg` and `icon174.jpg` are created automatically
5. Reboot your Switch to see the icon change on the HOME menu

---

## Notes

- Source images must be JPEG (.jpg / .jpeg)
- Images are automatically resized — source can be any resolution
- Existing icons are overwritten without warning
- Reboot is required after assigning icons (sys-icon reads on boot)
