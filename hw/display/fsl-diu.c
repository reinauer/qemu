/*
 * Freescale Display Interface Unit (DIU)
 *
 * The display controller of the MPC8610, MPC5121 and P1022 ("fsl,diu").  It
 * scans out up to three planes, each described by a chain of area
 * descriptors in system memory, over a background colour, with a hardware
 * cursor on top, and sends the result through a gamma table to the display.
 *
 * The registers are big-endian.  An area descriptor is a little-endian
 * structure (Linux's struct diu_ad): the pixel format, the frame buffer
 * address, the source image size, the size and source offset of the area
 * of interest (AOI) and its position on the display, a colour key and a
 * link to the next descriptor of the plane.  The pixel format gives the
 * number of bytes per pixel, the byte order, the widths of up to four
 * components counted from the least significant bit, which of them are
 * red, green, blue and alpha, and whether the pixel indexes the palette.
 * The palette, the gamma table and the 32x32 cursor are also fetched from
 * memory, at the addresses their registers hold.  The palette has 256
 * entries of 8 bytes, each starting with a little-endian 0xAARRGGBB word;
 * the gamma table is 256 bytes each for red, green and blue; the cursor
 * holds little-endian ARGB1555 pixels, shown where alpha is set.
 *
 * The register layout follows Linux's include/linux/fsl-diu-fb.h and
 * drivers/video/fbdev/fsl-diu-fb.c; the palette layout is the one the
 * AmigaOS P1022DIU driver uses.  Emulated: plane 1 with its descriptor
 * chain, direct colour and paletted formats, the background colour, the
 * gamma table, the cursor and the vertical sync interrupt.  Not emulated:
 * planes 2 and 3, alpha blending, colour keying, flipping, write-back and
 * the colour bar test mode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/display/fsl-diu.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "ui/console.h"
#include "framebuffer.h"
#include "trace.h"

#define FSL_DIU_MMIO_SIZE       0x1000

#define DIU_DESC_1              0x000   /* plane 1 area descriptor */
#define DIU_DESC_2              0x004
#define DIU_DESC_3              0x008
#define DIU_GAMMA               0x00c   /* gamma table address */
#define DIU_PALETTE             0x010   /* palette address */
#define DIU_CURSOR              0x014   /* cursor image address */
#define DIU_CURS_POS            0x018
#define DIU_MODE                0x01c
#define DIU_BGND                0x020   /* background colour */
#define DIU_BGND_WB             0x024
#define DIU_DISP_SIZE           0x028
#define DIU_WB_SIZE             0x02c
#define DIU_WB_MEM_ADDR         0x030
#define DIU_HSYN_PARA           0x034
#define DIU_VSYN_PARA           0x038
#define DIU_SYN_POL             0x03c
#define DIU_THRESHOLDS          0x040
#define DIU_INT_STATUS          0x044
#define DIU_INT_MASK            0x048
#define DIU_COLORBAR            0x04c   /* 8 registers */
#define DIU_FILLING             0x06c
#define DIU_PLUT                0x070
#define DIU_NUM_REGS            (0x074 / 4)

/* DIU_MODE */
#define DIU_MODE_OFF            0
#define DIU_MODE_NORMAL         1

/* DIU_INT_STATUS and DIU_INT_MASK */
#define INT_VSYNC               0x01
#define INT_VSYNC_WB            0x02
#define INT_UNDRUN              0x04
#define INT_PARERR              0x08
#define INT_LS_BF_VS            0x10

/* Pixel format word of an area descriptor */
#define PF_BYTE_F               (1u << 28)  /* big-endian pixels */
#define PF_ALPHA_C_SHIFT        25
#define PF_BLUE_C_SHIFT         23
#define PF_GREEN_C_SHIFT        21
#define PF_RED_C_SHIFT          19
#define PF_PALETTE              (1u << 18)
#define PF_PIXEL_S_SHIFT        16

