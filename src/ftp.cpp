/*
 * ftp.cpp - FTP server for IconSwap, runs on its own libnx thread.
 *
 * Uses the EXACT same non-blocking poll logic as the working single-file
 * version. The only difference is it runs in a thread loop instead of
 * being called from the main loop every frame.
 */

#include "ftp.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define FTP_PORT        5000
#define FTP_BUF_SIZE    4096
#define FTP_MAX_CLIENTS 4
#define FTP_MAX_PATH    512
#define FTP_STACK_SIZE  (256 * 1024)
#define FTP_THREAD_PRIO 0x2C

typedef struct {
    int  ctrlFd;
    int  dataFd;
    int  passiveListenFd;
    char cwd[FTP_MAX_PATH];
    char recvBuf[FTP_BUF_SIZE];
    int  recvLen;
    char renamePending[FTP_MAX_PATH];
    bool loggedIn;
    bool binaryMode;
} FtpClient;

static Mutex         g_mutex;
static char          g_ipStr[32]     = "0.0.0.0";
static char          g_status[128]   = "FTP not started";
static int           g_filesReceived = 0;
static volatile bool g_running       = false;

static Thread    g_thread;
static int       g_listenFd = -1;
static FtpClient g_clients[FTP_MAX_CLIENTS];

// ── Helpers ───────────────────────────────────────────────────────────────────

