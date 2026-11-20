/*
 * Freescale Elo Plus DMA controller ("fsl,eloplus-dma")
 *
 * The general purpose DMA controller of the MPC85xx, P1 and P2 SoCs: four
 * channels, each moving data between system memory addresses, either
 * directly from its source, destination and byte count registers, or from
 * a chain of link descriptors in memory.  A link descriptor holds the
 * source and destination attributes and addresses, the address of the
 * next descriptor with an end-of-list flag, and the byte count, all
 * big-endian, as Linux's drivers/dma/fsldma.h describes them.
 *
 * Transfers complete at once, when software starts the channel, unless
 * its mode register enables external master start or pause: then the
 * channel moves data only while its DMA request input is asserted, as a
 * peripheral such as the SSI paces it, one source or destination address
 * hold unit at a time.  Not emulated: list descriptors (chains of link
 * descriptor chains), stride mode, and priority settings, which make no
 * difference here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/units.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/dma/fsl-dma.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "trace.h"

#define FSL_DMA_NUM_CHANNELS    4
#define FSL_DMA_CHAN_SIZE       0x80
#define FSL_DMA_DGSR            0x200   /* general status, after channels */
#define FSL_DMA_MMIO_SIZE       0x204

/* Channel registers */
#define DMA_MR                  0x00
#define DMA_SR                  0x04
#define DMA_ECLNDAR             0x08
#define DMA_CLNDAR              0x0c
#define DMA_SATR                0x10
#define DMA_SAR                 0x14
#define DMA_DATR                0x18
#define DMA_DAR                 0x1c
#define DMA_BCR                 0x20
#define DMA_ENLNDAR             0x24
#define DMA_NLNDAR              0x28
#define DMA_ECLSDAR             0x30
#define DMA_CLSDAR              0x34
#define DMA_ENLSDAR             0x38
#define DMA_NLSDAR              0x3c
#define DMA_SSR                 0x40
#define DMA_DSR                 0x44
#define DMA_NUM_REGS            (0x48 / 4)

#define MR_CS                   0x00000001  /* channel start */
#define MR_CC                   0x00000002  /* channel continue */
#define MR_CTM                  0x00000004  /* direct mode */
#define MR_CA                   0x00000008  /* channel abort */
#define MR_EIE                  0x00000040
#define MR_EOTIE                0x00000080
#define MR_EOLNIE               0x00000100
#define MR_EOSIE                0x00000200
#define MR_SAHE                 0x00001000  /* source address hold */
#define MR_DAHE                 0x00002000  /* destination address hold */
#define MR_SAHTS_SHIFT          14          /* their sizes, 1 << n bytes */
#define MR_DAHTS_SHIFT          16
#define MR_EMS_EN               0x00040000  /* external master start */
#define MR_EMP_EN               0x00200000  /* external master pause */
#define MR_BWC_MASK             0x0f000000  /* bandwidth, 1 << n bytes */
#define MR_BWC_SHIFT            24

#define SR_EOCDI                0x00000001  /* end of direct transfer */
#define SR_EOSI                 0x00000002  /* end of segment */
#define SR_CB                   0x00000004  /* channel busy */
#define SR_EOLNI                0x00000008  /* end of links */
#define SR_PE                   0x00000010
#define SR_CH                   0x00000020
#define SR_TE                   0x00000080  /* transfer error */
#define SR_W1C                  (SR_EOCDI | SR_EOSI | SR_EOLNI | SR_PE | \
                                 SR_TE)

/* Attribute registers: the extended address and stride mode */
#define ATR_EAD_MASK            0x0000000f
#define ATR_STRIDE              0x01000000

/* The next link descriptor address register */
#define NLNDAR_EOL              0x00000001
#define NLNDAR_EOSIE            0x00000008
#define NLNDAR_ADDR_MASK        0xffffffe0

#define FSL_DMA_MAX_LINKS       4096
#define FSL_DMA_CHUNK           (64 * KiB)
/* What an externally paced channel moves at most before others get a go */
#define FSL_DMA_PACE_BUDGET     (16 * KiB)

typedef struct FslDmaChannel {
    uint32_t regs[DMA_NUM_REGS];
    bool dreq;
    qemu_irq irq;
} FslDmaChannel;