#define FSL_DIU_CURSOR_SIZE     32
#define FSL_DIU_MAX_AD_CHAIN    16
#define FSL_DIU_MAX_WIDTH       2048
#define FSL_DIU_REFRESH_HZ      60

/* An area descriptor, with its fields in host byte order */
typedef struct FslDiuAD {
    uint32_t pix_fmt;
    uint32_t addr;
    uint32_t src_size_g_alpha;
    uint32_t aoi_size;
    uint32_t offset_xyi;
    uint32_t offset_xyd;
    uint32_t ckmax;
    uint32_t ckmin;
    uint32_t next_ad;
} FslDiuAD;

/* The geometry of an area of interest, decoded */
typedef struct FslDiuAOI {
    uint32_t addr;
    unsigned stride;
    int src_x, src_y;
    int width, height;
    int x, y;
} FslDiuAOI;

struct FslDiuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq;
    QemuConsole *con;
    QEMUTimer *vsync_timer;
    Notifier machine_done;
    bool primary_console;

    uint32_t regs[DIU_NUM_REGS];

    /* Plane 1's descriptor chain as last drawn */
    FslDiuAD ad[FSL_DIU_MAX_AD_CHAIN];
    int nr_ads;

    /* The pixel format the decoder below is set up for */
    uint32_t pix_fmt;
    unsigned bypp;
    bool big_endian;
    bool paletted;
    uint8_t comp_shift[3];      /* red, green, blue */
    uint32_t comp_mask[3];
    uint8_t comp_lut[3][256];   /* component value to 8 bits */
    uint8_t comp_drop[3];       /* low bits dropped before the lookup */

    uint32_t palette[256];      /* as 0x00RRGGBB */
    uint8_t gamma[3][256];
    bool gamma_identity;
    uint16_t cursor[FSL_DIU_CURSOR_SIZE * FSL_DIU_CURSOR_SIZE];

    MemoryRegionSection fbsection;
    bool invalidate;
    bool blanked;
    bool cursor_drawn;
    int cursor_x, cursor_y;
    uint8_t linebuf[FSL_DIU_MAX_WIDTH * 4];
};

static void fsl_diu_update_irq(FslDiuState *s)
{
    uint32_t pending = s->regs[DIU_INT_STATUS / 4] &
                       ~s->regs[DIU_INT_MASK / 4];

    qemu_set_irq(s->irq, pending != 0);
}

static bool fsl_diu_enabled(FslDiuState *s)
{
    return s->regs[DIU_MODE / 4] != DIU_MODE_OFF;
}

static void fsl_diu_vsync_arm(FslDiuState *s)
{
    if (fsl_diu_enabled(s)) {
        timer_mod(s->vsync_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  NANOSECONDS_PER_SECOND / FSL_DIU_REFRESH_HZ);
    } else {
        timer_del(s->vsync_timer);
    }
}

static void fsl_diu_vsync(void *opaque)
{
    FslDiuState *s = opaque;

    s->regs[DIU_INT_STATUS / 4] |= INT_VSYNC | INT_LS_BF_VS;
    fsl_diu_update_irq(s);
    fsl_diu_vsync_arm(s);
}

static void fsl_diu_read_ad(FslDiuState *s, hwaddr addr, FslDiuAD *ad)
{
    uint32_t w[10];
    int i;

    if (address_space_read(&s->dma_as, addr, MEMTXATTRS_UNSPECIFIED,
                           w, sizeof(w)) != MEMTX_OK) {
        memset(w, 0, sizeof(w));
    }
    for (i = 0; i < ARRAY_SIZE(w); i++) {
        w[i] = le32_to_cpu(w[i]);
    }
    ad->pix_fmt = w[0];
    ad->addr = w[1];
    ad->src_size_g_alpha = w[2];
    ad->aoi_size = w[3];
    ad->offset_xyi = w[4];
    ad->offset_xyd = w[5];
    ad->ckmax = w[6];
    ad->ckmin = w[7];
    ad->next_ad = w[8];
}

