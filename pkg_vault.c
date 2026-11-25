/*
 * PKG Vault - PS4 homebrew
 * Copies split PKG parts from a USB drive to the PS4, joins them on the
 * console and (optionally) installs the game. Controlled with the PS4 pad.
 *
 * Made by OnyxSoul-1 - built with Claude and DeepSeek
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

/* ---- names shown inside the app (change them here) ---- */
#define APP_NAME "PKG Vault"
#define CREDITS  "By OnyxSoul-1  |  Built with Claude and DeepSeek"

#define VAULT_DIR "/data/pkg_vault/"
#define FINAL_PKG "/data/pkg_vault/FULL_GAME.pkg"
#define LOG_PATH  "/data/pkg_vault_log.txt"
#define MANUAL_USB_PATH "/data/pkg_vault/manual_usb.txt"  /* saved manual USB path */
#define MAX_PARTS 100
#define MAX_BROWSE 256   /* max entries shown in folder browser */

#define W 1920
#define H 1080
#define FB_BYTES (W * H * 4)

/* DualShock 4 button bits (values from the OpenOrbis Pad docs) */
#define BTN_UP       0x0010
#define BTN_DOWN     0x0040
#define BTN_TRIANGLE 0x1000   /* used in folder browser to confirm selection */
#define BTN_CIRCLE   0x2000
#define BTN_CROSS    0x4000

/* ---- system functions declared here with asm labels, so they cannot
        clash with whatever the toolchain headers say ---- */
/* Fixed: canonical OrbisNotificationRequest layout (3344 bytes total).
   Old layout was 3120 bytes with message at offset 45, which wrote the
   message text into the middle of what the kernel reads as iconPath. */
typedef struct {
    unsigned int type;       /* offset 0  */
    unsigned int unk;        /* offset 4  */
    unsigned int useIcon;    /* offset 8  */
    char         iconPath[256]; /* offset 12 */
    char         message[3072]; /* offset 268 */
} notify_req_t;
extern int notify_send(int dev, notify_req_t *req, size_t size, int blocking)
    __asm__("sceKernelSendNotificationRequest");
extern int vo_open(int user, int type, int index, const void *param)
    __asm__("sceVideoOutOpen");
extern void vo_set_attr(void *attr, unsigned fmt, unsigned tiling, unsigned aspect,
                        unsigned w, unsigned h, unsigned pitch)
    __asm__("sceVideoOutSetBufferAttribute");
extern int vo_register(int h, int start, void *const *addrs, int num, const void *attr)
    __asm__("sceVideoOutRegisterBuffers");
extern int vo_flip(int h, int idx, unsigned mode, long long arg)
    __asm__("sceVideoOutSubmitFlip");
extern int vo_flip_rate(int h, int rate)
    __asm__("sceVideoOutSetFlipRate");
extern long long dm_size(void)
    __asm__("sceKernelGetDirectMemorySize");
extern int dm_alloc(long long start, long long end, size_t len, size_t align,
                    int type, long long *phys)
    __asm__("sceKernelAllocateDirectMemory");
extern int dm_map(void **addr, size_t len, int prot, int flags, long long phys, size_t align)
    __asm__("sceKernelMapDirectMemory");
extern int hide_splash(void)
    __asm__("sceSystemServiceHideSplashScreen") __attribute__((weak));

