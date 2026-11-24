#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#ifdef ENABLE_APPINST
#include <orbis/AppInstUtil.h>
#endif

#define VAULT_DIR "/data/pkg_vault/"
#define USB_DIR   "/mnt/usb0/"
#define MAX_PARTS 100

/* DualShock 4 button bits (values from the OpenOrbis Pad docs) */
#define BTN_CIRCLE 0x2000
#define BTN_CROSS  0x4000

static char io_buf[1024 * 1024];

/* PS4 pop-up notification. Struct layout is the one widely used in PS4
   payloads (written from memory, not checked against the toolchain headers).
   The asm label avoids a clash if libkernel.h declares it differently. */
typedef struct { char unused[45]; char message[3075]; } notify_req_t;
extern int notify_send(int dev, notify_req_t *req, size_t size, int blocking)
    __asm__("sceKernelSendNotificationRequest");

static void notify(const char *msg) {
    static notify_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, msg, sizeof(req.message) - 1);
    notify_send(0, &req, sizeof(req), 0);
}

/* "game.pkg.0", "game.pkg.1" ... but not "game.pkg.0.done" */
static int is_part(const char *name) {
    size_t n = strlen(name);
    if (!strstr(name, ".pkg.")) return 0;
    if (n >= 5 && strcmp(name + n - 5, ".done") == 0) return 0;
    return 1;
}

static int compare_parts(const void *a, const void *b) {
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return atoi(strrchr(s1, '.') + 1) - atoi(strrchr(s2, '.') + 1);
}

static int copy_file(const char *src, const char *dst) {
    FILE *s = fopen(src, "rb");
    if (!s) return -1;
    FILE *d = fopen(dst, "wb");
    if (!d) { fclose(s); return -1; }
    int ok = 1;
    size_t n;
    while ((n = fread(io_buf, 1, sizeof(io_buf), s)) > 0) {
        if (fwrite(io_buf, 1, n, d) != n) { ok = 0; break; }
    }
    if (ferror(s)) ok = 0;
    if (fclose(d) != 0) ok = 0;
    fclose(s);
    if (!ok) remove(dst);
    return ok ? 0 : -1;
}

/* returns 0 if every part was copied (or none found), -1 on a copy failure */
static int scan_usb(void) {
    DIR *dir = opendir(USB_DIR);
    if (!dir) return 0;
    int result = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!is_part(ent->d_name)) continue;
        char src[512], dst[512], renamed[512], msg[300];
        snprintf(src, sizeof(src), "%s%s", USB_DIR, ent->d_name);
        snprintf(dst, sizeof(dst), "%s%s", VAULT_DIR, ent->d_name);
        snprintf(msg, sizeof(msg), "Copying %s to PS4...", ent->d_name);
        notify(msg);
        if (copy_file(src, dst) == 0) {
            snprintf(renamed, sizeof(renamed), "%s%s.done", USB_DIR, ent->d_name);
            rename(src, renamed);
            snprintf(msg, sizeof(msg), "Saved %s to PS4!", ent->d_name);
            notify(msg);
        } else {
            snprintf(msg, sizeof(msg), "Copy FAILED: %s (disk full?)", ent->d_name);
            notify(msg);
            result = -1;
            break;
        }
    }
    closedir(dir);
    return result;
}

static void join_and_install(void) {
    char *parts[MAX_PARTS];
    int count = 0;
    DIR *dir = opendir(VAULT_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (is_part(ent->d_name) && count < MAX_PARTS)
                parts[count++] = strdup(ent->d_name);
        }
        closedir(dir);
    }
    if (count == 0) { notify("No parts found on PS4!"); return; }
    qsort(parts, count, sizeof(char *), compare_parts);

    char final_path[512];
    snprintf(final_path, sizeof(final_path), "%sFULL_GAME.pkg", VAULT_DIR);
    FILE *out = fopen(final_path, "wb");
    if (!out) {
        notify("Error creating final PKG.");
        for (int i = 0; i < count; i++) free(parts[i]);
        return;
    }

    notify("Joining parts... please wait.");
    int ok = 1;
    for (int i = 0; i < count && ok; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", VAULT_DIR, parts[i]);
        FILE *in = fopen(path, "rb");
        if (!in) { ok = 0; break; }
        size_t n;
        while ((n = fread(io_buf, 1, sizeof(io_buf), in)) > 0) {
            if (fwrite(io_buf, 1, n, out) != n) { ok = 0; break; }
        }
        if (ferror(in)) ok = 0;
        fclose(in);
    }
    if (fclose(out) != 0) ok = 0;

    if (!ok) {
        notify("Join FAILED (disk full?). Parts were kept.");
        for (int i = 0; i < count; i++) free(parts[i]);
        return;
    }

    /* delete the parts only after the whole join succeeded */
    for (int i = 0; i < count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", VAULT_DIR, parts[i]);
        remove(path);
        free(parts[i]);
    }

#ifdef ENABLE_APPINST
    notify("Join complete! Installing game...");
    char uri[512];
    snprintf(uri, sizeof(uri), "file://%s", final_path);
    int ret = sceAppInstUtilAppInstallPkg(uri, NULL, NULL);
    notify(ret == 0 ? "Installation started!" : "Install error.");
#else
    notify("Join complete! File: /data/pkg_vault/FULL_GAME.pkg - install it with your Package Installer.");
#endif
}

int main(void) {
    mkdir(VAULT_DIR, 0777);

    sceUserServiceInitialize(NULL);
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    scePadInit();
    int pad = scePadOpen(userId, 0, 0, NULL);

    notify("PKG Vault started. Plug in USB parts. X = join, O = exit.");

    unsigned int prev = 0;
    int tick = 0, next_scan = 0;
    for (;;) {
        OrbisPadData data;
        memset(&data, 0, sizeof(data));
        if (pad >= 0 && scePadReadState(pad, &data) == 0) {
            unsigned int pressed = data.buttons & ~prev;
            prev = data.buttons;
            if (pressed & BTN_CROSS) join_and_install();
            if (pressed & BTN_CIRCLE) { notify("Exiting PKG Vault."); break; }
        }
        if (tick >= next_scan) {
            /* scan every ~2 s; after a failed copy wait ~10 s */
            next_scan = tick + (scan_usb() == 0 ? 40 : 200);
        }
        tick++;
        sceKernelUsleep(50000);
    }
    return 0;
}