/* Read plane 1's descriptor chain, returning whether it changed */
static bool fsl_diu_fetch_plane(FslDiuState *s)
{
    FslDiuAD ad[FSL_DIU_MAX_AD_CHAIN];
    hwaddr addr = s->regs[DIU_DESC_1 / 4];
    int n = 0;
    bool changed;

    while (addr && n < FSL_DIU_MAX_AD_CHAIN) {
        fsl_diu_read_ad(s, addr, &ad[n]);
        addr = ad[n++].next_ad;
    }

    changed = n != s->nr_ads || memcmp(ad, s->ad, n * sizeof(ad[0]));
    if (changed) {
        memcpy(s->ad, ad, n * sizeof(ad[0]));
        s->nr_ads = n;
    }
    return changed;
}

static void fsl_diu_set_format(FslDiuState *s, uint32_t pix_fmt)
{
    unsigned width[4], shift[4];
    unsigned sel[3];
    int i, v;

    s->pix_fmt = pix_fmt;
    s->bypp = extract32(pix_fmt, PF_PIXEL_S_SHIFT, 2) + 1;
    s->big_endian = pix_fmt & PF_BYTE_F;
    s->paletted = pix_fmt & PF_PALETTE;

    for (i = 0; i < 4; i++) {
        width[i] = extract32(pix_fmt, i * 4, 4);
        shift[i] = i ? shift[i - 1] + width[i - 1] : 0;
    }
    sel[0] = extract32(pix_fmt, PF_RED_C_SHIFT, 2);
    sel[1] = extract32(pix_fmt, PF_GREEN_C_SHIFT, 2);
    sel[2] = extract32(pix_fmt, PF_BLUE_C_SHIFT, 2);

    for (i = 0; i < 3; i++) {
        unsigned w = width[sel[i]];
        unsigned bits = MIN(w, 8);

        s->comp_shift[i] = MIN(shift[sel[i]], 31);
        s->comp_mask[i] = MAKE_64BIT_MASK(0, w);
        s->comp_drop[i] = w - bits;
        for (v = 0; v < 256; v++) {
            s->comp_lut[i][v] = bits ? (v & MAKE_64BIT_MASK(0, bits)) * 255 /
                                       MAKE_64BIT_MASK(0, bits) : 0;
        }
    }

    trace_fsl_diu_format(pix_fmt, s->bypp, s->big_endian, s->paletted);
}

static bool fsl_diu_fetch_palette(FslDiuState *s)
{
    uint32_t raw[256 * 2];
    uint32_t pal[256];
    int i;

    if (address_space_read(&s->dma_as, s->regs[DIU_PALETTE / 4],
                           MEMTXATTRS_UNSPECIFIED, raw,
                           sizeof(raw)) != MEMTX_OK) {
        memset(raw, 0, sizeof(raw));
    }
    for (i = 0; i < 256; i++) {
        pal[i] = le32_to_cpu(raw[i * 2]) & 0xffffff;
    }
    if (!memcmp(pal, s->palette, sizeof(pal))) {
        return false;
    }
    memcpy(s->palette, pal, sizeof(pal));
    return true;
}

static bool fsl_diu_fetch_gamma(FslDiuState *s)
{
    uint8_t gamma[3][256];
    int i, c;

    if (!s->regs[DIU_GAMMA / 4] ||
        address_space_read(&s->dma_as, s->regs[DIU_GAMMA / 4],
                           MEMTXATTRS_UNSPECIFIED, gamma,
                           sizeof(gamma)) != MEMTX_OK) {
        for (c = 0; c < 3; c++) {
            for (i = 0; i < 256; i++) {
                gamma[c][i] = i;
            }
        }
    }
    if (!memcmp(gamma, s->gamma, sizeof(gamma))) {
        return false;
    }
    memcpy(s->gamma, gamma, sizeof(gamma));
    s->gamma_identity = true;
    for (c = 0; c < 3; c++) {
        for (i = 0; i < 256; i++) {
            if (gamma[c][i] != i) {
                s->gamma_identity = false;
            }
        }
    }
    return true;
}