static void setStatus(const char* msg) {
    mutexLock(&g_mutex);
    strncpy(g_status, msg, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    mutexUnlock(&g_mutex);
}

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

static bool isImage(const char* name) {
    int len = strlen(name);
    if (len < 4) return false;
    const char* ext = name + len - 4;
    if (strcasecmp(ext, ".jpg") == 0) return true;
    if (strcasecmp(ext, ".png") == 0) return true;
    if (strcasecmp(ext, ".bmp") == 0) return true;
    if (strcasecmp(ext, ".tga") == 0) return true;
    if (len >= 5 && strcasecmp(name + len - 5, ".jpeg") == 0) return true;
    return false;
}

// ── FTP helpers (verbatim from working main.cpp) ──────────────────────────────

static void ftpToSdmcPath(const char* ftpPath, char* out, size_t outSize) {
    const char* rel = ftpPath;
    while (*rel == '/') rel++;
    if (*rel == '\0')
        snprintf(out, outSize, "sdmc:/");
    else
        snprintf(out, outSize, "sdmc:/%.*s", (int)(outSize - 8), rel);
}

static void ftpResolvePath(FtpClient* c, const char* arg, char* out, size_t outSize) {
    if (!arg || arg[0] == '\0') { snprintf(out, outSize, "/%s", c->cwd); return; }
    if (arg[0] == '/') { snprintf(out, outSize, "%s", arg); }
    else {
        if (c->cwd[0]) snprintf(out, outSize, "/%s/%s", c->cwd, arg);
        else           snprintf(out, outSize, "/%s", arg);
    }
}

static void ftpSend(int fd, const char* msg) {
    if (fd < 0) return;
    send(fd, msg, strlen(msg), 0);
}

static void ftpCloseClient(FtpClient* c) {
    if (c->dataFd >= 0)          { close(c->dataFd);          c->dataFd = -1; }
    if (c->passiveListenFd >= 0) { close(c->passiveListenFd); c->passiveListenFd = -1; }
    if (c->ctrlFd >= 0)          { close(c->ctrlFd);          c->ctrlFd = -1; }
    c->recvLen  = 0;
    c->loggedIn = false;
}

// Open data connection.
// Uses select() to wait up to 10s for client to connect - reliable on libnx.
static int ftpOpenDataConn(FtpClient* c) {
    if (c->dataFd >= 0) return c->dataFd;
    if (c->passiveListenFd < 0) return -1;

    // select() is reliable on libnx; SO_RCVTIMEO on listen sockets is not
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(c->passiveListenFd, &fds);
    struct timeval tv = { 10, 0 };
    int ready = select(c->passiveListenFd + 1, &fds, NULL, NULL, &tv);

    int dataFd = -1;
    if (ready > 0) {
        struct sockaddr_in addr; socklen_t addrLen = sizeof(addr);
        dataFd = accept(c->passiveListenFd, (struct sockaddr*)&addr, &addrLen);
    }

    close(c->passiveListenFd); c->passiveListenFd = -1;

    if (dataFd >= 0) {
        // Ensure data socket is blocking
        int flags = fcntl(dataFd, F_GETFL, 0);
        fcntl(dataFd, F_SETFL, flags & ~O_NONBLOCK);
        // TCP_NODELAY so data flushes without Nagle delay
        int nodelay = 1;
        setsockopt(dataFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        // Generous timeouts on data socket
        struct timeval dtv = { 30, 0 };
        setsockopt(dataFd, SOL_SOCKET, SO_SNDTIMEO, &dtv, sizeof(dtv));
        setsockopt(dataFd, SOL_SOCKET, SO_RCVTIMEO, &dtv, sizeof(dtv));
    }

    c->dataFd = dataFd;
    return dataFd;
}

// ── Command handler (verbatim from working main.cpp) ─────────────────────────

static bool ftpHandleCmd(FtpClient* c, char* line) {
    char cmd[16] = {0};
    char arg[FTP_MAX_PATH] = {0};

    int llen = strlen(line);
    while (llen > 0 && (line[llen-1] == '\r' || line[llen-1] == '\n')) line[--llen] = '\0';

    char* sp = strchr(line, ' ');
    if (sp) {
        int clen = (int)(sp - line);
        if (clen >= (int)sizeof(cmd)) clen = (int)sizeof(cmd) - 1;
        memcpy(cmd, line, clen);
        const char* a = sp + 1; while (*a == ' ') a++;
        utf8_strncpy(arg, a, sizeof(arg));
    } else {
        if (llen >= (int)sizeof(cmd)) llen = (int)sizeof(cmd) - 1;
        memcpy(cmd, line, llen);
    }
    for (int i = 0; cmd[i]; i++) if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] -= 32;

    if (strcmp(cmd, "USER") == 0) { ftpSend(c->ctrlFd, "331 Password required.\r\n"); return true; }
    if (strcmp(cmd, "PASS") == 0) { c->loggedIn = true; ftpSend(c->ctrlFd, "230 Logged in.\r\n"); return true; }
    if (strcmp(cmd, "QUIT") == 0) { ftpSend(c->ctrlFd, "221 Goodbye.\r\n"); return false; }
    if (strcmp(cmd, "SYST") == 0) { ftpSend(c->ctrlFd, "215 UNIX Type: L8\r\n"); return true; }
    if (strcmp(cmd, "FEAT") == 0) { ftpSend(c->ctrlFd, "211-Features:\r\n UTF8\r\n211 End\r\n"); return true; }
    if (strcmp(cmd, "OPTS") == 0) { ftpSend(c->ctrlFd, "200 OK.\r\n"); return true; }
    if (strcmp(cmd, "TYPE") == 0) { c->binaryMode = (arg[0]=='I'||arg[0]=='i'); ftpSend(c->ctrlFd, "200 Type set.\r\n"); return true; }
    if (strcmp(cmd, "MODE") == 0 || strcmp(cmd, "STRU") == 0) { ftpSend(c->ctrlFd, "200 OK.\r\n"); return true; }
    if (strcmp(cmd, "NOOP") == 0) { ftpSend(c->ctrlFd, "200 NOOP OK.\r\n"); return true; }
    if (!c->loggedIn) { ftpSend(c->ctrlFd, "530 Not logged in.\r\n"); return true; }

    if (strcmp(cmd, "PWD") == 0) {
        char resp[FTP_MAX_PATH + 16];
        snprintf(resp, sizeof(resp), "257 \"/%s\" is current directory.\r\n", c->cwd);
        ftpSend(c->ctrlFd, resp); return true;
    }
    if (strcmp(cmd, "CWD") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs));
        ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        struct stat st;
        if (strcmp(ftpAbs, "/") == 0 || (stat(sdmcPath, &st) == 0 && S_ISDIR(st.st_mode))) {
            const char* rel = ftpAbs; while (*rel == '/') rel++;
            strncpy(c->cwd, rel, sizeof(c->cwd) - 1); c->cwd[sizeof(c->cwd)-1] = '\0';
            ftpSend(c->ctrlFd, "250 Directory changed.\r\n");
        } else ftpSend(c->ctrlFd, "550 Directory not found.\r\n");
        return true;
    }
    if (strcmp(cmd, "CDUP") == 0) {
        int len = strlen(c->cwd);
        while (len > 0 && c->cwd[len-1] != '/') len--;
        if (len > 0) len--;
        c->cwd[len] = '\0';
        ftpSend(c->ctrlFd, "250 Directory changed.\r\n"); return true;
    }
    if (strcmp(cmd, "PASV") == 0) {
        if (c->passiveListenFd >= 0) { close(c->passiveListenFd); c->passiveListenFd = -1; }
        int listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) { ftpSend(c->ctrlFd, "425 Cannot create data socket.\r\n"); return true; }
        int opt = 1; setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        // Enable TCP_NODELAY on data listen socket for low latency
        setsockopt(listenFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET; addr.sin_addr.s_addr = inet_addr(g_ipStr); addr.sin_port = 0;
        if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(listenFd, 1) < 0) {
            close(listenFd); ftpSend(c->ctrlFd, "425 Cannot bind data socket.\r\n"); return true;
        }
        socklen_t addrLen = sizeof(addr);
        getsockname(listenFd, (struct sockaddr*)&addr, &addrLen);
        c->passiveListenFd = listenFd;
        unsigned long ip = inet_addr(g_ipStr); unsigned char* ip4 = (unsigned char*)&ip;
        unsigned short port = ntohs(addr.sin_port);
        char resp[64];
        snprintf(resp, sizeof(resp), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
            ip4[0],ip4[1],ip4[2],ip4[3],(port>>8)&0xFF,port&0xFF);
        ftpSend(c->ctrlFd, resp);
        return true;
    }
    if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
        ftpSend(c->ctrlFd, "150 Opening data connection.\r\n");  // send 150 BEFORE accepting
        svcSleepThread(20000000LL); // 20ms flush
        int dataFd = ftpOpenDataConn(c);
        if (dataFd < 0) { ftpSend(c->ctrlFd, "425 No data connection.\r\n"); return true; }
        char sdmcPath[FTP_MAX_PATH];
        ftpToSdmcPath(c->cwd[0] ? c->cwd : "", sdmcPath, sizeof(sdmcPath));
        DIR* dir = opendir(sdmcPath);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                char fp[FTP_MAX_PATH + 256]; snprintf(fp, sizeof(fp), "%s/%s", sdmcPath, ent->d_name);
                struct stat st; stat(fp, &st);
                char linebuf[512];
                if (strcmp(cmd, "NLST") == 0)
                    snprintf(linebuf, sizeof(linebuf), "%s\r\n", ent->d_name);
                else
                    snprintf(linebuf, sizeof(linebuf), "%srwxr-xr-x 1 ftp ftp %8ld Jan  1 00:00 %s\r\n",
                        S_ISDIR(st.st_mode) ? "d" : "-", (long)st.st_size, ent->d_name);
                send(dataFd, linebuf, strlen(linebuf), 0);
            }
            closedir(dir);
        }
        close(dataFd); c->dataFd = -1;
        ftpSend(c->ctrlFd, "226 Transfer complete.\r\n"); return true;
    }
    if (strcmp(cmd, "STOR") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs));
        ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        char dirPart[FTP_MAX_PATH]; strncpy(dirPart, sdmcPath, sizeof(dirPart)-1); dirPart[sizeof(dirPart)-1]='\0';
        char* slash = strrchr(dirPart, '/'); if (slash) { *slash='\0'; mkdir(dirPart, 0777); }
        ftpSend(c->ctrlFd, "150 Opening data connection for upload.\r\n");  // send 150 BEFORE accepting
        int dataFd = ftpOpenDataConn(c);
        if (dataFd < 0) { ftpSend(c->ctrlFd, "425 No data connection.\r\n"); return true; }
        FILE* f = fopen(sdmcPath, "wb");
        if (!f) { close(dataFd); c->dataFd=-1; ftpSend(c->ctrlFd, "550 Cannot create file.\r\n"); return true; }
        char buf[FTP_BUF_SIZE]; int n;
        while ((n = recv(dataFd, buf, sizeof(buf), 0)) > 0) fwrite(buf, 1, n, f);
        fclose(f); close(dataFd); c->dataFd = -1;
        if (isImage(sdmcPath)) { mutexLock(&g_mutex); g_filesReceived++; mutexUnlock(&g_mutex); }
        char st[128]; snprintf(st, sizeof(st), "Uploaded: %.80s", arg[0] ? arg : "file");
        setStatus(st);
        ftpSend(c->ctrlFd, "226 Transfer complete.\r\n"); return true;
    }
    if (strcmp(cmd, "RETR") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs));
        ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        struct stat st;
        if (stat(sdmcPath, &st) != 0) {
            if (c->passiveListenFd >= 0) { close(c->passiveListenFd); c->passiveListenFd=-1; }
            ftpSend(c->ctrlFd, "550 File not found.\r\n"); return true;
        }
        char resp[64]; snprintf(resp, sizeof(resp), "150 Opening data connection (%ld bytes).\r\n", (long)st.st_size);
        ftpSend(c->ctrlFd, resp);  // send 150 BEFORE accepting data conn so client knows to connect
        svcSleepThread(20000000LL); // 20ms: ensure 150 is flushed to network before blocking on accept
        int dataFd = ftpOpenDataConn(c);
        if (dataFd < 0) { ftpSend(c->ctrlFd, "425 No data connection.\r\n"); return true; }
        FILE* f = fopen(sdmcPath, "rb");
        if (!f) { close(dataFd); c->dataFd=-1; ftpSend(c->ctrlFd, "550 Cannot open file.\r\n"); return true; }
        char buf[FTP_BUF_SIZE]; int n;
        while ((n = (int)fread(buf, 1, sizeof(buf), f)) > 0) send(dataFd, buf, n, 0);
        fclose(f); close(dataFd); c->dataFd = -1;
        ftpSend(c->ctrlFd, "226 Transfer complete.\r\n"); return true;
    }
    if (strcmp(cmd, "DELE") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs)); ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        ftpSend(c->ctrlFd, remove(sdmcPath)==0 ? "250 File deleted.\r\n" : "550 Delete failed.\r\n"); return true;
    }
    if (strcmp(cmd, "MKD") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs)); ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        if (mkdir(sdmcPath, 0777)==0) { char r[FTP_MAX_PATH+8]; snprintf(r,sizeof(r),"257 \"%s\" created.\r\n",ftpAbs); ftpSend(c->ctrlFd,r); }
        else ftpSend(c->ctrlFd, "550 MKD failed.\r\n"); return true;
    }
    if (strcmp(cmd, "RMD") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs)); ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        ftpSend(c->ctrlFd, rmdir(sdmcPath)==0 ? "250 Directory removed.\r\n" : "550 RMD failed.\r\n"); return true;
    }
    if (strcmp(cmd, "RNFR") == 0) {
        char ftpAbs[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs));
        ftpToSdmcPath(ftpAbs, c->renamePending, sizeof(c->renamePending));
        ftpSend(c->ctrlFd, "350 Ready for RNTO.\r\n"); return true;
    }
    if (strcmp(cmd, "RNTO") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs)); ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        ftpSend(c->ctrlFd, rename(c->renamePending,sdmcPath)==0 ? "250 Rename OK.\r\n" : "550 Rename failed.\r\n");
        c->renamePending[0]='\0'; return true;
    }
    if (strcmp(cmd, "SIZE") == 0) {
        char ftpAbs[FTP_MAX_PATH], sdmcPath[FTP_MAX_PATH];
        ftpResolvePath(c, arg, ftpAbs, sizeof(ftpAbs)); ftpToSdmcPath(ftpAbs, sdmcPath, sizeof(sdmcPath));
        struct stat st;
        if (stat(sdmcPath, &st)==0) { char r[32]; snprintf(r,sizeof(r),"213 %ld\r\n",(long)st.st_size); ftpSend(c->ctrlFd,r); }
        else ftpSend(c->ctrlFd, "550 File not found.\r\n"); return true;
    }
    ftpSend(c->ctrlFd, "502 Command not implemented.\r\n"); return true;
}

