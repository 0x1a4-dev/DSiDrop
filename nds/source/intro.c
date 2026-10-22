#include "ui.h"
#include "icon.h"
#include "intro_logo.h"
#include "brand_font.h"
#include "title_font.h"
#include <string.h>

static u16 pixels[128 * 128] __attribute__((aligned(32)));
static u16 *logo, *halo, *ring, *star, *letters[7], *tagline[3];
static u16 *device[2][4], *signal, *reflection;
static bool mainReady;
static u16 previousBlend[4];
static u32 previousDisplay;
static u8 previousBank;

static unsigned ease(unsigned value, unsigned duration) {
    if (value >= duration) return 4096;
    unsigned remaining = (duration - value) * 4096 / duration;
    return 4096 - (unsigned)(((uint64_t)remaining * remaining * remaining) >> 24);
}

static unsigned phase(unsigned frame, unsigned start, unsigned duration) {
    return frame <= start ? 0 : ease(frame - start, duration);
}

static unsigned opacity(unsigned frame, unsigned start, unsigned duration) {
    return phase(frame, start, duration) * 15 / 4096;
}

static u16 color16(unsigned red, unsigned green, unsigned blue) {
    return BIT(15) | RGB15(red, green, blue);
}

static u16 *texture(OamState *oam, SpriteSize size, unsigned width, unsigned height) {
    u16 *gfx = oamAllocateGfx(oam, size, SpriteColorFormat_Bmp);
    if (gfx) {
        DC_FlushRange(pixels, width * height * 2);
        dmaCopy(pixels, gfx, width * height * 2);
    }
    return gfx;
}

static void sprite(OamState *oam, unsigned id, int x, int y, unsigned priority,
        unsigned alpha, SpriteSize size, const u16 *gfx, int affine, bool doubled) {
    oamSet(oam, id, x, y, priority, alpha ? alpha : 1, size, SpriteColorFormat_Bmp,
        gfx, affine, doubled, !alpha, false, false, false);
}

static void radial(bool outline) {
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int dx = 2 * x - 63, dy = 2 * y - 63;
        unsigned radius = dx * dx + dy * dy;
        unsigned power = radius < 3969 ? 3969 - radius : 0;
        if (outline) power = radius >= 3320 && radius <= 3840 ?
            24 + (3840 - radius) / 16 : 0;
        else power = power * power / 3969;
        pixels[y * 64 + x] = !power ? 0 : outline ? color16(13, 7, 26) :
            color16(power * 9 / 3969, power * 3 / 3969, power * 21 / 3969);
    }
}

static unsigned textWidth(const char *text) {
    unsigned width = 0;
    for (; *text; text++) width += titleWidths[(unsigned)*text - 32];
    return width;
}

static bool textTextures(OamState *oam, u16 **parts, const char *text) {
    unsigned total = textWidth(text);
    for (unsigned part = 0; part < 3; part++) {
        memset(pixels, 0, 64 * 32 * 2);
        int left = (192 - (int)total) / 2 - (int)part * 64;
        for (const char *p = text; *p; p++) {
            unsigned index = (unsigned)*p - 32;
            for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 16; x++) {
                unsigned offset = y * 16 + x;
                unsigned coverage = (titleGlyphs[index][offset / 4] >> ((offset % 4) * 2)) & 3;
                int target = left + (int)x;
                if (coverage && (unsigned)target < 64) {
                    static const u16 colors[] = {0, RGB15(9,7,12), RGB15(18,16,22), RGB15(27,26,30)};
                    pixels[y * 64 + target] = BIT(15) | colors[coverage];
                }
            }
            left += titleWidths[index];
        }
        parts[part] = texture(oam, SpriteSize_64x32, 64, 32);
        if (!parts[part]) return false;
    }
    return true;
}

static void rounded(int left, int top, int width, int height, int radius, u16 color) {
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        int dx = x < radius ? radius - x : x >= width - radius ? x - (width - radius - 1) : 0;
        int dy = y < radius ? radius - y : y >= height - radius ? y - (height - radius - 1) : 0;
        if (dx * dx + dy * dy <= radius * radius)
            pixels[(top + y) * 128 + left + x] = color;
    }
}