static bool fsl_diu_fetch_cursor(FslDiuState *s)
{
    uint16_t cursor[FSL_DIU_CURSOR_SIZE * FSL_DIU_CURSOR_SIZE];
    int i;

    if (!s->regs[DIU_CURSOR / 4] ||
        address_space_read(&s->dma_as, s->regs[DIU_CURSOR / 4],
                           MEMTXATTRS_UNSPECIFIED, cursor,
                           sizeof(cursor)) != MEMTX_OK) {
        memset(cursor, 0, sizeof(cursor));
    }
    for (i = 0; i < ARRAY_SIZE(cursor); i++) {
        cursor[i] = le16_to_cpu(cursor[i]);
    }
    if (!memcmp(cursor, s->cursor, sizeof(cursor))) {
        return false;
    }
    memcpy(s->cursor, cursor, sizeof(cursor));
    return true;
}

static inline uint32_t fsl_diu_gamma(FslDiuState *s, uint32_t rgb)
{
    if (s->gamma_identity) {
        return rgb;
    }
    return s->gamma[0][(rgb >> 16) & 0xff] << 16 |
           s->gamma[1][(rgb >> 8) & 0xff] << 8 |
           s->gamma[2][rgb & 0xff];
}

static void fsl_diu_convert(FslDiuState *s, uint32_t *dst,
                            const uint8_t *src, int n)
{
    int i, c;

    for (i = 0; i < n; i++, src += s->bypp) {
        uint32_t v, rgb;

        switch (s->bypp) {
        case 1:
            v = src[0];
            break;
        case 2:
            v = s->big_endian ? lduw_be_p(src) : lduw_le_p(src);
            break;
        case 3:
            v = s->big_endian ? src[0] << 16 | src[1] << 8 | src[2]
                              : src[2] << 16 | src[1] << 8 | src[0];
            break;
        default:
            v = s->big_endian ? ldl_be_p(src) : ldl_le_p(src);
            break;
        }

        if (s->paletted) {
            rgb = s->palette[v & 0xff];
        } else {
            rgb = 0;
            for (c = 0; c < 3; c++) {
                uint32_t x = (v >> s->comp_shift[c]) & s->comp_mask[c];

                rgb = rgb << 8 | s->comp_lut[c][(x >> s->comp_drop[c]) & 0xff];
            }
        }
        dst[i] = fsl_diu_gamma(s, rgb);
    }
}

static void fsl_diu_draw_line(void *opaque, uint8_t *dst, const uint8_t *src,
                              int width, int deststep)
{
    fsl_diu_convert(opaque, (uint32_t *)dst, src, width);
}

static void fsl_diu_decode_aoi(FslDiuState *s, const FslDiuAD *ad,
                               FslDiuAOI *aoi)
{
    aoi->addr = ad->addr;
    aoi->stride = extract32(ad->src_size_g_alpha, 0, 11) * s->bypp;
    aoi->width = extract32(ad->aoi_size, 0, 11);
    aoi->height = extract32(ad->aoi_size, 16, 11);
    aoi->src_x = extract32(ad->offset_xyi, 0, 11);
    aoi->src_y = extract32(ad->offset_xyi, 16, 11);
    aoi->x = extract32(ad->offset_xyd, 0, 11);
    aoi->y = extract32(ad->offset_xyd, 16, 11);
}