// ── ftpPoll - exact logic from working main.cpp, called from thread loop ──────
static void ftpPoll() {
    // Accept new client
    if (g_listenFd >= 0) {
        struct sockaddr_in addr; socklen_t addrLen = sizeof(addr);
        int newFd = accept(g_listenFd, (struct sockaddr*)&addr, &addrLen);
        if (newFd >= 0) {
            for (int i = 0; i < FTP_MAX_CLIENTS; i++) {
                if (g_clients[i].ctrlFd < 0) {
                    FtpClient* c = &g_clients[i];
                    c->ctrlFd=newFd; c->dataFd=-1; c->passiveListenFd=-1;
                    c->recvLen=0; c->loggedIn=false; c->binaryMode=true;
                    c->cwd[0]='\0'; c->renamePending[0]='\0';
                    fcntl(newFd, F_SETFL, O_NONBLOCK);
                    int nodelay = 1;
                    setsockopt(newFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                    ftpSend(newFd, "220 IconSwap FTP Server Ready (UTF-8).\r\n");
                    char st[64]; snprintf(st, sizeof(st), "Client: %s", inet_ntoa(addr.sin_addr));
                    setStatus(st);
                    goto acceptDone;
                }
            }
            close(newFd);
            acceptDone:;
        }
    }
    // Service existing clients
    for (int i = 0; i < FTP_MAX_CLIENTS; i++) {
        FtpClient* c = &g_clients[i];
        if (c->ctrlFd < 0) continue;
        char tmp[FTP_BUF_SIZE];
        int n = recv(c->ctrlFd, tmp, sizeof(tmp), 0);
        if (n == 0) { ftpCloseClient(c); continue; }
        if (n < 0)  { if (errno != EAGAIN && errno != EWOULDBLOCK) ftpCloseClient(c); continue; }
        int space = FTP_BUF_SIZE - c->recvLen - 1;
        if (n > space) n = space;
        memcpy(c->recvBuf + c->recvLen, tmp, n);
        c->recvLen += n; c->recvBuf[c->recvLen] = '\0';
        char* lineStart = c->recvBuf;
        char* nl;
        while ((nl = strchr(lineStart, '\n')) != NULL) {
            *nl = '\0';
            bool keep = ftpHandleCmd(c, lineStart);
            if (!keep) { ftpCloseClient(c); goto nextClient; }
            lineStart = nl + 1;
        }
        {
            int remaining = (int)(c->recvBuf + c->recvLen - lineStart);
            if (remaining > 0 && lineStart != c->recvBuf) memmove(c->recvBuf, lineStart, remaining);
            c->recvLen = remaining; c->recvBuf[c->recvLen] = '\0';
        }
        nextClient:;
    }
}

// ── FTP thread ─────────────────────────────────────────────────────────────────
static void ftpThreadFunc(void* arg) {
    (void)arg;
    setStatus("Waiting for connections...");
    while (g_running) {
        ftpPoll();
        svcSleepThread(1000000LL); // 1ms yield so we don't pin the CPU
    }
}

// ── IP detection ──────────────────────────────────────────────────────────────
static bool getLocalIp(char* out, size_t outSize) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in remote; memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET; remote.sin_port = htons(80);
    remote.sin_addr.s_addr = inet_addr("8.8.8.8");
    fcntl(fd, F_SETFL, O_NONBLOCK);
    connect(fd, (struct sockaddr*)&remote, sizeof(remote));
    struct sockaddr_in local; socklen_t localLen = sizeof(local);
    bool ok = (getsockname(fd, (struct sockaddr*)&local, &localLen) == 0 && local.sin_addr.s_addr != 0);
    if (ok) snprintf(out, outSize, "%s", inet_ntoa(local.sin_addr));
    close(fd);
    return ok;
}