static bool deviceTextures(unsigned lit) {
    memset(pixels, 0, sizeof(pixels));

    rounded(8, 2, 112, 58, 7, color16(10,8,15));
    rounded(9, 3, 110, 56, 6, color16(3,3,5));
    rounded(10, 4, 108, 2, 1, color16(15,13,20));
    rounded(34, 8, 60, 46, 3, color16(1,1,2));
    rounded(36, 10, 56, 42, 1, lit ? color16(10,2,23) : color16(3,2,6));
    if (lit) for (unsigned y = 0; y < 38; y++) for (unsigned x = 0; x < 52; x++)
        pixels[(12 + y) * 128 + 38 + x] = color16(8 + (37 - y) / 11, 2 + y / 18, 17 + (37 - y) / 4);
    rounded(63, 5, 2, 2, 0, color16(1,1,2));
    for (unsigned i = 0; i < 3; i++) {
        rounded(22, 23 + i * 5, 2, 2, 0, color16(9,8,12));
        rounded(104, 23 + i * 5, 2, 2, 0, color16(9,8,12));
    }
    rounded(17, 61, 94, 4, 2, color16(7,5,11));
    rounded(23, 62, 82, 1, 0, color16(13,9,19));
    rounded(8, 66, 112, 60, 8, color16(11,8,16));
    rounded(9, 67, 110, 58, 7, color16(3,3,5));
    rounded(10, 68, 108, 2, 1, color16(13,10,18));
    rounded(34, 74, 60, 46, 3, color16(12,11,15));
    rounded(36, 76, 56, 42, 1, lit ? color16(7,2,16) : color16(1,1,2));
    rounded(18, 94, 15, 5, 1, color16(15,13,19));
    rounded(23, 89, 5, 15, 1, color16(15,13,19));
    for (unsigned i = 0; i < 4; i++) {
        static const unsigned bx[] = {101,108,101,94}, by[] = {87,94,101,94};
        rounded(bx[i], by[i], 5, 5, 2, color16(17,15,21));
    }
    rounded(110, 7, 2, 2, 0, lit ? color16(22,10,31) : color16(8,5,13));
    if (lit) {
        rounded(63, 86, 2, 17, 0, color16(26,22,31));
        for (unsigned i = 0; i < 5; i++) {
            pixels[(98 + i) * 128 + 59 + i] = color16(26,22,31);
            pixels[(98 + i) * 128 + 68 - i] = color16(26,22,31);
        }
        rounded(56, 110, 16, 1, 0, color16(20,13,29));
    }
    static u16 drawing[128 * 128] __attribute__((aligned(32)));
    memcpy(drawing, pixels, sizeof(drawing));
    for (unsigned part = 0; part < 4; part++) {
        unsigned left = (part & 1) * 64, top = (part >> 1) * 64;
        for (unsigned y = 0; y < 64; y++)
            memcpy(pixels + y * 64, drawing + (top + y) * 128 + left, 128);
        device[lit][part] = texture(&oamSub, SpriteSize_64x64, 64, 64);
        if (!device[lit][part]) return false;
    }
    return true;
}

