/*
 * IconSwap - Nintendo Switch homebrew NRO
 * Assign custom icons to games for use with sys-icon
 * by eradicatinglove
 *
 * FTP server added: press X on game list to open FTP menu.
 * Upload images to sdmc:/icon-manager/ via FTP, then browse
 * and assign them from the file browser.
 */

#include <switch.h>
#include <switch/nacp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ftp.h"

extern "C" {
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
}

#define MAX_TITLES      512
#define MAX_FILES       512
#define WINDOW_SIZE     36
#define ICON256_SIZE    256
#define ICON174_SIZE    174
#define ICON256_MAXB    102400
#define ICON174_MAXB    65536
#define JPEG_QUALITY    90
#define MAX_PATH        512

typedef enum {
    SCREEN_GAMELIST,
    SCREEN_FILEBROWSER,
    SCREEN_CONFIRM,
    SCREEN_RESULT,
    SCREEN_REBOOT_CONFIRM,
    SCREEN_FTP
} Screen;

typedef struct {
    char name[256];
    char fullPath[MAX_PATH];
    int  isDir;
} FileEntry;

// Globals
static NsApplicationRecord  g_records[MAX_TITLES];
static char                 g_names[MAX_TITLES][0x201];
static s32                  g_recordCount = 0;
static FileEntry            g_files[MAX_FILES];
static int                  g_fileCount   = 0;
static Screen g_screen       = SCREEN_GAMELIST;
static int    g_gameSelected = 0;
static int    g_gameScroll   = 0;
static int    g_fileSelected = 0;
static int    g_fileScroll   = 0;
static char   g_currentDir[MAX_PATH]  = "sdmc:/";
static char   g_selectedImg[MAX_PATH] = "";
static char   g_resultMsg[256]        = "";
static int    g_resultOk = 0;
// 0 = crop to square (default), 1 = fit/letterbox (keeps full image, adds black bars)
static int    g_scaleMode = 0;

// UTF-8 safe copy: never cuts a multi-byte sequence mid-character
static void utf8_strncpy(char* dst, const char* src, size_t maxBytes) {
    if (!maxBytes) return;
    size_t i = 0, last_safe = 0;
    while (src[i] && i < maxBytes - 1) {
        if ((src[i] & 0xC0) != 0x80) last_safe = i;
        i++;
    }
    if (src[i] && (src[i] & 0xC0) == 0x80) i = last_safe;
    memcpy(dst, src, i);
    dst[i] = '\0';
}

// Image processing
static void jpegWriteCallback(void* ctx, void* data, int size) {
    fwrite(data, 1, size, (FILE*)ctx);
}

// Writes a JPEG, retrying at lower quality if file exceeds maxBytes.
// dstPixels must be targetSize*targetSize*3. Returns 1 on success.
static int writeJpegWithFallback(const char* dstPath, int targetSize,
                                  unsigned char* dstPixels, int maxBytes,
                                  const char* srcPath) {
    FILE* f = fopen(dstPath, "wb");
    if (!f) return 0;
    int result = stbi_write_jpg_to_func(jpegWriteCallback, f, targetSize, targetSize, 3, dstPixels, JPEG_QUALITY);
    fclose(f);
    struct stat st;
    if (result && stat(dstPath, &st) == 0 && st.st_size > maxBytes) {
        remove(dstPath);
        f = fopen(dstPath, "wb");
        if (!f) return 0;
        result = stbi_write_jpg_to_func(jpegWriteCallback, f, targetSize, targetSize, 3, dstPixels, 75);
        fclose(f);
    }
    return result;
}