// ── Public API ────────────────────────────────────────────────────────────────
bool ftpStart(void) {
    if (g_running) return true;
    mutexInit(&g_mutex);

    if (!getLocalIp(g_ipStr, sizeof(g_ipStr))) {
        nifmInitialize(NifmServiceType_User);
        u32 ipAddr = 0; nifmGetCurrentIpAddress(&ipAddr); nifmExit();
        if (ipAddr == 0) { setStatus("No network. Check Wi-Fi."); return false; }
        struct in_addr ia; ia.s_addr = ipAddr;
        snprintf(g_ipStr, sizeof(g_ipStr), "%s", inet_ntoa(ia));
    }

    g_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenFd < 0) { setStatus("socket() failed."); return false; }
    int opt = 1; setsockopt(g_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(FTP_PORT);
    if (bind(g_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char msg[64]; snprintf(msg, sizeof(msg), "bind() failed: %d", errno);
        setStatus(msg); close(g_listenFd); g_listenFd=-1; return false;
    }
    if (listen(g_listenFd, FTP_MAX_CLIENTS) < 0) {
        setStatus("listen() failed."); close(g_listenFd); g_listenFd=-1; return false;
    }
    fcntl(g_listenFd, F_SETFL, O_NONBLOCK); // non-blocking so ftpPoll's accept() doesn't block

    for (int i = 0; i < FTP_MAX_CLIENTS; i++) {
        memset(&g_clients[i], 0, sizeof(FtpClient));
        g_clients[i].ctrlFd=-1; g_clients[i].dataFd=-1; g_clients[i].passiveListenFd=-1;
    }

    g_running = true; g_filesReceived = 0;

    Result rc = threadCreate(&g_thread, ftpThreadFunc, NULL, NULL, FTP_STACK_SIZE, FTP_THREAD_PRIO, -2);
    if (R_FAILED(rc)) {
        g_running = false; close(g_listenFd); g_listenFd=-1;
        setStatus("threadCreate failed."); return false;
    }
    threadStart(&g_thread);
    return true;
}

void ftpStop(void) {
    if (!g_running) return;
    g_running = false;
    if (g_listenFd >= 0) { close(g_listenFd); g_listenFd=-1; }
    for (int i = 0; i < FTP_MAX_CLIENTS; i++) ftpCloseClient(&g_clients[i]);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    setStatus("FTP stopped.");
}

bool        ftpIsRunning(void)     { return g_running; }
const char* ftpGetIp(void)         { return g_ipStr; }
const char* ftpGetStatus(void)     { return g_status; }
int         ftpGetFilesReceived(void) {
    mutexLock(&g_mutex); int n = g_filesReceived; mutexUnlock(&g_mutex); return n;
}