bool uiIntroBegin(void) {
    previousBlend[0] = REG_BLDCNT; previousBlend[1] = REG_BLDCNT_SUB;
    previousBlend[2] = REG_BLDALPHA; previousBlend[3] = REG_BLDALPHA_SUB;
    previousBank = VRAM_B_CR; previousDisplay = REG_DISPCNT;
    REG_BLDCNT = REG_BLDCNT_SUB = BLEND_ALPHA | BLEND_DST_BG0 | BLEND_DST_BG1 | BLEND_DST_BACKDROP;
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
    mainReady = true;
    unsigned pixel = 0;
    for (unsigned i = 0; i < sizeof(introLogoRuns) / sizeof(introLogoRuns[0]); i++) {
        unsigned run = introLogoRuns[i] >> 5, color = introLogoRuns[i] & 31;
        while (run--) pixels[pixel++] = color ? introLogoPalette[color] | BIT(15) : 0;
    }
    logo = texture(&oamMain, SpriteSize_64x64, 64, 64);
    radial(false); halo = texture(&oamMain, SpriteSize_64x64, 64, 64);
    radial(true); ring = texture(&oamMain, SpriteSize_64x64, 64, 64);
    memset(pixels, 0, 16 * 16 * 2);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        int dx = x - 7, dy = y - 7, radius = dx * dx + dy * dy;
        if (radius < 36) pixels[y * 16 + x] = color16(18 - radius / 3, 8 - radius / 6, 30 - radius / 2);
    }
    star = texture(&oamMain, SpriteSize_16x16, 16, 16);
    const char *brand = "DSiDrop";
    for (unsigned letter = 0; letter < 7; letter++) {
        unsigned glyph = strchr(brandChars, brand[letter]) - brandChars;
        memset(pixels, 0, 32 * 32 * 2);
        for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 32; x++) {
            unsigned offset = y * 32 + x;
            unsigned coverage = (brandGlyphs[glyph][offset / 4] >> ((offset % 4) * 2)) & 3;
            static const u16 colors[] = {0, RGB15(10,7,15), RGB15(21,18,27), RGB15(30,29,31)};
            if (coverage) pixels[offset] = BIT(15) | colors[coverage];
        }
        letters[letter] = texture(&oamMain, SpriteSize_32x32, 32, 32);
        if (!letters[letter]) return false;
    }
    if (!logo || !halo || !ring || !star || !textTextures(&oamMain, tagline, "Your Library. On your DSi.")) return false;
    if (!deviceTextures(0) || !deviceTextures(1)) return false;
    radial(false); reflection = texture(&oamSub, SpriteSize_64x64, 64, 64);
    for (unsigned i = 0; i < 32 * 32; i++) pixels[i] = rfIcon[i] ? rfIconPalette[rfIcon[i]] | BIT(15) : 0;
    signal = texture(&oamSub, SpriteSize_32x32, 32, 32);
    if (!reflection || !signal) return false;
    uiText(false, "Tap or press A to skip", 60, 174, 184, 1);
    return true;
}