// MODE 0 – Crop to square (original behaviour):
//   Takes the largest centred square from the source, then scales to targetSize.
// MODE 1 – Fit / letterbox (new):
//   Scales the full image to fit inside targetSize×targetSize while keeping the
//   aspect ratio, then centres it on a black canvas.  No pixels are cropped.
static int resizeAndSave(const char* srcPath, const char* dstPath, int targetSize, int maxBytes) {
    int srcW, srcH, channels;
    unsigned char* srcPixels = stbi_load(srcPath, &srcW, &srcH, &channels, 3);
    if (!srcPixels) return 0;

    unsigned char* dstPixels = (unsigned char*)malloc(targetSize * targetSize * 3);
    if (!dstPixels) { stbi_image_free(srcPixels); return 0; }

    if (g_scaleMode == 1) {
        // --- Squeeze mode (for vertical source images + vertical themes) ---
        // Scale the full image directly to targetSize x targetSize with no
        // cropping and no padding.  A 600x900 source becomes 256x256; the
        // vertical .nxtheme then stretches it back to 2:3 on screen,
        // restoring the original composition correctly.
        stbir_resize_uint8_linear(srcPixels, srcW, srcH, 0, dstPixels, targetSize, targetSize, 0, STBIR_RGB);
        stbi_image_free(srcPixels);
    } else {
        // --- Crop to square mode (original behaviour) ---
        int cropSize = (srcW < srcH) ? srcW : srcH;
        int cropX = (srcW - cropSize) / 2;
        int cropY = (srcH - cropSize) / 2;

        unsigned char* cropPixels = (unsigned char*)malloc(cropSize * cropSize * 3);
        if (!cropPixels) { stbi_image_free(srcPixels); free(dstPixels); return 0; }
        for (int row = 0; row < cropSize; row++) {
            memcpy(
                cropPixels + row * cropSize * 3,
                srcPixels + ((cropY + row) * srcW + cropX) * 3,
                cropSize * 3
            );
        }
        stbi_image_free(srcPixels);
        stbir_resize_uint8_linear(cropPixels, cropSize, cropSize, 0, dstPixels, targetSize, targetSize, 0, STBIR_RGB);
        free(cropPixels);
    }

    int result = writeJpegWithFallback(dstPath, targetSize, dstPixels, maxBytes, srcPath);
    free(dstPixels);
    return result;
}

static void applyIcon(const char* srcPath, u64 titleId) {
    char dst256[MAX_PATH], dst174[MAX_PATH];
    char folder[MAX_PATH];
    snprintf(folder, sizeof(folder), "sdmc:/atmosphere/contents/%016lx", titleId);
    mkdir(folder, 0777);
    snprintf(dst256, sizeof(dst256), "sdmc:/atmosphere/contents/%016lx/icon.jpg",    titleId);
    snprintf(dst174, sizeof(dst174), "sdmc:/atmosphere/contents/%016lx/icon174.jpg", titleId);
    int ok256 = resizeAndSave(srcPath, dst256, ICON256_SIZE, ICON256_MAXB);
    int ok174 = resizeAndSave(srcPath, dst174, ICON174_SIZE, ICON174_MAXB);
    if (ok256 && ok174) {
        g_resultOk = 1;
        snprintf(g_resultMsg, sizeof(g_resultMsg), "icon.jpg and icon174.jpg saved!");
    } else if (ok256) {
        g_resultOk = 1;
        snprintf(g_resultMsg, sizeof(g_resultMsg), "icon.jpg saved. icon174.jpg failed.");
    } else {
        g_resultOk = 0;
        snprintf(g_resultMsg, sizeof(g_resultMsg), "Failed! Is the source a valid image?");
    }
}

// File browser
static int isImage(const char* name) {
    int len = strlen(name);
    if (len < 4) return 0;
    const char* ext = name + len - 4;
    if (strcasecmp(ext, ".jpg") == 0) return 1;
    if (strcasecmp(ext, ".png") == 0) return 1;
    if (strcasecmp(ext, ".bmp") == 0) return 1;
    if (strcasecmp(ext, ".tga") == 0) return 1;
    if (len >= 5 && strcasecmp(name + len - 5, ".jpeg") == 0) return 1;
    return 0;
}

static void loadDir(const char* path) {
    g_fileCount = 0;
    if (strcmp(path, "sdmc:/") != 0) {
        strncpy(g_files[0].name,     "..", sizeof(g_files[0].name) - 1);
        strncpy(g_files[0].fullPath, "..", sizeof(g_files[0].fullPath) - 1);
        g_files[0].isDir = 1;
        g_fileCount = 1;
    }
    DIR* dir = opendir(path);
    if (!dir) return;
    FileEntry dirs[MAX_FILES], files[MAX_FILES];
    int dc = 0, fc = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char fp[MAX_PATH];
        int plen = strlen(path);
        snprintf(fp, sizeof(fp), "%s%s%s", path, (plen > 0 && path[plen-1] == '/') ? "" : "/", ent->d_name);
        if (ent->d_type == DT_DIR && dc < MAX_FILES) {
            utf8_strncpy(dirs[dc].name, ent->d_name, sizeof(dirs[0].name));
            strncpy(dirs[dc].fullPath, fp, sizeof(dirs[0].fullPath) - 1);
            dirs[dc].isDir = 1; dc++;
        } else if (isImage(ent->d_name) && fc < MAX_FILES) {
            utf8_strncpy(files[fc].name, ent->d_name, sizeof(files[0].name));
            strncpy(files[fc].fullPath, fp, sizeof(files[0].fullPath) - 1);
            files[fc].isDir = 0; fc++;
        }
    }
    closedir(dir);
    for (int i = 0; i < dc && g_fileCount < MAX_FILES; i++) g_files[g_fileCount++] = dirs[i];
    for (int i = 0; i < fc && g_fileCount < MAX_FILES; i++) g_files[g_fileCount++] = files[i];
}