struct FslDmaState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUBH *bh;
    FslDmaChannel chan[FSL_DMA_NUM_CHANNELS];
    uint8_t buf[FSL_DMA_CHUNK];
};

static void fsl_dma_update_irq(FslDmaChannel *ch)
{
    uint32_t mr = ch->regs[DMA_MR / 4];
    uint32_t sr = ch->regs[DMA_SR / 4];
    bool level = ((sr & SR_EOLNI) && (mr & MR_EOLNIE)) ||
                 ((sr & SR_EOSI) && (mr & MR_EOSIE)) ||
                 ((sr & SR_EOCDI) && (mr & MR_EOTIE)) ||
                 ((sr & SR_TE) && (mr & MR_EIE));

    qemu_set_irq(ch->irq, level);
}

static hwaddr fsl_dma_addr(uint32_t atr, uint32_t addr)
{
    return (hwaddr)(atr & ATR_EAD_MASK) << 32 | addr;
}

/* The number of bytes an address hold size field selects */
static uint32_t fsl_dma_hold_size(uint32_t mr, int shift)
{
    return 1 << ((mr >> shift) & 3);
}

/* Whether a channel moves data only while its DREQ input is asserted */
static bool fsl_dma_paced(FslDmaChannel *ch)
{
    return ch->regs[DMA_MR / 4] & (MR_EMS_EN | MR_EMP_EN);
}

/*
 * Move @len bytes of the current segment, from the source to the
 * destination address registers, which advance unless the mode register
 * holds them, and count them off the byte count register.  Returns false
 * on a transfer error.
 */