/* Draw display row @y from the background and plane 1 */
static void fsl_diu_render_row(FslDiuState *s, int y, uint32_t *dst, int xres)
{
    uint32_t bg = fsl_diu_gamma(s, s->regs[DIU_BGND / 4] & 0xffffff);
    int i;

    for (i = 0; i < xres; i++) {
        dst[i] = bg;
    }

    for (i = 0; i < s->nr_ads; i++) {
        FslDiuAOI aoi;
        hwaddr src;
        int w;

        fsl_diu_decode_aoi(s, &s->ad[i], &aoi);
        if (y < aoi.y || y >= aoi.y + aoi.height || aoi.x >= xres) {
            continue;
        }
        w = MIN(aoi.width, xres - aoi.x);
        src = aoi.addr + (hwaddr)(aoi.src_y + y - aoi.y) * aoi.stride +
              aoi.src_x * s->bypp;
        if (address_space_read(&s->dma_as, src, MEMTXATTRS_UNSPECIFIED,
                               s->linebuf, w * s->bypp) != MEMTX_OK) {
            continue;
        }
        fsl_diu_convert(s, dst + aoi.x, s->linebuf, w);
    }
}

static void fsl_diu_render_rows(FslDiuState *s, DisplaySurface *ds,
                                int first, int last)
{
    int xres = surface_width(ds);
    int y;

    first = MAX(first, 0);
    last = MIN(last, surface_height(ds) - 1);
    for (y = first; y <= last; y++) {
        fsl_diu_render_row(s, y, (uint32_t *)((uint8_t *)surface_data(ds) +
                                              y * surface_stride(ds)), xres);
    }
    if (first <= last) {
        qemu_console_update(s->con, 0, first, xres, last - first + 1);
    }
}

static void fsl_diu_draw_cursor(FslDiuState *s, DisplaySurface *ds)
{
    uint32_t pos = s->regs[DIU_CURS_POS / 4];
    int cx = extract32(pos, 0, 11);
    int cy = extract32(pos, 16, 11);
    int xres = surface_width(ds);
    int yres = surface_height(ds);
    int x, y;

    s->cursor_drawn = false;
    if (!s->regs[DIU_CURSOR / 4] || cx >= xres || cy >= yres) {
        return;
    }

    for (y = 0; y < FSL_DIU_CURSOR_SIZE && cy + y < yres; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)surface_data(ds) +
                                     (cy + y) * surface_stride(ds)) + cx;

        for (x = 0; x < FSL_DIU_CURSOR_SIZE && cx + x < xres; x++) {
            uint16_t p = s->cursor[y * FSL_DIU_CURSOR_SIZE + x];

            if (p & 0x8000) {
                uint32_t r = extract32(p, 10, 5), g = extract32(p, 5, 5);
                uint32_t b = extract32(p, 0, 5);

                dst[x] = (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 |
                         (b << 3 | b >> 2);
            }
        }
    }
    s->cursor_drawn = true;
    s->cursor_x = cx;
    s->cursor_y = cy;
    qemu_console_update(s->con, cx, cy, MIN(FSL_DIU_CURSOR_SIZE, xres - cx),
                        MIN(FSL_DIU_CURSOR_SIZE, yres - cy));
}

static void fsl_diu_blank(FslDiuState *s)
{
    DisplaySurface *ds = qemu_console_surface(s->con);

    if (s->blanked || !ds) {
        return;
    }
    memset(surface_data(ds), 0, surface_stride(ds) * surface_height(ds));
    qemu_console_update_full(s->con);
    s->blanked = true;
    s->cursor_drawn = false;
}