static void goUpDir() {
    int len = strlen(g_currentDir);
    if (len > 1 && g_currentDir[len-1] == '/') len--;
    int pos = len - 1;
    while (pos > 0 && g_currentDir[pos] != '/') pos--;
    if (pos <= 6) strncpy(g_currentDir, "sdmc:/", sizeof(g_currentDir));
    else g_currentDir[pos+1] = '\0';
}

// Remove icons
static void removeIcons(u64 titleId) {
    char dst256[MAX_PATH], dst174[MAX_PATH];
    snprintf(dst256, sizeof(dst256), "sdmc:/atmosphere/contents/%016lx/icon.jpg",    titleId);
    snprintf(dst174, sizeof(dst174), "sdmc:/atmosphere/contents/%016lx/icon174.jpg", titleId);
    int r1 = remove(dst256);
    int r2 = remove(dst174);
    if (r1 == 0 || r2 == 0) {
        g_resultOk = 1;
        snprintf(g_resultMsg, sizeof(g_resultMsg), "Icons removed successfully.");
    } else {
        g_resultOk = 0;
        snprintf(g_resultMsg, sizeof(g_resultMsg), "No icons found to remove.");
    }
}

// Reboot to payload
static void rebootToPayload() {
    spsmShutdown(true);
}

// Render functions
static void renderRebootConfirm() {
    consoleClear();
    printf("=== IconSwap - Reboot to Payload ===\n\n");
    printf("Reboot now and load payload?\n\n");
    printf("Make sure reboot_payload.bin is at:\n");
    printf("  atmosphere/reboot_payload.bin\n\n");
    printf("A: Reboot now    B: Cancel\n");
}

static void renderGameList() {
    consoleClear();
    printf("=== IconSwap - Select Game ===  (Total: %d)\n\n", g_recordCount);
    int end = g_gameScroll + WINDOW_SIZE;
    if (end > g_recordCount) end = g_recordCount;
    for (int i = g_gameScroll; i < end; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "sdmc:/atmosphere/contents/%016lx/icon.jpg", g_records[i].application_id);
        struct stat st;
        const char* tag = (stat(path, &st) == 0) ? "[*]" : "   ";
        if (i == g_gameSelected)
            printf("> %s [%016lX] %s\n", tag, g_records[i].application_id, g_names[i]);
        else
            printf("  %s  %016lX  %s\n", tag, g_records[i].application_id, g_names[i]);
    }
    printf("\nUp/Down: move 1  Left/Right: move 10  A: assign icon  +: exit\n");
    printf("Y: remove icons  -: reboot to payload  X: FTP menu\n");
    if (ftpIsRunning()) printf("[FTP ON: %s port 5000]\n", ftpGetIp());
}

static void renderFileBrowser() {
    consoleClear();
    printf("=== IconSwap - Select Image ===\n");
    printf("Game: %.60s\n", g_names[g_gameSelected]);
    printf("Dir : %.70s\n", g_currentDir);
    printf("Mode: %s  (Y: toggle)\n",
           g_scaleMode == 0 ? "[CROP]  squeeze" : " CROP  [SQUEEZE]");
    if (g_scaleMode == 0) {
        printf("Hint: Crops the centre square of your image.\n");
        printf("      Best for square/landscape images with a\n");
        printf("      standard square icon theme.\n\n");
    } else {
        printf("Hint: Scales the full image to 256x256 (no crop).\n");
        printf("      for vertical themes 2:3 images (600x900) \n");
        printf("      theme restores aspect.\n\n");
    }
    int end = g_fileScroll + WINDOW_SIZE;
    if (end > g_fileCount) end = g_fileCount;
    if (g_fileCount == 0) printf("  (no image files or folders here)\n");
    for (int i = g_fileScroll; i < end; i++) {
        const char* slash = g_files[i].isDir ? "/" : "";
        if (i == g_fileSelected)
            printf("> %.70s%s\n", g_files[i].name, slash);
        else
            printf("  %.70s%s\n", g_files[i].name, slash);
    }
    printf("\nUp/Down: move  A: open/select  B: back  Y: toggle mode\n");
    printf("Supported: JPG PNG BMP TGA  (UTF-8 filenames supported)\n");
}