void uiIntroFrame(unsigned frame) {
    setBrightness(3, frame < 24 ? -16 + (int)frame * 16 / 24 : 0);
    unsigned arrival = phase(frame, 6, 62);
    unsigned settle = phase(frame, 28, 76);
    int breath = sinLerp((int)frame * 170) / 1024;
    oamRotateScale(&oamMain, 0, 0, 119 + breath, 119 + breath);
    sprite(&oamMain, 0, 64, 0, 2, opacity(frame, 2, 42), SpriteSize_64x64, halo, 0, true);
    unsigned pulse = phase(frame, 18, 66);
    oamRotateScale(&oamMain, 1, 0, 256 - pulse * 123 / 4096, 256 - pulse * 123 / 4096);
    unsigned ringAlpha = frame < 24 ? 0 : frame < 60 ? 5 : frame < 112 ? (112 - frame) / 11 : 0;
    sprite(&oamMain, 1, 64, 0, 1, ringAlpha, SpriteSize_64x64, ring, 1, true);
    oamRotateScale(&oamMain, 2, 0, 285 - settle * 29 / 4096, 285 - settle * 29 / 4096);
    int logoY = 28 + (4096 - (int)arrival) * -88 / 4096;
    sprite(&oamMain, 2, 64, logoY - 32, 0, opacity(frame, 7, 34), SpriteSize_64x64, logo, 2, true);
    int left = 73;
    const char *brand = "DSiDrop";
    for (unsigned i = 0; i < 7; i++) {
        unsigned progress = phase(frame, 48 + i * 3, 30);
        sprite(&oamMain, 3 + i, left, 103 + 7 * (4096 - progress) / 4096, 0,
            progress * 15 / 4096, SpriteSize_32x32, letters[i], -1, false);
        left += brandWidths[strchr(brandChars, brand[i]) - brandChars];
    }
    for (unsigned i = 0; i < 3; i++)
        sprite(&oamMain, 10 + i, 32 + i * 64, 147, 0, opacity(frame, 90, 34), SpriteSize_64x32, tagline[i], -1, false);
    for (unsigned i = 0; i < 4; i++) {
        int angle = (int)frame * 85 + i * 8192 + 3500;
        int x = 120 + cosLerp(angle) * 73 / 4096;
        int y = 57 + sinLerp(angle) * 37 / 4096;
        unsigned alpha = frame < 110 ? opacity(frame, 12 + i * 5, 35) / 3 : frame < 155 ? (155 - frame) / 9 : 0;
        sprite(&oamMain, 13 + i, x, y, 1, alpha, SpriteSize_16x16, star, -1, false);
    }
    unsigned machineAlpha = opacity(frame, 32, 44);
    bool delivered = frame >= 102;
    int machineY = 30 + (4096 - (int)phase(frame, 32, 55)) * 10 / 4096;
    oamRotateScale(&oamSub, 0, 0, 120, 210);
    sprite(&oamSub, 8, 64, 11, 2, frame < 84 ? 0 : opacity(frame, 84, 36), SpriteSize_64x64, reflection, 0, true);
    for (unsigned i = 0; i < 4; i++) {
        int x = 64 + (i & 1) * 64, y = machineY + (i >> 1) * 64;
        sprite(&oamSub, 9 + i, x, y, 1, delivered ? 0 : machineAlpha, SpriteSize_64x64, device[0][i], -1, false);
        sprite(&oamSub, 13 + i, x, y, 0, delivered ? 15 : 0, SpriteSize_64x64, device[1][i], -1, false);
    }
    unsigned journey = phase(frame, 52, 49);
    int dropY = -31 + journey * 76 / 4096;
    unsigned signalAlpha = frame < 100 ? opacity(frame, 48, 14) : frame < 121 ? (121 - frame) * 15 / 21 : 0;
    int shrink = 256 + phase(frame, 85, 30) * 150 / 4096;
    oamRotateScale(&oamSub, 1, 0, shrink, shrink);
    sprite(&oamSub, 17, 96, dropY - 16, 0, signalAlpha, SpriteSize_32x32, signal, 1, true);
    oamUpdate(&oamMain); oamUpdate(&oamSub);
}

static void release(OamState *oam, u16 **gfx) {
    if (*gfx) oamFreeGfx(oam, *gfx);
    *gfx = NULL;
}

void uiIntroEnd(void) {
    swiWaitForVBlank();
    if (mainReady) {
        for (unsigned i = 0; i < 17; i++) oamClearSprite(&oamMain, i);
        oamUpdate(&oamMain);
    }
    for (unsigned i = 8; i < 21; i++) oamClearSprite(&oamSub, i);
    oamUpdate(&oamSub);
    release(&oamMain, &logo); release(&oamMain, &halo); release(&oamMain, &ring); release(&oamMain, &star);
    for (unsigned i = 0; i < 7; i++) release(&oamMain, &letters[i]);
    for (unsigned i = 0; i < 3; i++) release(&oamMain, &tagline[i]);
    for (unsigned lit = 0; lit < 2; lit++) for (unsigned i = 0; i < 4; i++) release(&oamSub, &device[lit][i]);
    release(&oamSub, &signal); release(&oamSub, &reflection);
    for (unsigned i = 0; i < 3; i++) oamRotateScale(&oamMain, i, 0, 256, 256);
    for (unsigned i = 0; i < 2; i++) oamRotateScale(&oamSub, i, 0, 256, 256);
    oamUpdate(&oamMain); oamUpdate(&oamSub);
    REG_BLDCNT = previousBlend[0]; REG_BLDCNT_SUB = previousBlend[1];
    REG_BLDALPHA = previousBlend[2]; REG_BLDALPHA_SUB = previousBlend[3];
    VRAM_B_CR = previousBank; REG_DISPCNT = previousDisplay;
    mainReady = false;
    setBrightness(3, 0);
}