static bool fsl_diu_update_display(void *opaque)
{
    FslDiuState *s = opaque;
    uint32_t disp_size = s->regs[DIU_DISP_SIZE / 4];
    int xres = extract32(disp_size, 0, 11);
    int yres = extract32(disp_size, 16, 11);
    uint32_t curs_pos = s->regs[DIU_CURS_POS / 4];
    DisplaySurface *ds;
    bool cursor_changed;
    FslDiuAOI aoi = {};
    int first = 0, last;

    if (!fsl_diu_enabled(s) || !s->regs[DIU_DESC_1 / 4] || !xres || !yres) {
        fsl_diu_blank(s);
        return true;
    }

    ds = qemu_console_surface(s->con);
    if (!ds || surface_width(ds) != xres || surface_height(ds) != yres) {
        qemu_console_resize(s->con, xres, yres);
        ds = qemu_console_surface(s->con);
        s->invalidate = true;
    }
    if (s->blanked) {
        s->blanked = false;
        s->invalidate = true;
    }

    if (fsl_diu_fetch_plane(s)) {
        s->invalidate = true;
    }
    if (s->nr_ads && s->ad[0].pix_fmt != s->pix_fmt) {
        fsl_diu_set_format(s, s->ad[0].pix_fmt);
        s->invalidate = true;
    }
    if (s->paletted && fsl_diu_fetch_palette(s)) {
        s->invalidate = true;
    }
    if (fsl_diu_fetch_gamma(s)) {
        s->invalidate = true;
    }
    cursor_changed = fsl_diu_fetch_cursor(s) ||
                     s->cursor_x != extract32(curs_pos, 0, 11) ||
                     s->cursor_y != extract32(curs_pos, 16, 11);

    /* Take the cursor off where it was */
    if (!s->invalidate && s->cursor_drawn && cursor_changed) {
        fsl_diu_render_rows(s, ds, s->cursor_y,
                            s->cursor_y + FSL_DIU_CURSOR_SIZE - 1);
    }

    if (s->nr_ads) {
        fsl_diu_decode_aoi(s, &s->ad[0], &aoi);
    }
    if (s->nr_ads == 1 && !aoi.x && !aoi.y && aoi.width == xres &&
        aoi.height == yres && aoi.stride >= xres * s->bypp) {
        /* The common case of one AOI covering the display */
        hwaddr base = aoi.addr + (hwaddr)aoi.src_y * aoi.stride +
                      aoi.src_x * s->bypp;

        if (s->invalidate) {
            framebuffer_update_memory_section(&s->fbsection, s->dma_mr, base,
                                              yres, aoi.stride);
        }
        if (s->fbsection.mr) {
            framebuffer_update_display(ds, &s->fbsection, xres, yres,
                                       aoi.stride, surface_stride(ds), 0,
                                       s->invalidate, fsl_diu_draw_line, s,
                                       &first, &last);
            if (first >= 0) {
                qemu_console_update(s->con, 0, first, xres, last - first + 1);
            }
        } else {
            fsl_diu_render_rows(s, ds, 0, yres - 1);
        }
    } else {
        fsl_diu_render_rows(s, ds, 0, yres - 1);
    }

    fsl_diu_draw_cursor(s, ds);
    s->invalidate = false;
    return true;
}

static void fsl_diu_invalidate_display(void *opaque)
{
    FslDiuState *s = opaque;

    s->invalidate = true;
}

static const GraphicHwOps fsl_diu_gfx_ops = {
    .invalidate = fsl_diu_invalidate_display,
    .gfx_update = fsl_diu_update_display,
};

static void fsl_diu_check_planes(FslDiuState *s)
{
    if (s->regs[DIU_DESC_2 / 4] || s->regs[DIU_DESC_3 / 4]) {
        qemu_log_mask(LOG_UNIMP, "fsl-diu: planes 2 and 3 are not "
                      "implemented\n");
    }
}