static void renderConfirm() {
    consoleClear();
    printf("=== IconSwap - Confirm ===\n\n");
    printf("Game  : %.60s\n",   g_names[g_gameSelected]);
    printf("TID   : %016lX\n\n", g_records[g_gameSelected].application_id);
    printf("Image : %.70s\n\n", g_selectedImg);
    if (g_scaleMode == 0) {
        printf("Mode  : CROP\n");
        printf("        Crops centre square -> 256x256.\n");
        printf("        Use with square/landscape images + square theme.\n\n");
    } else {
        printf("Mode  : SQUEEZE\n");
        printf("        Full image scaled to 256x256 (no crop).\n");
        printf("        Use with 2:3 images (600x900) + vertical theme.\n\n");
    }
    printf("Will create:\n");
    printf("  icon.jpg    (256x256)\n");
    printf("  icon174.jpg (174x174)\n\n");
    printf("A: Confirm and apply    B: Cancel\n");
}

static void renderResult() {
    consoleClear();
    printf("=== IconSwap - Done ===\n\n");
    if (g_resultOk)
        printf("OK: %s\n\nReboot Switch to see icon on HOME menu.\n", g_resultMsg);
    else
        printf("ERROR: %s\n", g_resultMsg);
    printf("\nA: Back to game list    -: Reboot to payload    +: Exit\n");
}

static void renderFtp() {
    consoleClear();
    printf("=== IconSwap - FTP Server ===\n\n");
    if (!ftpIsRunning()) {
        printf("ERROR: Could not start FTP server.\n");
        printf("Make sure Wi-Fi is connected.\n\n");
        printf("Status: %s\n", ftpGetStatus());
    } else {
        printf("FTP server: RUNNING\n\n");
        printf("  IP Address : %s\n", ftpGetIp());
        printf("  Port       : 5000\n");
        printf("  Login      : any username / any password\n");
        printf("  Encoding   : UTF-8\n\n");
        printf("  Status     : %s\n", ftpGetStatus());
        printf("  Uploaded   : %d image(s) this session\n", ftpGetFilesReceived());
    }
    printf("\nB: Back to game list\n");
}