/* ---- 8x16 bitmap font, ASCII 32..126 (generated from DejaVu Sans Mono Bold) ---- */
static const unsigned char font8x16[95][16] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32   */
  {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 33 ! */
  {0x00,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 " */
  {0x00,0x1B,0x1B,0x12,0x7F,0x36,0x34,0xFF,0x6C,0x6C,0x68,0x00,0x00,0x00,0x00,0x00}, /* 35 # */
  {0x08,0x08,0x3E,0x7A,0x68,0x7C,0x1E,0x0E,0x4F,0x6E,0x3C,0x08,0x08,0x00,0x00,0x00}, /* 36 $ */
  {0x00,0x70,0xD8,0xD8,0x73,0x0C,0x30,0x47,0x0D,0x0D,0x07,0x00,0x00,0x00,0x00,0x00}, /* 37 % */
  {0x00,0x3C,0x30,0x70,0x30,0x78,0x79,0xCF,0xCF,0x67,0x3F,0x00,0x00,0x00,0x00,0x00}, /* 38 & */
  {0x00,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 39 ' */
  {0x0C,0x0C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x0C,0x0C,0x00,0x00,0x00,0x00}, /* 40 ( */
  {0x30,0x18,0x18,0x1C,0x0C,0x0C,0x0C,0x0C,0x1C,0x18,0x18,0x30,0x00,0x00,0x00,0x00}, /* 41 ) */
  {0x00,0x18,0x7A,0x3E,0x3E,0x7A,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 42 ? */
  {0x00,0x00,0x18,0x18,0x18,0xFF,0xFF,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, /* 43 + */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x10,0x00,0x00,0x00,0x00}, /* 44 , */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x3C,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 45 - */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 46 . */
  {0x00,0x03,0x06,0x06,0x0C,0x0C,0x08,0x18,0x10,0x30,0x30,0x60,0x60,0x00,0x00,0x00}, /* 47 ? */
  {0x00,0x3C,0x76,0x66,0x67,0x7F,0x7F,0x67,0x66,0x76,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 48 0 */
  {0x00,0x3C,0x7C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 49 1 */
  {0x00,0x3C,0x46,0x06,0x06,0x0E,0x0C,0x18,0x38,0x70,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 50 2 */
  {0x00,0x3C,0x46,0x06,0x06,0x1C,0x06,0x07,0x07,0x46,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 51 3 */
  {0x00,0x0E,0x1E,0x1E,0x36,0x66,0x66,0x7F,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x00}, /* 52 4 */
  {0x00,0x7E,0x60,0x60,0x7C,0x46,0x06,0x07,0x06,0x46,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 53 5 */
  {0x00,0x1E,0x32,0x60,0x7E,0x76,0x63,0x63,0x63,0x76,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 54 6 */
  {0x00,0x7E,0x06,0x06,0x0E,0x0C,0x1C,0x18,0x18,0x38,0x30,0x00,0x00,0x00,0x00,0x00}, /* 55 7 */
  {0x00,0x3C,0x66,0x66,0x66,0x3C,0x66,0x63,0x63,0x66,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 56 8 */
  {0x00,0x3C,0x66,0x66,0x67,0x67,0x67,0x3F,0x06,0x46,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 57 9 */
  {0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 58 : */
  {0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x10,0x00,0x00,0x00,0x00}, /* 59 ; */
  {0x00,0x00,0x00,0x03,0x0F,0x7C,0xE0,0x7C,0x0F,0x03,0x00,0x00,0x00,0x00,0x00,0x00}, /* 60 < */
  {0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 61 = */
  {0x00,0x00,0x00,0x40,0x78,0x1E,0x07,0x1E,0x78,0x40,0x00,0x00,0x00,0x00,0x00,0x00}, /* 62 > */
  {0x00,0x3C,0x26,0x06,0x0E,0x1C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 63 ? */
  {0x00,0x3E,0x63,0x43,0xCF,0xDB,0xD3,0xD3,0xDB,0xCF,0x40,0x33,0x1F,0x00,0x00,0x00}, /* 64 @ */
  {0x00,0x1C,0x3C,0x3C,0x3E,0x36,0x66,0x7E,0x63,0x63,0xC3,0x00,0x00,0x00,0x00,0x00}, /* 65 A */
  {0x00,0x7E,0x66,0x67,0x66,0x7E,0x67,0x63,0x63,0x67,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 66 B */
  {0x00,0x1E,0x30,0x70,0x60,0x60,0x60,0x60,0x70,0x30,0x1E,0x00,0x00,0x00,0x00,0x00}, /* 67 C */
  {0x00,0x7C,0x66,0x67,0x63,0x63,0x63,0x63,0x67,0x66,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 68 D */
  {0x00,0x7F,0x60,0x60,0x60,0x7E,0x60,0x60,0x60,0x60,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 69 E */
  {0x00,0x7F,0x60,0x60,0x60,0x7E,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00}, /* 70 F */
  {0x00,0x1E,0x30,0x60,0x60,0x60,0x6F,0x63,0x63,0x33,0x1F,0x00,0x00,0x00,0x00,0x00}, /* 71 G */
  {0x00,0x67,0x67,0x67,0x67,0x7F,0x67,0x67,0x67,0x67,0x67,0x00,0x00,0x00,0x00,0x00}, /* 72 H */
  {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 73 I */
  {0x00,0x3E,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x4E,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 74 J */
  {0x00,0x67,0x66,0x6C,0x7C,0x78,0x7C,0x6E,0x66,0x67,0x63,0x00,0x00,0x00,0x00,0x00}, /* 75 K */
  {0x00,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 76 L */
  {0x00,0xE7,0xF7,0xF7,0xFF,0xDF,0xDB,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00}, /* 77 M */
  {0x00,0x63,0x73,0x73,0x73,0x7B,0x6B,0x6F,0x6F,0x67,0x67,0x00,0x00,0x00,0x00,0x00}, /* 78 N */
  {0x00,0x3C,0x76,0x67,0x63,0x63,0x63,0x63,0x67,0x76,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 79 O */
  {0x00,0x7E,0x67,0x63,0x63,0x67,0x7E,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00}, /* 80 P */
  {0x00,0x3C,0x76,0x67,0x63,0x63,0x63,0x63,0x67,0x76,0x3E,0x06,0x02,0x00,0x00,0x00}, /* 81 Q */
  {0x00,0x7C,0x66,0x67,0x67,0x66,0x7C,0x6E,0x66,0x67,0x63,0x00,0x00,0x00,0x00,0x00}, /* 82 R */
  {0x00,0x3C,0x62,0x60,0x70,0x7C,0x1E,0x07,0x07,0x46,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 83 S */
  {0x00,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 84 T */
  {0x00,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x66,0x3E,0x00,0x00,0x00,0x00,0x00}, /* 85 U */
  {0x00,0xE3,0x63,0x67,0x66,0x66,0x36,0x36,0x3C,0x3C,0x1C,0x00,0x00,0x00,0x00,0x00}, /* 86 V */
  {0x00,0xC1,0xC3,0xC3,0xDB,0xDB,0x7F,0x7F,0x77,0x77,0x66,0x00,0x00,0x00,0x00,0x00}, /* 87 W */
  {0x00,0xE3,0x66,0x3E,0x3C,0x1C,0x1C,0x3C,0x36,0x66,0xE3,0x00,0x00,0x00,0x00,0x00}, /* 88 X */
  {0x00,0xE3,0x67,0x76,0x3E,0x3C,0x1C,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 89 Y */
  {0x00,0x7F,0x07,0x0E,0x0E,0x1C,0x3C,0x38,0x70,0x70,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 90 Z */
  {0x1C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1C,0x00,0x00,0x00,0x00}, /* 91 [ */
  {0x00,0x60,0x60,0x30,0x30,0x10,0x18,0x08,0x0C,0x0C,0x06,0x06,0x03,0x00,0x00,0x00}, /* 92 ? */
  {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00}, /* 93 ] */
  {0x00,0x1C,0x3C,0x76,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 94 ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00}, /* 95 _ */
  {0x10,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 96 ` */
  {0x00,0x00,0x00,0x3E,0x66,0x07,0x3F,0x67,0x67,0x67,0x3F,0x00,0x00,0x00,0x00,0x00}, /* 97 a */
  {0x60,0x60,0x60,0x7E,0x76,0x63,0x63,0x63,0x63,0x76,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 98 b */
  {0x00,0x00,0x00,0x1E,0x32,0x70,0x60,0x60,0x70,0x32,0x1E,0x00,0x00,0x00,0x00,0x00}, /* 99 c */
  {0x07,0x07,0x07,0x3F,0x67,0x67,0x67,0x67,0x67,0x67,0x3F,0x00,0x00,0x00,0x00,0x00}, /* 100 d */
  {0x00,0x00,0x00,0x3C,0x66,0x63,0x7F,0x60,0x60,0x73,0x3E,0x00,0x00,0x00,0x00,0x00}, /* 101 e */
  {0x0F,0x18,0x18,0x7F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 102 f */
  {0x00,0x00,0x00,0x3F,0x67,0x67,0x67,0x67,0x67,0x67,0x3F,0x06,0x46,0x3C,0x00,0x00}, /* 103 g */
  {0x60,0x60,0x60,0x7E,0x76,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00}, /* 104 h */
  {0x1C,0x1C,0x00,0x7C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 105 i */
  {0x0C,0x0C,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x1C,0x78,0x00,0x00}, /* 106 j */
  {0x60,0x60,0x60,0x67,0x6E,0x7C,0x7C,0x7C,0x6E,0x66,0x63,0x00,0x00,0x00,0x00,0x00}, /* 107 k */
  {0xF8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1F,0x00,0x00,0x00,0x00,0x00}, /* 108 l */
  {0x00,0x00,0x00,0xFE,0xDB,0xDB,0xDB,0xDB,0xDB,0xDB,0xDB,0x00,0x00,0x00,0x00,0x00}, /* 109 m */
  {0x00,0x00,0x00,0x7E,0x76,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00}, /* 110 n */
  {0x00,0x00,0x00,0x3C,0x66,0x67,0x63,0x63,0x67,0x66,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 111 o */
  {0x00,0x00,0x00,0x7E,0x76,0x63,0x63,0x63,0x63,0x76,0x7E,0x60,0x60,0x60,0x00,0x00}, /* 112 p */
  {0x00,0x00,0x00,0x3F,0x67,0x67,0x67,0x67,0x67,0x67,0x3F,0x07,0x07,0x07,0x00,0x00}, /* 113 q */
  {0x00,0x00,0x00,0x3F,0x39,0x30,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00}, /* 114 r */
  {0x00,0x00,0x00,0x3C,0x62,0x70,0x7C,0x1E,0x06,0x46,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 115 s */
  {0x00,0x18,0x18,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x1E,0x00,0x00,0x00,0x00,0x00}, /* 116 t */
  {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00,0x00}, /* 117 u */
  {0x00,0x00,0x00,0x63,0x67,0x66,0x76,0x36,0x3C,0x3C,0x1C,0x00,0x00,0x00,0x00,0x00}, /* 118 v */
  {0x00,0x00,0x00,0xC1,0xC3,0xDB,0xDB,0x7F,0x7F,0x76,0x76,0x00,0x00,0x00,0x00,0x00}, /* 119 w */
  {0x00,0x00,0x00,0x67,0x36,0x3C,0x1C,0x1C,0x3C,0x76,0x67,0x00,0x00,0x00,0x00,0x00}, /* 120 x */
  {0x00,0x00,0x00,0xE3,0x67,0x66,0x36,0x3E,0x3C,0x1C,0x1C,0x18,0x18,0x70,0x00,0x00}, /* 121 y */
  {0x00,0x00,0x00,0x7F,0x06,0x0E,0x1C,0x38,0x38,0x70,0x7F,0x00,0x00,0x00,0x00,0x00}, /* 122 z */
  {0x0E,0x1C,0x18,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x1C,0x0E,0x00,0x00,0x00}, /* 123 { */
  {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00}, /* 124 | */
  {0x78,0x18,0x18,0x18,0x18,0x1C,0x0E,0x1C,0x18,0x18,0x18,0x18,0x78,0x00,0x00,0x00}, /* 125 } */
  {0x00,0x00,0x00,0x00,0x00,0x79,0xFF,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 ~ */
};

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
typedef struct { char name[256]; long long size; } part_t;

static char io_buf[1024 * 1024];
static part_t vault_parts[MAX_PARTS];
static int vault_count = 0;
static char usb_dir[512] = "";          /* bumped to 512 — can hold deep paths */
static int usb_count = 0;
static char status[160] = "Ready. Plug in the USB with a part and choose Copy.";
static const char *menu[8];
static int menu_action[8];
static int menu_n = 0;
static int sel = 0;
static long long prog_cur = 0, prog_total = 0;

/* Folder browser state */
static int browsing = 0;                         /* 1 when browser is open */
static char browse_path[512] = "/";              /* current path being shown */
static char browse_entries[MAX_BROWSE][256];     /* entry names */
static int  browse_isdir[MAX_BROWSE];            /* 1 if folder, 0 if file */
static int  browse_count = 0;                    /* how many entries loaded */
static int  browse_sel = 0;                      /* selected index in browser */
static char manual_usb_dir[512] = "";            /* user-picked USB path */
static int  manual_usb_count = 0;                /* parts count in manual path */

enum { A_COPY, A_JOIN, A_DELETE, A_INSTALL, A_BROWSE, A_EXIT };

static unsigned int *fb[2];
static int vo = -1;
static int back = 0;

static void log_line(const char *m) {
    FILE *f = fopen(LOG_PATH, "a");
    if (f) { fprintf(f, "%s\n", m); fclose(f); }
}

/* Save / load the manual USB path so it survives app restarts. */
static void save_manual_usb(void) {
    FILE *f = fopen(MANUAL_USB_PATH, "w");
    if (f) {
        fprintf(f, "%s", manual_usb_dir);
        fclose(f);
    }
}
static void load_manual_usb(void) {
    FILE *f = fopen(MANUAL_USB_PATH, "r");
    if (f) {
        if (fgets(manual_usb_dir, sizeof(manual_usb_dir), f)) {
            /* strip trailing newline */
            size_t n = strlen(manual_usb_dir);
            while (n > 0 && (manual_usb_dir[n-1] == '\n' || manual_usb_dir[n-1] == '\r')) {
                manual_usb_dir[--n] = 0;
            }
        }
        fclose(f);
    }
}

static void notify(const char *msg) {
    static notify_req_t req;
    memset(&req, 0, sizeof(req));
    req.type    = 0;            /* simple text notification */
    req.useIcon = 0;            /* no icon */
    req.iconPath[0] = 0;        /* empty icon path */
    strncpy(req.message, msg, sizeof(req.message) - 1);
    notify_send(0, &req, sizeof(req), 0);
}

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
    log_line(status);
}

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
static int init_video(void) {
    vo = vo_open(0xFF, 0, 0, NULL);
    if (vo < 0) return -1;
    size_t total = 0x1000000; /* 16 MiB, room for two 1920x1080 buffers */
    long long phys = 0;
    if (dm_alloc(0, dm_size(), total, 0x200000, 3, &phys) != 0) return -2;
    void *va = NULL;
    if (dm_map(&va, total, 0x33, 0, phys, 0x200000) != 0) return -3;
    fb[0] = (unsigned int *)va;
    fb[1] = (unsigned int *)((char *)va + FB_BYTES);
    char attr[128];
    memset(attr, 0, sizeof(attr));
    vo_set_attr(attr, 0x80000000u, 1, 0, W, H, W); /* BGRA, linear, 16:9 */
    void *addrs[2] = { fb[0], fb[1] };
    if (vo_register(vo, 0, addrs, 2, attr) != 0) return -4;
    vo_flip_rate(vo, 0);
    return 0;
}

static void fill_rect(int x, int y, int w, int h, unsigned int c);
static void draw_text(int x, int y, int s, unsigned int c, const char *t);
static void draw_ui(void);
static void draw_browser(void);
static void ui_redraw(void);
static void refresh_state(void);
static void browse_load(const char *path);
static int  count_parts_in(const char *path);
static int  browse_up(void);
static void browse_enter(void);
static void browse_confirm(void);
static void set_status(const char *fmt, ...);
static void log_line(const char *m);

static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    unsigned int *b = fb[back];
    for (int j = 0; j < h; j++) {
        unsigned int *row = b + (size_t)(y + j) * W + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static void draw_char(int x, int y, int s, unsigned int c, char ch) {
    if (ch < 32 || ch > 126) ch = '?';
    const unsigned char *g = font8x16[ch - 32];
    for (int r = 0; r < 16; r++)
        for (int k = 0; k < 8; k++)
            if (g[r] & (0x80 >> k)) fill_rect(x + k * s, y + r * s, s, s, c);
}

static void draw_text(int x, int y, int s, unsigned int c, const char *t) {
    for (; *t; t++) { draw_char(x, y, s, c, *t); x += 8 * s; }
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
/* "game.pkg.0", "game.pkg.1" ... but not "game.pkg.0.done" */
static int is_part(const char *name) {
    size_t n = strlen(name);
    if (!strstr(name, ".pkg.")) return 0;
    if (n >= 5 && strcmp(name + n - 5, ".done") == 0) return 0;
    return 1;
}

static int part_num(const char *s) {
    const char *dot = strrchr(s, '.');
    return dot ? atoi(dot + 1) : 0;
}

static int cmp_part(const void *a, const void *b) {
    return part_num(((const part_t *)a)->name) - part_num(((const part_t *)b)->name);
}

/* Try one path. Returns 1 if opendir() succeeded, 0 if it failed.
   On success, *cnt_out is set to the number of PKG parts found.
   Every attempt is written to the log file so we can debug
   "USB: not found" problems. */
static int try_usb_path(const char *p, int *cnt_out) {
    if (cnt_out) *cnt_out = 0;
    DIR *u = opendir(p);
    if (!u) return 0;
    char m[160];
    int cnt = 0;
    struct dirent *e;
    while ((e = readdir(u)) != NULL) {
        if (!e->d_name || !e->d_name[0]) continue;   /* skip bogus entries */
        if (is_part(e->d_name)) cnt++;
    }
    closedir(u);
    snprintf(m, sizeof(m), "USB scan OK: %s (%d part(s))", p, cnt);
    log_line(m);
    if (cnt_out) *cnt_out = cnt;
    return 1;
}

static void refresh_state(void) {
    vault_count = 0;
    DIR *d = opendir(VAULT_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && vault_count < MAX_PARTS) {
            if (!is_part(e->d_name)) continue;
            part_t *p = &vault_parts[vault_count++];
            snprintf(p->name, sizeof(p->name), "%s", e->d_name);
            char path[512];
            snprintf(path, sizeof(path), "%s%s", VAULT_DIR, e->d_name);
            struct stat st;
            p->size = (stat(path, &st) == 0) ? (long long)st.st_size : 0;
        }
        closedir(d);
    }
    qsort(vault_parts, vault_count, sizeof(part_t), cmp_part);

    /* If the user has picked a manual USB path, use it. We still count
       parts in it so the UI shows "N part(s) waiting". */
    if (manual_usb_dir[0]) {
        snprintf(usb_dir, sizeof(usb_dir), "%s", manual_usb_dir);
        usb_count = 0;
        DIR *u = opendir(usb_dir);
        if (u) {
            struct dirent *e;
            while ((e = readdir(u)) != NULL) {
                if (!e->d_name || !e->d_name[0]) continue;
                if (is_part(e->d_name)) usb_count++;
            }
            closedir(u);
        }
        manual_usb_count = usb_count;
        return;
    }

    /* Otherwise auto-scan many possible USB mount points.
       PS4 normally mounts USBs at /mnt/usb0..7, but some GoldHEN / firmware
       combos use different paths. We try them all and log every attempt.
       Strategy: prefer the first path that HAS parts; otherwise remember
       the first path that at least opened (even if empty) so the UI can
       show "USB: found, no parts" instead of "USB: not found". */
    usb_dir[0] = 0;
    usb_count = 0;
    char first_open_path[64] = "";  /* first path opendir() succeeded on */

    /* Pass 1: /mnt/usb0 .. /mnt/usb7 (standard PS4 path) */
    for (int i = 0; i < 8; i++) {
        char p[64];
        int cnt = 0;
        snprintf(p, sizeof(p), "/mnt/usb%d/", i);
        if (try_usb_path(p, &cnt)) {
            if (!first_open_path[0]) snprintf(first_open_path, sizeof(first_open_path), "%s", p);
            if (cnt > 0) { snprintf(usb_dir, sizeof(usb_dir), "%s", p); usb_count = cnt; goto done; }
        }
    }

    /* Pass 2: /data/usb0 .. /data/usb7 (some GoldHEN builds use this) */
    for (int i = 0; i < 8; i++) {
        char p[64];
        int cnt = 0;
        snprintf(p, sizeof(p), "/data/usb%d/", i);
        if (try_usb_path(p, &cnt)) {
            if (!first_open_path[0]) snprintf(first_open_path, sizeof(first_open_path), "%s", p);
            if (cnt > 0) { snprintf(usb_dir, sizeof(usb_dir), "%s", p); usb_count = cnt; goto done; }
        }
    }

    /* Pass 3: single-numbered folders that some firmwares use */
    {
        const char *single[] = {
            "/mnt/usb/", "/data/usb/", "/usb0/", "/usb1/", "/usb/",
            "/data/usb0/", "/data/usb1/", NULL
        };
        for (int i = 0; single[i]; i++) {
            int cnt = 0;
            if (try_usb_path(single[i], &cnt)) {
                if (!first_open_path[0]) snprintf(first_open_path, sizeof(first_open_path), "%s", single[i]);
                if (cnt > 0) { snprintf(usb_dir, sizeof(usb_dir), "%s", single[i]); usb_count = cnt; goto done; }
            }
        }
    }

    /* Nothing had parts. But if at least one path opened, use it so the
       UI shows "USB: found, no parts" — that tells us the USB is mounted,
       just empty or with the wrong file names. */
    if (!usb_dir[0] && first_open_path[0]) {
        snprintf(usb_dir, sizeof(usb_dir), "%s", first_open_path);
    }

done:
    ;
}

/* ------------------------------------------------------------------ */
/* folder browser                                                      */
/* ------------------------------------------------------------------ */
/* Load entries for the folder browser. Fills browse_entries / browse_isdir.
   Folders come first (sorted alphabetically), then files (sorted alphabetically).
   The special entry ".." is added at the top so the user can always go up. */
static void browse_load(const char *path) {
    browse_count = 0;
    browse_sel = 0;
    snprintf(browse_path, sizeof(browse_path), "%s", path);

    /* First entry is always ".." for going up */
    snprintf(browse_entries[0], sizeof(browse_entries[0]), "..");
    browse_isdir[0] = 1;
    browse_count = 1;

    DIR *d = opendir(path);
    if (!d) {
        char m[256];
        snprintf(m, sizeof(m), "browse: cannot open %s", path);
        log_line(m);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && browse_count < MAX_BROWSE) {
        if (!e->d_name || !e->d_name[0]) continue;
        if (strcmp(e->d_name, ".") == 0) continue;
        if (strcmp(e->d_name, "..") == 0) continue;

        char full[768];
        snprintf(full, sizeof(full), "%s%s", path, e->d_name);
        struct stat st;
        int is_dir = 0;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }
        snprintf(browse_entries[browse_count], sizeof(browse_entries[0]), "%s", e->d_name);
        browse_isdir[browse_count] = is_dir;
        browse_count++;
    }
    closedir(d);

    /* Simple sort: folders first (after the leading ".."), then by name.
       Bubble sort is fine here — counts are small. */
    for (int i = 1; i < browse_count; i++) {
        for (int j = i + 1; j < browse_count; j++) {
            int i_dir = browse_isdir[i], j_dir = browse_isdir[j];
            int swap = 0;
            if (!i_dir && j_dir) swap = 1;
            else if (i_dir == j_dir && strcmp(browse_entries[i], browse_entries[j]) > 0) swap = 1;
            if (swap) {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s", browse_entries[i]);
                snprintf(browse_entries[i], sizeof(browse_entries[0]), "%s", browse_entries[j]);
                snprintf(browse_entries[j], sizeof(browse_entries[0]), "%s", tmp);
                int t = browse_isdir[i]; browse_isdir[i] = browse_isdir[j]; browse_isdir[j] = t;
            }
        }
    }
}

/* Count PKG parts in a folder. Returns -1 if the folder can't be opened. */
static int count_parts_in(const char *path) {
    DIR *u = opendir(path);
    if (!u) return -1;
    int cnt = 0;
    struct dirent *e;
    while ((e = readdir(u)) != NULL) {
        if (!e->d_name || !e->d_name[0]) continue;
        if (is_part(e->d_name)) cnt++;
    }
    closedir(u);
    return cnt;
}

/* Go up one level in the browser. Returns 1 if it moved up, 0 if at root. */
static int browse_up(void) {
    if (strcmp(browse_path, "/") == 0) return 0;
    /* find last '/' that is not the trailing one */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", browse_path);
    size_t n = strlen(tmp);
    /* strip trailing '/' */
    if (n > 1 && tmp[n-1] == '/') tmp[--n] = 0;
    /* find last '/' */
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        snprintf(tmp, sizeof(tmp), "/");
    } else {
        if (slash == tmp) slash[1] = 0;  /* keep root as "/" */
        else *slash = 0;
        if (!tmp[0]) snprintf(tmp, sizeof(tmp), "/");
    }
    /* make sure it ends with '/' */
    size_t m = strlen(tmp);
    if (m > 0 && tmp[m-1] != '/') {
        if (m + 1 < sizeof(tmp)) { tmp[m] = '/'; tmp[m+1] = 0; }
    }
    browse_load(tmp);
    return 1;
}

/* Enter the currently selected folder. */
static void browse_enter(void) {
    if (browse_sel < 0 || browse_sel >= browse_count) return;
    if (!browse_isdir[browse_sel]) return;  /* can't enter a file */
    if (strcmp(browse_entries[browse_sel], "..") == 0) {
        browse_up();
        return;
    }
    char newpath[768];
    snprintf(newpath, sizeof(newpath), "%s%s/", browse_path, browse_entries[browse_sel]);
    /* try to open it — if it fails, stay put */
    DIR *test = opendir(newpath);
    if (!test) {
        char m[300];
        snprintf(m, sizeof(m), "browse: cannot enter %s", newpath);
        log_line(m);
        return;
    }
    closedir(test);
    browse_load(newpath);
}

/* Use the current folder as the USB path. */
static void browse_confirm(void) {
    /* count parts in current folder */
    int cnt = count_parts_in(browse_path);
    if (cnt < 0) cnt = 0;
    snprintf(manual_usb_dir, sizeof(manual_usb_dir), "%s", browse_path);
    manual_usb_count = cnt;
    save_manual_usb();
    browsing = 0;
    char m[600];
    snprintf(m, sizeof(m), "USB path set to: %s (%d part(s))", manual_usb_dir, cnt);
    set_status("USB path set to: %s (%d parts)", manual_usb_dir, cnt);
    log_line(m);
    refresh_state();
}

/* ------------------------------------------------------------------ */
/* screen                                                              */
/* ------------------------------------------------------------------ */
static void draw_ui(void) {
    char line[200];
    fill_rect(0, 0, W, H, 0xFF0E1530);
    fill_rect(0, 0, W, 170, 0xFF1B2A55);
    fill_rect(0, 170, W, 4, 0xFF5B8CFF);
    draw_text(90, 28, 5, 0xFFFFFFFF, APP_NAME);
    draw_text(90, 122, 2, 0xFFA8C8FF, CREDITS);

    draw_text(90, 205, 2, 0xFF7F9BD6, "MENU");
    for (int i = 0; i < menu_n; i++) {
        int y = 250 + i * 72;
        if (i == sel) fill_rect(80, y - 8, 840, 64, 0xFF2F55C0);
        draw_text(100, y, 3, i == sel ? 0xFFFFFFFF : 0xFFB8C8F0, menu[i]);
    }

    int ix = 1000;
    draw_text(ix, 205, 2, 0xFF7F9BD6, "STATUS");
    snprintf(line, sizeof(line), "Saved on PS4: %d part(s)", vault_count);
    draw_text(ix, 250, 3, 0xFFFFFFFF, line);
    int shown = vault_count < 8 ? vault_count : 8;
    for (int i = 0; i < shown; i++) {
        snprintf(line, sizeof(line), "%.30s  %lld MB", vault_parts[i].name, vault_parts[i].size >> 20);
        draw_text(ix, 310 + i * 36, 2, 0xFFA8C8FF, line);
    }
    if (vault_count > shown) {
        snprintf(line, sizeof(line), "+ %d more", vault_count - shown);
        draw_text(ix, 310 + shown * 36, 2, 0xFFA8C8FF, line);
    }
    if (usb_dir[0] && usb_count > 0)
        snprintf(line, sizeof(line), "USB: %d part(s) waiting", usb_count);
    else if (usb_dir[0])
        snprintf(line, sizeof(line), "USB: found, no parts");
    else
        snprintf(line, sizeof(line), "USB: not found");
    draw_text(ix, 650, 3, 0xFFFFFFFF, line);
    if (usb_dir[0]) draw_text(ix, 704, 2, 0xFF7F9BD6, usb_dir);

    struct stat st;
    if (stat(FINAL_PKG, &st) == 0)
        snprintf(line, sizeof(line), "FULL_GAME.pkg: %lld MB", (long long)(st.st_size >> 20));
    else
        snprintf(line, sizeof(line), "FULL_GAME.pkg: not created yet");
    draw_text(ix, 760, 3, 0xFFFFFFFF, line);

    fill_rect(0, 870, W, 210, 0xFF141E40);
    snprintf(line, sizeof(line), "%.70s", status);
    draw_text(90, 892, 3, 0xFFFFE070, line);
    if (prog_total > 0) {
        int bw = 1740;
        long long done = prog_cur > prog_total ? prog_total : prog_cur;
        fill_rect(90, 960, bw, 26, 0xFF28385F);
        fill_rect(90, 960, (int)((long long)bw * done / prog_total), 26, 0xFF4CC38A);
    }
    draw_text(90, 1006, 2, 0xFF9FB4E8, "D-pad: move    X: select    O: exit");
}

static void ui_redraw(void) {
    if (vo < 0) return;
    if (browsing) draw_browser();
    else draw_ui();
    vo_flip(vo, back, 1, 0);
    back ^= 1;
}

/* Draw the folder browser. Shown full-screen instead of the main menu. */
static void draw_browser(void) {
    char line[600];
    fill_rect(0, 0, W, H, 0xFF0E1530);
    fill_rect(0, 0, W, 170, 0xFF1B2A55);
    fill_rect(0, 170, W, 4, 0xFF5B8CFF);
    draw_text(90, 28, 5, 0xFFFFFFFF, "BROWSE FOR USB FOLDER");
    draw_text(90, 122, 2, 0xFFA8C8FF, "Find the folder that has your game.pkg.0 file");

    /* Current path */
    fill_rect(0, 174, W, 70, 0xFF141E40);
    snprintf(line, sizeof(line), "Path: %s", browse_path);
    draw_text(90, 200, 2, 0xFFFFE070, line);

    /* Show how many parts are in the current folder, if any */
    int cur_parts = count_parts_in(browse_path);
    if (cur_parts > 0) {
        snprintf(line, sizeof(line), "Found %d part(s) here! Press TRIANGLE to use this folder.", cur_parts);
        draw_text(90, 250, 3, 0xFF4CC38A, line);
    } else {
        draw_text(90, 250, 2, 0xFFB8C8F0, "No parts here yet. Keep looking, or press TRIANGLE anyway.");
    }

    /* Entry list */
    int top = browse_sel - 12;
    if (top < 0) top = 0;
    if (top > browse_count - 20 && browse_count > 20) top = browse_count - 20;
    int shown = browse_count - top;
    if (shown > 20) shown = 20;
    for (int i = 0; i < shown; i++) {
        int idx = top + i;
        int y = 320 + i * 32;
        if (idx == browse_sel) fill_rect(80, y - 4, 1760, 28, 0xFF2F55C0);
        const char *marker = browse_isdir[idx] ? "[DIR]  " : "[FILE] ";
        unsigned int color = browse_isdir[idx] ? 0xFFFFE070 : 0xFFA8C8FF;
        /* truncate name to fit */
        char display[80];
        snprintf(display, sizeof(display), "%.76s", browse_entries[idx]);
        char row[100];
        snprintf(row, sizeof(row), "%s%s", marker, display);
        draw_text(100, y, 2, idx == browse_sel ? 0xFFFFFFFF : color, row);
    }

    /* Footer with controls */
    fill_rect(0, 1000, W, 80, 0xFF141E40);
    draw_text(90, 1024, 2, 0xFF9FB4E8,
        "D-pad: move   X: enter folder   TRIANGLE: use this folder   O: back");
    if (manual_usb_dir[0]) {
        snprintf(line, sizeof(line), "Current USB: %s", manual_usb_dir);
        draw_text(900, 1024, 2, 0xFF7F9BD6, line);
    }
}

/* ------------------------------------------------------------------ */
/* actions                                                             */
/* ------------------------------------------------------------------ */
static int copy_file(const char *src, const char *dst, long long total) {
    FILE *s = fopen(src, "rb");
    if (!s) return -1;
    FILE *d = fopen(dst, "wb");
    if (!d) { fclose(s); return -1; }
    int ok = 1, chunks = 0;
    size_t n;
    prog_cur = 0;
    prog_total = total > 0 ? total : 1;
    while ((n = fread(io_buf, 1, sizeof(io_buf), s)) > 0) {
        if (fwrite(io_buf, 1, n, d) != n) { ok = 0; break; }
        prog_cur += (long long)n;
        if (++chunks % 16 == 0) ui_redraw();
    }
    if (ferror(s)) ok = 0;
    if (fclose(d) != 0) ok = 0;
    fclose(s);
    if (!ok) remove(dst);
    return ok ? 0 : -1;
}

static void do_copy(void) {
    static char names[MAX_PARTS][256];
    int n = 0;
    refresh_state();
    if (!usb_dir[0]) { set_status("No USB found. Plug it in and try again."); return; }
    if (usb_count == 0) { set_status("No parts (like game.pkg.0) found on the USB."); return; }

    DIR *u = opendir(usb_dir);
    if (!u) { set_status("Cannot open the USB."); return; }
    struct dirent *e;
    while ((e = readdir(u)) != NULL && n < MAX_PARTS)
        if (is_part(e->d_name)) snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
    closedir(u);

    int copied = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        char src[512], dst[512], renamed[512];
        snprintf(src, sizeof(src), "%s%s", usb_dir, names[i]);
        snprintf(dst, sizeof(dst), "%s%s", VAULT_DIR, names[i]);
        struct stat st;
        long long size = (stat(src, &st) == 0) ? (long long)st.st_size : 0;
        set_status("Copying %s (%d of %d)...", names[i], i + 1, n);
        ui_redraw();
        if (copy_file(src, dst, size) == 0) {
            snprintf(renamed, sizeof(renamed), "%s%s.done", usb_dir, names[i]);
            rename(src, renamed);
            copied++;
        } else {
            failed = 1;
            break;
        }
    }
    prog_total = 0;
    if (failed) set_status("Copy FAILED (disk full or USB removed?). Try again.");
    else set_status("Copied %d part(s) to the PS4. Plug in the next one.", copied);
    refresh_state();
}

static void do_join(void) {
    refresh_state();
    if (vault_count == 0) { set_status("No parts saved on the PS4 yet."); return; }

    FILE *out = fopen(FINAL_PKG, "wb");
    if (!out) { set_status("Cannot create FULL_GAME.pkg."); return; }

    long long total = 0;
    for (int i = 0; i < vault_count; i++) total += vault_parts[i].size;
    prog_cur = 0;
    prog_total = total > 0 ? total : 1;

    int ok = 1, chunks = 0;
    for (int i = 0; i < vault_count && ok; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", VAULT_DIR, vault_parts[i].name);
        set_status("Joining %s (%d of %d)...", vault_parts[i].name, i + 1, vault_count);
        ui_redraw();
        FILE *in = fopen(path, "rb");
        if (!in) { ok = 0; break; }
        size_t n;
        while ((n = fread(io_buf, 1, sizeof(io_buf), in)) > 0) {
            if (fwrite(io_buf, 1, n, out) != n) { ok = 0; break; }
            prog_cur += (long long)n;
            if (++chunks % 16 == 0) ui_redraw();
        }
        if (ferror(in)) ok = 0;
        fclose(in);
    }
    if (fclose(out) != 0) ok = 0;
    prog_total = 0;

    if (!ok) {
        set_status("Join FAILED (disk full?). Your parts were kept.");
        return;
    }
    /* delete the parts only after the whole join worked */
    for (int i = 0; i < vault_count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", VAULT_DIR, vault_parts[i].name);
        remove(path);
    }
    set_status("Join complete! FULL_GAME.pkg is ready. Install it with the Package Installer.");
    refresh_state();
}

static void do_delete(void) {
    if (remove(FINAL_PKG) == 0) set_status("FULL_GAME.pkg deleted. Space freed.");
    else set_status("No FULL_GAME.pkg to delete.");
}

/* Clear the manual USB path so the app goes back to auto-detection. */
static void do_clear_manual_usb(void) {
    manual_usb_dir[0] = 0;
    manual_usb_count = 0;
    save_manual_usb();
    set_status("Manual USB path cleared. App will auto-detect again.");
    refresh_state();
}

#ifdef ENABLE_APPINST
static void do_install(void) {
    char uri[512];
    snprintf(uri, sizeof(uri), "file://%s", FINAL_PKG);
    int ret = sceAppInstUtilAppInstallPkg(uri, NULL, NULL);
    if (ret == 0) set_status("Installation started!");
    else set_status("Install error (code %d).", ret);
}
#endif

/* ------------------------------------------------------------------ */
int main(void) {
    mkdir(VAULT_DIR, 0777);
    log_line("=== PKG Vault started ===");

    sceUserServiceInitialize(NULL);
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    scePadInit();
    int pad = scePadOpen(userId, 0, 0, NULL);
#ifdef ENABLE_APPINST
    sceAppInstUtilInitialize();
#endif

    int vr = init_video();
    {
        char m[64];
        snprintf(m, sizeof(m), "video init result: %d", vr);
        log_line(m);
        if (vr != 0) notify(m);
    }

    menu[menu_n] = "Copy parts from USB";             menu_action[menu_n++] = A_COPY;
    menu[menu_n] = "Join parts into FULL_GAME.pkg";   menu_action[menu_n++] = A_JOIN;
    menu[menu_n] = "Delete FULL_GAME.pkg";            menu_action[menu_n++] = A_DELETE;
    menu[menu_n] = "Browse for USB folder (MANUAL)";  menu_action[menu_n++] = A_BROWSE;
#ifdef ENABLE_APPINST
    menu[menu_n] = "Install game";                    menu_action[menu_n++] = A_INSTALL;
#endif
    menu[menu_n] = "Exit";                            menu_action[menu_n++] = A_EXIT;

    load_manual_usb();
    refresh_state();
    ui_redraw();
    if (hide_splash) hide_splash();

    unsigned int prev = 0;
    int running = 1;
    for (int tick = 0; running; tick++) {
        int dirty = 0;
        OrbisPadData data;
        memset(&data, 0, sizeof(data));
        if (pad >= 0 && scePadReadState(pad, &data) == 0) {
            unsigned int pressed = data.buttons & ~prev;
            prev = data.buttons;

            if (browsing) {
                /* ---- browser input ---- */
                if (pressed & BTN_UP) {
                    if (browse_sel > 0) browse_sel--;
                    else if (browse_count > 0) browse_sel = browse_count - 1;
                    dirty = 1;
                }
                if (pressed & BTN_DOWN) {
                    if (browse_sel < browse_count - 1) browse_sel++;
                    else browse_sel = 0;
                    dirty = 1;
                }
                if (pressed & BTN_CROSS) {
                    browse_enter();
                    dirty = 1;
                }
                if (pressed & BTN_TRIANGLE) {
                    browse_confirm();
                    dirty = 1;
                }
                if (pressed & BTN_CIRCLE) {
                    if (!browse_up()) {
                        /* at root — exit browser without changing USB path */
                        browsing = 0;
                    }
                    dirty = 1;
                }
            } else {
                /* ---- main menu input ---- */
                if (pressed & BTN_UP)   { sel = (sel + menu_n - 1) % menu_n; dirty = 1; }
                if (pressed & BTN_DOWN) { sel = (sel + 1) % menu_n; dirty = 1; }
                if (pressed & BTN_CROSS) {
                    switch (menu_action[sel]) {
                        case A_COPY:   do_copy(); break;
                        case A_JOIN:   do_join(); break;
                        case A_DELETE: do_delete(); break;
                        case A_BROWSE:
                            browsing = 1;
                            browse_load("/");
                            break;
#ifdef ENABLE_APPINST
                        case A_INSTALL: do_install(); break;
#endif
                        case A_EXIT:   running = 0; break;
                    }
                    prev = 0;
                    dirty = 1;
                }
                if (pressed & BTN_CIRCLE) running = 0;
            }
        }
        if (tick % 40 == 0 && !browsing) {
            int ou = usb_count, ov = vault_count;
            char od[512];
            snprintf(od, sizeof(od), "%s", usb_dir);
            refresh_state();
            if (ou != usb_count || ov != vault_count || strcmp(od, usb_dir) != 0) dirty = 1;
        }
        if (dirty) ui_redraw();
        sceKernelUsleep(50000);
    }
    log_line("=== PKG Vault exit ===");
    return 0;
}