static uint64_t fsl_diu_read(void *opaque, hwaddr addr, unsigned size)
{
    FslDiuState *s = opaque;
    uint32_t val;

    if (addr >= DIU_NUM_REGS * 4) {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    val = s->regs[addr / 4];
    if (addr == DIU_INT_STATUS) {
        /* Reading the status clears it */
        s->regs[DIU_INT_STATUS / 4] = 0;
        fsl_diu_update_irq(s);
    }
    trace_fsl_diu_read(addr, val);
    return val;
}

static void fsl_diu_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    FslDiuState *s = opaque;
    uint32_t old;

    trace_fsl_diu_write(addr, val);
    if (addr >= DIU_NUM_REGS * 4) {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n", __func__, addr, val);
        return;
    }

    old = s->regs[addr / 4];
    s->regs[addr / 4] = val;

    switch (addr) {
    case DIU_DESC_1:
    case DIU_BGND:
    case DIU_DISP_SIZE:
        s->invalidate = true;
        break;
    case DIU_DESC_2:
    case DIU_DESC_3:
        fsl_diu_check_planes(s);
        break;
    case DIU_MODE:
        if (val > DIU_MODE_NORMAL) {
            qemu_log_mask(LOG_UNIMP, "fsl-diu: mode %" PRIu64 " is not "
                          "implemented\n", val);
        }
        if (val != old) {
            s->invalidate = true;
            fsl_diu_vsync_arm(s);
        }
        break;
    case DIU_INT_STATUS:
        /* Read-only */
        s->regs[addr / 4] = old;
        break;
    case DIU_INT_MASK:
        fsl_diu_update_irq(s);
        break;
    case DIU_WB_MEM_ADDR:
        if (val) {
            qemu_log_mask(LOG_UNIMP, "fsl-diu: write-back is not "
                          "implemented\n");
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps fsl_diu_ops = {
    .read = fsl_diu_read,
    .write = fsl_diu_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fsl_diu_reset(DeviceState *dev)
{
    FslDiuState *s = FSL_DIU(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[DIU_INT_MASK / 4] = 0xffffffff;
    timer_del(s->vsync_timer);
    fsl_diu_update_irq(s);
    s->invalidate = true;
}

static int fsl_diu_post_load(void *opaque, int version_id)
{
    FslDiuState *s = opaque;

    /* Fetch everything again and redraw */
    s->nr_ads = 0;
    s->pix_fmt = 0;
    s->bypp = 1;
    memset(s->palette, 0, sizeof(s->palette));
    s->invalidate = true;
    return 0;
}

static const VMStateDescription vmstate_fsl_diu = {
    .name = TYPE_FSL_DIU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = fsl_diu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FslDiuState, DIU_NUM_REGS),
        VMSTATE_TIMER_PTR(vsync_timer, FslDiuState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * A secondary console is created once the machine is complete, after those
 * of the display cards given with -device
 */
static void fsl_diu_machine_done(Notifier *notifier, void *data)
{
    FslDiuState *s = container_of(notifier, FslDiuState, machine_done);

    s->con = qemu_graphic_console_create(DEVICE(s), 0, &fsl_diu_gfx_ops, s);
}

static void fsl_diu_realize(DeviceState *dev, Error **errp)
{
    FslDiuState *s = FSL_DIU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->dma_mr) {
        error_setg(errp, "'memory' property was not set");
        return;
    }
    address_space_init(&s->dma_as, s->dma_mr, "fsl-diu");

    memory_region_init_io(&s->iomem, OBJECT(s), &fsl_diu_ops, s, "fsl-diu",
                          FSL_DIU_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->vsync_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, fsl_diu_vsync, s);
    s->bypp = 1;
    if (s->primary_console) {
        s->con = qemu_graphic_console_create(dev, 0, &fsl_diu_gfx_ops, s);
    } else {
        s->machine_done.notify = fsl_diu_machine_done;
        qemu_add_machine_init_done_notifier(&s->machine_done);
    }
}

static const Property fsl_diu_properties[] = {
    DEFINE_PROP_LINK("memory", FslDiuState, dma_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_BOOL("primary-console", FslDiuState, primary_console, true),
};

static void fsl_diu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Freescale Display Interface Unit";
    dc->realize = fsl_diu_realize;
    dc->vmsd = &vmstate_fsl_diu;
    device_class_set_legacy_reset(dc, fsl_diu_reset);
    device_class_set_props(dc, fsl_diu_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo fsl_diu_types[] = {
    {
        .name          = TYPE_FSL_DIU,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslDiuState),
        .class_init    = fsl_diu_class_init,
    },
};

DEFINE_TYPES(fsl_diu_types)