// Main
int main(int argc, char** argv) {
    consoleInit(NULL);
    nsInitialize();
    fsInitialize();
    spsmInitialize();
    socketInitializeDefault();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Result rc = nsListApplicationRecord(g_records, MAX_TITLES, 0, &g_recordCount);
    if (R_FAILED(rc)) {
        printf("Error listing applications: 0x%x\n", rc);
        consoleUpdate(NULL);
        svcSleepThread(3000000000ULL);
        goto cleanup;
    }

    for (s32 i = 0; i < g_recordCount; i++) {
        NsApplicationControlData ctrl;
        size_t ctrlSize;
        rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
             g_records[i].application_id, &ctrl, sizeof(ctrl), &ctrlSize);
        if (R_SUCCEEDED(rc)) {
            NacpLanguageEntry* lang = NULL;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&ctrl.nacp, &lang)) && lang != NULL)
                utf8_strncpy(g_names[i], lang->name, sizeof(g_names[i]));
            else
                utf8_strncpy(g_names[i], ctrl.nacp.lang[0].name, sizeof(g_names[i]));
        } else {
            snprintf(g_names[i], sizeof(g_names[i]), "%016lX", g_records[i].application_id);
        }
    }

    loadDir(g_currentDir);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        switch (g_screen) {
            case SCREEN_GAMELIST:
                if (kDown & HidNpadButton_Down) {
                    if (g_gameSelected < g_recordCount - 1) {
                        g_gameSelected++;
                        if (g_gameSelected >= g_gameScroll + WINDOW_SIZE) g_gameScroll++;
                        if (g_gameScroll > g_recordCount - WINDOW_SIZE) g_gameScroll = g_recordCount - WINDOW_SIZE;
                        if (g_gameScroll < 0) g_gameScroll = 0;
                    }
                }
                if (kDown & HidNpadButton_Up) {
                    if (g_gameSelected > 0) {
                        g_gameSelected--;
                        if (g_gameSelected < g_gameScroll) g_gameScroll--;
                        if (g_gameScroll < 0) g_gameScroll = 0;
                    }
                }
                if (kDown & HidNpadButton_Right) {
                    g_gameSelected += 10;
                    if (g_gameSelected >= g_recordCount) g_gameSelected = g_recordCount - 1;
                    if (g_gameSelected >= g_gameScroll + WINDOW_SIZE) {
                        g_gameScroll = g_gameSelected - WINDOW_SIZE + 1;
                        if (g_gameScroll < 0) g_gameScroll = 0;
                    }
                }
                if (kDown & HidNpadButton_Left) {
                    g_gameSelected -= 10;
                    if (g_gameSelected < 0) g_gameSelected = 0;
                    if (g_gameSelected < g_gameScroll) {
                        g_gameScroll = g_gameSelected;
                        if (g_gameScroll < 0) g_gameScroll = 0;
                    }
                }
                if (kDown & HidNpadButton_A) {
                    if (g_recordCount > 0) {
                        strncpy(g_currentDir, "sdmc:/", sizeof(g_currentDir));
                        g_fileSelected = 0; g_fileScroll = 0;
                        loadDir(g_currentDir);
                        g_screen = SCREEN_FILEBROWSER;
                    }
                }
                if (kDown & HidNpadButton_Y) {
                    if (g_recordCount > 0) {
                        removeIcons(g_records[g_gameSelected].application_id);
                        g_screen = SCREEN_RESULT;
                    }
                }
                if (kDown & HidNpadButton_Minus) {
                    g_screen = SCREEN_REBOOT_CONFIRM;
                }
                if (kDown & HidNpadButton_X) {
                    if (!ftpIsRunning()) ftpStart();
                    g_screen = SCREEN_FTP;
                }
                renderGameList();
                break;

            case SCREEN_FILEBROWSER:
                if (kDown & HidNpadButton_Down) {
                    if (g_fileSelected < g_fileCount - 1) {
                        g_fileSelected++;
                        if (g_fileSelected >= g_fileScroll + WINDOW_SIZE) g_fileScroll++;
                    }
                }
                if (kDown & HidNpadButton_Up) {
                    if (g_fileSelected > 0) {
                        g_fileSelected--;
                        if (g_fileSelected < g_fileScroll) g_fileScroll--;
                    }
                }
                if (kDown & HidNpadButton_A) {
                    if (g_fileCount > 0) {
                        FileEntry* f = &g_files[g_fileSelected];
                        if (f->isDir) {
                            if (strcmp(f->name, "..") == 0) {
                                goUpDir();
                            } else {
                                strncpy(g_currentDir, f->fullPath, sizeof(g_currentDir) - 1);
                                int len = strlen(g_currentDir);
                                if (len > 0 && g_currentDir[len-1] != '/')
                                    strncat(g_currentDir, "/", sizeof(g_currentDir) - len - 1);
                            }
                            g_fileSelected = 0; g_fileScroll = 0;
                            loadDir(g_currentDir);
                        } else {
                            strncpy(g_selectedImg, f->fullPath, sizeof(g_selectedImg) - 1);
                            g_screen = SCREEN_CONFIRM;
                        }
                    }
                }
                if (kDown & HidNpadButton_B) {
                    g_screen = SCREEN_GAMELIST;
                }
                if (kDown & HidNpadButton_Y) {
                    g_scaleMode = (g_scaleMode == 0) ? 1 : 0;
                }
                renderFileBrowser();
                break;

            case SCREEN_CONFIRM:
                if (kDown & HidNpadButton_A) {
                    applyIcon(g_selectedImg, g_records[g_gameSelected].application_id);
                    g_screen = SCREEN_RESULT;
                }
                if (kDown & HidNpadButton_B) {
                    g_screen = SCREEN_FILEBROWSER;
                }
                renderConfirm();
                break;

            case SCREEN_RESULT:
                if (kDown & HidNpadButton_A) {
                    g_screen = SCREEN_GAMELIST;
                }
                if (kDown & HidNpadButton_Minus) {
                    g_screen = SCREEN_REBOOT_CONFIRM;
                }
                renderResult();
                break;

            case SCREEN_REBOOT_CONFIRM:
                if (kDown & HidNpadButton_A) {
                    rebootToPayload();
                }
                if (kDown & HidNpadButton_B) {
                    g_screen = SCREEN_GAMELIST;
                }
                renderRebootConfirm();
                break;

            case SCREEN_FTP:
                if (kDown & HidNpadButton_B) {
                    g_screen = SCREEN_GAMELIST;
                }
                renderFtp();
                break;
        }

        consoleUpdate(NULL);
    }

cleanup:
    if (ftpIsRunning()) ftpStop();
    socketExit();
    spsmExit();
    fsExit();
    nsExit();
    consoleExit(NULL);
    return 0;
}