static bool fsl_dma_move(FslDmaState *s, int n, uint32_t len)
{
    uint32_t *r = s->chan[n].regs;
    uint32_t mr = r[DMA_MR / 4];
    bool shold = mr & MR_SAHE;
    bool dhold = mr & MR_DAHE;
    hwaddr src = fsl_dma_addr(r[DMA_SATR / 4], r[DMA_SAR / 4]);
    hwaddr dst = fsl_dma_addr(r[DMA_DATR / 4], r[DMA_DAR / 4]);
    uint32_t unit = FSL_DMA_CHUNK;
    uint8_t *buf;

    trace_fsl_dma_copy(n, src, dst, len);
    if ((r[DMA_SATR / 4] | r[DMA_DATR / 4]) & ATR_STRIDE) {
        qemu_log_mask(LOG_UNIMP, "fsl-dma: stride mode is not "
                      "implemented\n");
        return false;
    }

    /* A held address takes the data in units of its hold size */
    if (shold) {
        unit = fsl_dma_hold_size(mr, MR_SAHTS_SHIFT);
    }
    if (dhold) {
        unit = MIN(unit, fsl_dma_hold_size(mr, MR_DAHTS_SHIFT));
    }

    buf = s->buf;
    while (len) {
        uint32_t chunk = MIN(len, unit);

        if (dma_memory_read(&address_space_memory, src, buf, chunk,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK ||
            dma_memory_write(&address_space_memory, dst, buf, chunk,
                             MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "fsl-dma: transfer error, 0x%"
                          HWADDR_PRIx " to 0x%" HWADDR_PRIx "\n", src, dst);
            return false;
        }
        if (!shold) {
            src += chunk;
            r[DMA_SAR / 4] += chunk;
        }
        if (!dhold) {
            dst += chunk;
            r[DMA_DAR / 4] += chunk;
        }
        r[DMA_BCR / 4] -= chunk;
        len -= chunk;
    }
    return true;
}

/* Load the link descriptor at the current link descriptor address */
static bool fsl_dma_load_link(FslDmaState *s, int n)
{
    uint32_t *r = s->chan[n].regs;
    hwaddr ld = fsl_dma_addr(r[DMA_ECLNDAR / 4],
                             r[DMA_CLNDAR / 4] & NLNDAR_ADDR_MASK);
    uint32_t w[8];
    int i;

    if (dma_memory_read(&address_space_memory, ld, w, sizeof(w),
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "fsl-dma: channel %d: cannot read "
                      "link descriptor at 0x%" HWADDR_PRIx "\n", n, ld);
        return false;
    }
    for (i = 0; i < ARRAY_SIZE(w); i++) {
        w[i] = be32_to_cpu(w[i]);
    }
    r[DMA_SATR / 4] = w[0];
    r[DMA_SAR / 4] = w[1];
    r[DMA_DATR / 4] = w[2];
    r[DMA_DAR / 4] = w[3];
    r[DMA_ENLNDAR / 4] = w[4];
    r[DMA_NLNDAR / 4] = w[5];
    r[DMA_BCR / 4] = w[6];
    trace_fsl_dma_link(n, ld, w[1], w[3], w[5], w[6]);
    return true;
}

/*
 * Finish the current segment, and in chaining mode go on to the next link
 * descriptor.  Returns whether the channel has more to transfer; if not,
 * it is no longer busy.
 */
static bool fsl_dma_end_segment(FslDmaState *s, int n)
{
    uint32_t *r = s->chan[n].regs;

    if (r[DMA_MR / 4] & MR_CTM) {
        r[DMA_SR / 4] |= SR_EOCDI;
        r[DMA_SR / 4] &= ~SR_CB;
        return false;
    }
    if ((r[DMA_NLNDAR / 4] & NLNDAR_EOSIE) || (r[DMA_MR / 4] & MR_EOSIE)) {
        r[DMA_SR / 4] |= SR_EOSI;
    }
    if (r[DMA_NLNDAR / 4] & NLNDAR_EOL) {
        r[DMA_SR / 4] |= SR_EOLNI;
        r[DMA_SR / 4] &= ~SR_CB;
        return false;
    }
    r[DMA_ECLNDAR / 4] = r[DMA_ENLNDAR / 4];
    r[DMA_CLNDAR / 4] = r[DMA_NLNDAR / 4];
    if (!fsl_dma_load_link(s, n)) {
        r[DMA_SR / 4] |= SR_TE;
        r[DMA_SR / 4] &= ~SR_CB;
        return false;
    }
    return true;
}

/* Run a channel that software started to completion */
static void fsl_dma_run(FslDmaState *s, int n)
{
    uint32_t *r = s->chan[n].regs;
    int links;

    for (links = 0; links < FSL_DMA_MAX_LINKS; links++) {
        if (!fsl_dma_move(s, n, r[DMA_BCR / 4])) {
            r[DMA_SR / 4] |= SR_TE;
            r[DMA_SR / 4] &= ~SR_CB;
            return;
        }
        if (!fsl_dma_end_segment(s, n)) {
            return;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "fsl-dma: channel %d: more than %d link "
                  "descriptors\n", n, FSL_DMA_MAX_LINKS);
    r[DMA_SR / 4] |= SR_TE;
    r[DMA_SR / 4] &= ~SR_CB;
}

/* Whether an externally paced channel can move data now */
static bool fsl_dma_ready(FslDmaChannel *ch)
{
    return ch->dreq && (ch->regs[DMA_SR / 4] & SR_CB) &&
           (ch->regs[DMA_MR / 4] & MR_CS) && fsl_dma_paced(ch);
}

/*
 * Move data on the externally paced channels while their DREQ inputs stay
 * asserted, one transfer unit (the address hold size, or the bandwidth
 * the mode register sets) at a time, as the peripheral negates DREQ when
 * it has enough.
 */
static void fsl_dma_pace(void *opaque)
{
    FslDmaState *s = opaque;
    bool again = false;
    int n;

    for (n = 0; n < FSL_DMA_NUM_CHANNELS; n++) {
        FslDmaChannel *ch = &s->chan[n];
        uint32_t *r = ch->regs;
        uint32_t budget = FSL_DMA_PACE_BUDGET;

        while (budget && fsl_dma_ready(ch)) {
            uint32_t mr = r[DMA_MR / 4];
            uint32_t unit = 1 << ((mr & MR_BWC_MASK) >> MR_BWC_SHIFT);
            uint32_t len;

            if (mr & MR_DAHE) {
                unit = fsl_dma_hold_size(mr, MR_DAHTS_SHIFT);
            } else if (mr & MR_SAHE) {
                unit = fsl_dma_hold_size(mr, MR_SAHTS_SHIFT);
            }
            len = MIN(MIN(unit, r[DMA_BCR / 4]), budget);
            if (len && !fsl_dma_move(s, n, len)) {
                r[DMA_SR / 4] |= SR_TE;
                r[DMA_SR / 4] &= ~SR_CB;
                break;
            }
            budget -= MIN(budget, MAX(len, 1));
            if (!r[DMA_BCR / 4] && !fsl_dma_end_segment(s, n)) {
                break;
            }
        }
        fsl_dma_update_irq(ch);
        again |= fsl_dma_ready(ch);
    }
    if (again) {
        qemu_bh_schedule(s->bh);
    }
}

static void fsl_dma_set_dreq(void *opaque, int n, int level)
{
    FslDmaState *s = opaque;
    FslDmaChannel *ch = &s->chan[n];

    ch->dreq = level;
    if (fsl_dma_ready(ch)) {
        qemu_bh_schedule(s->bh);
    }
}

static void fsl_dma_start(FslDmaState *s, int n, bool resume)
{
    FslDmaChannel *ch = &s->chan[n];
    uint32_t *r = ch->regs;

    if (r[DMA_MR / 4] & MR_CTM) {
        trace_fsl_dma_start(n, "direct", 0);
    } else {
        if (resume) {
            /*
             * Continue after the descriptor the channel stopped at, if
             * software has since linked another one to it
             */
            hwaddr ld = fsl_dma_addr(r[DMA_ECLNDAR / 4],
                                     r[DMA_CLNDAR / 4] & NLNDAR_ADDR_MASK);
            uint32_t next[2];

            if (dma_memory_read(&address_space_memory, ld + 16, next,
                                sizeof(next),
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                r[DMA_SR / 4] |= SR_TE;
                fsl_dma_update_irq(ch);
                return;
            }
            if (be32_to_cpu(next[1]) & NLNDAR_EOL) {
                return;
            }
            r[DMA_ECLNDAR / 4] = be32_to_cpu(next[0]);
            r[DMA_CLNDAR / 4] = be32_to_cpu(next[1]);
        }
        trace_fsl_dma_start(n, resume ? "continue" : "chain",
                            r[DMA_CLNDAR / 4]);
        if (!fsl_dma_load_link(s, n)) {
            r[DMA_SR / 4] |= SR_TE;
            fsl_dma_update_irq(ch);
            return;
        }
    }

    r[DMA_SR / 4] |= SR_CB;
    if (fsl_dma_paced(ch)) {
        /* Wait for the peripheral to request data */
        if (fsl_dma_ready(ch)) {
            qemu_bh_schedule(s->bh);
        }
        return;
    }
    fsl_dma_run(s, n);
    fsl_dma_update_irq(ch);
}

static uint64_t fsl_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    FslDmaState *s = opaque;
    uint32_t val = 0;
    int n;

    if (addr == FSL_DMA_DGSR) {
        /* Each channel's status in a byte, channel 0 in the top one */
        for (n = 0; n < FSL_DMA_NUM_CHANNELS; n++) {
            val |= (s->chan[n].regs[DMA_SR / 4] & 0xff) << (24 - n * 8);
        }
    } else if (addr < FSL_DMA_NUM_CHANNELS * FSL_DMA_CHAN_SIZE &&
               (addr % FSL_DMA_CHAN_SIZE) < DMA_NUM_REGS * 4) {
        n = addr / FSL_DMA_CHAN_SIZE;
        val = s->chan[n].regs[(addr % FSL_DMA_CHAN_SIZE) / 4];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
    trace_fsl_dma_read(addr, val);
    return val;
}

static void fsl_dma_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    FslDmaState *s = opaque;
    FslDmaChannel *ch;
    hwaddr reg;
    uint32_t old;
    int n;

    trace_fsl_dma_write(addr, val);
    if (addr >= FSL_DMA_NUM_CHANNELS * FSL_DMA_CHAN_SIZE ||
        (addr % FSL_DMA_CHAN_SIZE) >= DMA_NUM_REGS * 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    n = addr / FSL_DMA_CHAN_SIZE;
    ch = &s->chan[n];
    reg = addr % FSL_DMA_CHAN_SIZE;
    old = ch->regs[reg / 4];

    switch (reg) {
    case DMA_MR:
        ch->regs[DMA_MR / 4] = val & ~MR_CA;
        if (val & MR_CA) {
            ch->regs[DMA_SR / 4] &= ~SR_CB;
        } else if ((val & MR_CS) && !(old & MR_CS) &&
                   (ch->regs[DMA_SR / 4] & SR_CB) && fsl_dma_paced(ch)) {
            /* Resume a paced transfer that clearing CS paused */
            if (fsl_dma_ready(ch)) {
                qemu_bh_schedule(s->bh);
            }
        } else if ((val & MR_CS) && !(old & MR_CS)) {
            fsl_dma_start(s, n, false);
        } else if ((val & MR_CC) && !(old & MR_CC)) {
            fsl_dma_start(s, n, true);
        }
        fsl_dma_update_irq(ch);
        break;
    case DMA_SR:
        ch->regs[DMA_SR / 4] &= ~(val & SR_W1C);
        fsl_dma_update_irq(ch);
        break;
    case DMA_ECLSDAR:
    case DMA_CLSDAR:
    case DMA_ENLSDAR:
    case DMA_NLSDAR:
        if (val) {
            qemu_log_mask(LOG_UNIMP, "fsl-dma: list descriptors are not "
                          "implemented\n");
        }
        ch->regs[reg / 4] = val;
        break;
    default:
        ch->regs[reg / 4] = val;
        break;
    }
}

static const MemoryRegionOps fsl_dma_ops = {
    .read = fsl_dma_read,
    .write = fsl_dma_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fsl_dma_reset(DeviceState *dev)
{
    FslDmaState *s = FSL_DMA(dev);
    int n;

    for (n = 0; n < FSL_DMA_NUM_CHANNELS; n++) {
        memset(s->chan[n].regs, 0, sizeof(s->chan[n].regs));
        qemu_irq_lower(s->chan[n].irq);
        /* DREQ inputs keep the level their peripherals drive */
    }
}

static int fsl_dma_post_load(void *opaque, int version_id)
{
    FslDmaState *s = opaque;
    int n;

    for (n = 0; n < FSL_DMA_NUM_CHANNELS; n++) {
        if (fsl_dma_ready(&s->chan[n])) {
            qemu_bh_schedule(s->bh);
        }
    }
    return 0;
}

static const VMStateDescription vmstate_fsl_dma_chan = {
    .name = "fsl-dma-channel",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FslDmaChannel, DMA_NUM_REGS),
        VMSTATE_BOOL(dreq, FslDmaChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_fsl_dma = {
    .name = TYPE_FSL_DMA,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = fsl_dma_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(chan, FslDmaState, FSL_DMA_NUM_CHANNELS, 2,
                             vmstate_fsl_dma_chan, FslDmaChannel),
        VMSTATE_END_OF_LIST()
    }
};

static void fsl_dma_init(Object *obj)
{
    FslDmaState *s = FSL_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int n;

    memory_region_init_io(&s->iomem, obj, &fsl_dma_ops, s, TYPE_FSL_DMA,
                          FSL_DMA_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    for (n = 0; n < FSL_DMA_NUM_CHANNELS; n++) {
        sysbus_init_irq(sbd, &s->chan[n].irq);
    }
    qdev_init_gpio_in_named(DEVICE(obj), fsl_dma_set_dreq, FSL_DMA_DREQ,
                            FSL_DMA_NUM_CHANNELS);
}

static void fsl_dma_realize(DeviceState *dev, Error **errp)
{
    FslDmaState *s = FSL_DMA(dev);

    s->bh = qemu_bh_new_guarded(fsl_dma_pace, s,
                                &dev->mem_reentrancy_guard);
}

static void fsl_dma_unrealize(DeviceState *dev)
{
    FslDmaState *s = FSL_DMA(dev);

    qemu_bh_delete(s->bh);
}

static void fsl_dma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Freescale Elo Plus DMA controller";
    dc->vmsd = &vmstate_fsl_dma;
    dc->realize = fsl_dma_realize;
    dc->unrealize = fsl_dma_unrealize;
    device_class_set_legacy_reset(dc, fsl_dma_reset);
}

static const TypeInfo fsl_dma_types[] = {
    {
        .name          = TYPE_FSL_DMA,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslDmaState),
        .instance_init = fsl_dma_init,
        .class_init    = fsl_dma_class_init,
    },
};

DEFINE_TYPES(fsl_dma_types)
