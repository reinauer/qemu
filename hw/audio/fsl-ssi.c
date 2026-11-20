/*
 * Freescale Synchronous Serial Interface (SSI)
 *
 * The audio interface of the MPC8610, P1022 and i.MX SoCs, as Linux's
 * sound/soc/fsl/fsl_ssi.h describes it: a serial port for I2S, AC97 and
 * network (TDM) mode codecs, with two 15-word FIFOs each way, which
 * software fills directly or through DMA, as the FIFO watermarks request.
 *
 * Emulated: transmitting to and receiving from a WM8776 codec in I2S (or
 * normal) mode, with one FIFO or both each way, the FIFO empty and full
 * DMA requests, and the status and interrupt registers.  The codec takes
 * stereo frames as fast as it has room for them, and the transmitter
 * requests data as long as the FIFO holding the left channel is below its
 * watermark; the receiver takes the frames the codec has recorded as long
 * as its FIFOs have room, and requests DMA once the left channel's FIFO
 * reaches its watermark.  Samples are the word length's low bits of each
 * word, sign-extended, as the AmigaOS A1222 driver's 24-bit samples with
 * STCR[TXBIT0] and SRCR[RXBIT0] clear show.  Not emulated: AC97 and
 * network mode, which work like I2S mode, the clock generator, and
 * transmit underruns.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/audio/fsl-ssi.h"
#include "hw/audio/wm8776.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

#define SSI_STX0            0x00
#define SSI_STX1            0x04
#define SSI_SRX0            0x08
#define SSI_SRX1            0x0c
#define SSI_SCR             0x10
#define SSI_SISR            0x14
#define SSI_SIER            0x18
#define SSI_STCR            0x1c
#define SSI_SRCR            0x20
#define SSI_STCCR           0x24
#define SSI_SRCCR           0x28
#define SSI_SFCSR           0x2c
#define SSI_STR             0x30
#define SSI_SOR             0x34
#define SSI_SACNT           0x38
#define SSI_SACADD          0x3c
#define SSI_SACDAT          0x40
#define SSI_SATAG           0x44
#define SSI_STMSK           0x48
#define SSI_SRMSK           0x4c
#define SSI_SACCST          0x50
#define SSI_SACCEN          0x54
#define SSI_SACCDIS         0x58
#define SSI_NUM_REGS        (0x5c / 4)
#define SSI_MMIO_SIZE       0x100

#define SCR_SSIEN           0x00000001
#define SCR_TE              0x00000002
#define SCR_RE              0x00000004

/* SISR bits, and their SIER interrupt enables */
#define SISR_TFE0           0x00000001
#define SISR_TFE1           0x00000002
#define SISR_RFF0           0x00000004
#define SISR_RFF1           0x00000008
#define SISR_RLS            0x00000010
#define SISR_TLS            0x00000020
#define SISR_RFS            0x00000040
#define SISR_TFS            0x00000080
#define SISR_TUE0           0x00000100
#define SISR_TUE1           0x00000200
#define SISR_ROE0           0x00000400
#define SISR_ROE1           0x00000800
#define SISR_TDE0           0x00001000
#define SISR_TDE1           0x00002000
#define SISR_RDR0           0x00004000
#define SISR_RDR1           0x00008000
#define SISR_RXT            0x00010000
#define SISR_CMDDU          0x00020000
#define SISR_CMDAU          0x00040000
#define SISR_TRFC           0x00800000
#define SISR_RFRC           0x01000000
#define SISR_MASK           0x01ffffff
#define SISR_W1C            (SISR_RLS | SISR_TLS | SISR_RFS | SISR_TFS | \
                             SISR_TUE0 | SISR_TUE1 | SISR_ROE0 | SISR_ROE1 | \
                             SISR_RXT | SISR_CMDDU | SISR_CMDAU | \
                             SISR_TRFC | SISR_RFRC)
#define SISR_TX             (SISR_TFE0 | SISR_TFE1 | SISR_TLS | SISR_TFS | \
                             SISR_TUE0 | SISR_TUE1 | SISR_TDE0 | SISR_TDE1)
#define SISR_RX             (SISR_RFF0 | SISR_RFF1 | SISR_RLS | SISR_RFS | \
                             SISR_ROE0 | SISR_ROE1 | SISR_RDR0 | SISR_RDR1 | \
                             SISR_RXT)

#define SIER_TIE            0x00080000
#define SIER_TDMAE          0x00100000
#define SIER_RIE            0x00200000
#define SIER_RDMAE          0x00400000

#define STCR_TFEN0          0x00000080
#define STCR_TFEN1          0x00000100
#define SRCR_RFEN1          0x00000100

#define SxCCR_WL(v)         ((((v) >> 13) & 0xf) * 2 + 2)

#define SFCSR_TFWM0(v)      ((v) & 0xf)
#define SFCSR_RFWM0(v)      (((v) >> 4) & 0xf)
#define SFCSR_RFWM1(v)      (((v) >> 20) & 0xf)
#define SFCSR_CNT_MASK      0xff00ff00
#define SFCSR_TFCNT0_SHIFT  8
#define SFCSR_RFCNT0_SHIFT  12
#define SFCSR_TFCNT1_SHIFT  24
#define SFCSR_RFCNT1_SHIFT  28

#define SOR_TX_CLR          0x00000010
#define SOR_RX_CLR          0x00000020

#define SSI_FIFO_DEPTH      15

typedef struct FslSsiFifo {
    uint32_t data[SSI_FIFO_DEPTH];
    uint32_t head;
    uint32_t count;
} FslSsiFifo;

struct FslSsiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq dreq[2];
    WM8776State *codec;

    uint32_t regs[SSI_NUM_REGS];
    /* The sticky SISR bits */
    uint32_t sisr;
    FslSsiFifo txfifo[2];
    FslSsiFifo rxfifo[2];
    uint32_t rxlast[2];
};

static void fsl_ssi_fifo_push(FslSsiFifo *f, uint32_t val)
{
    if (f->count < SSI_FIFO_DEPTH) {
        f->data[(f->head + f->count) % SSI_FIFO_DEPTH] = val;
        f->count++;
    }
}

static uint32_t fsl_ssi_fifo_pop(FslSsiFifo *f)
{
    uint32_t val = f->data[f->head];

    f->head = (f->head + 1) % SSI_FIFO_DEPTH;
    f->count--;
    return val;
}

static void fsl_ssi_fifo_clear(FslSsiFifo *f)
{
    f->head = 0;
    f->count = 0;
}

static bool fsl_ssi_dual_fifo(FslSsiState *s)
{
    return s->regs[SSI_STCR / 4] & STCR_TFEN1;
}

static bool fsl_ssi_rx_dual_fifo(FslSsiState *s)
{
    return s->regs[SSI_SRCR / 4] & SRCR_RFEN1;
}

static bool fsl_ssi_tx_enabled(FslSsiState *s)
{
    uint32_t scr = s->regs[SSI_SCR / 4];

    return (scr & SCR_SSIEN) && (scr & SCR_TE);
}

static bool fsl_ssi_rx_enabled(FslSsiState *s)
{
    uint32_t scr = s->regs[SSI_SCR / 4];

    return (scr & SCR_SSIEN) && (scr & SCR_RE);
}

/* The status register, with the FIFO and data register state */
static uint32_t fsl_ssi_sisr(FslSsiState *s)
{
    uint32_t sisr = s->sisr;
    uint32_t wm = MAX(SFCSR_TFWM0(s->regs[SSI_SFCSR / 4]), 1);
    int i;

    uint32_t rwm[2] = {
        MAX(SFCSR_RFWM0(s->regs[SSI_SFCSR / 4]), 1),
        MAX(SFCSR_RFWM1(s->regs[SSI_SFCSR / 4]), 1),
    };

    for (i = 0; i < 2; i++) {
        uint32_t room = SSI_FIFO_DEPTH - s->txfifo[i].count;

        if (room) {
            sisr |= SISR_TDE0 << i;
        }
        if (room >= wm) {
            sisr |= SISR_TFE0 << i;
        }
        if (s->rxfifo[i].count) {
            sisr |= SISR_RDR0 << i;
        }
        if (s->rxfifo[i].count >= rwm[i]) {
            sisr |= SISR_RFF0 << i;
        }
    }
    return sisr;
}

static void fsl_ssi_update(FslSsiState *s)
{
    uint32_t sisr = fsl_ssi_sisr(s);
    uint32_t sier = s->regs[SSI_SIER / 4];
    uint32_t pending = sisr & sier & SISR_MASK;
    bool tx_dreq, rx_dreq;

    /* With DMA, the FIFO levels request data rather than interrupt */
    if (sier & SIER_TDMAE) {
        pending &= ~(SISR_TFE0 | SISR_TFE1);
    }
    if (sier & SIER_RDMAE) {
        pending &= ~(SISR_RFF0 | SISR_RFF1);
    }
    if (!(sier & SIER_TIE)) {
        pending &= ~SISR_TX;
    }
    if (!(sier & SIER_RIE)) {
        pending &= ~SISR_RX;
    }
    qemu_set_irq(s->irq, pending != 0);

    /* Both FIFOs fill alike, so the first one's watermark paces DMA */
    tx_dreq = fsl_ssi_tx_enabled(s) && (sier & SIER_TDMAE) &&
              (sier & (SISR_TFE0 | SISR_TFE1)) && (sisr & SISR_TFE0);
    rx_dreq = fsl_ssi_rx_enabled(s) && (sier & SIER_RDMAE) &&
              (sier & (SISR_RFF0 | SISR_RFF1)) && (sisr & SISR_RFF0);
    qemu_set_irq(s->dreq[FSL_SSI_DREQ_TX], tx_dreq);
    qemu_set_irq(s->dreq[FSL_SSI_DREQ_RX], rx_dreq);
}

/* A transmitted word as a signed 32-bit sample */
static int32_t fsl_ssi_sample(FslSsiState *s, uint32_t word)
{
    int wl = SxCCR_WL(s->regs[SSI_STCCR / 4]);

    if (wl >= 32) {
        return word;
    }
    return sextract32(word, 0, wl) * (1 << (32 - wl));
}

/* Hand the codec the frames it has room for */
static void fsl_ssi_transmit(FslSsiState *s)
{
    FslSsiFifo *f0 = &s->txfifo[0];
    FslSsiFifo *f1 = fsl_ssi_dual_fifo(s) ? &s->txfifo[1] : &s->txfifo[0];

    if (!fsl_ssi_tx_enabled(s)) {
        return;
    }
    while (wm8776_dac_space(s->codec) > 0 &&
           (f0 == f1 ? f0->count >= 2 : f0->count && f1->count)) {
        int32_t left = fsl_ssi_sample(s, fsl_ssi_fifo_pop(f0));
        int32_t right = fsl_ssi_sample(s, fsl_ssi_fifo_pop(f1));

        wm8776_dac_frame(s->codec, left, right);
    }
}

/* A received sample as a word of the word length */
static uint32_t fsl_ssi_word(FslSsiState *s, int32_t sample)
{
    int wl = SxCCR_WL(s->regs[SSI_SRCCR / 4]);

    return wl >= 32 ? sample : sample >> (32 - wl);
}

/* Take the frames the codec has recorded that the FIFOs have room for */
static void fsl_ssi_receive(FslSsiState *s)
{
    FslSsiFifo *f0 = &s->rxfifo[0];
    FslSsiFifo *f1 = fsl_ssi_rx_dual_fifo(s) ? &s->rxfifo[1] : &s->rxfifo[0];

    if (!fsl_ssi_rx_enabled(s)) {
        return;
    }
    while (wm8776_adc_avail(s->codec) > 0 &&
           (f0 == f1 ? f0->count + 2 <= SSI_FIFO_DEPTH :
            f0->count < SSI_FIFO_DEPTH && f1->count < SSI_FIFO_DEPTH)) {
        int32_t left, right;

        wm8776_adc_frame(s->codec, &left, &right);
        fsl_ssi_fifo_push(f0, fsl_ssi_word(s, left));
        fsl_ssi_fifo_push(f1, fsl_ssi_word(s, right));
    }
    /* The codec lost frames while the FIFOs were full */
    if (wm8776_adc_overrun(s->codec)) {
        s->sisr |= SISR_ROE0 | (f0 != f1 ? SISR_ROE1 : 0);
    }
}

static void fsl_ssi_update_adc(FslSsiState *s)
{
    wm8776_adc_enable(s->codec, fsl_ssi_rx_enabled(s));
}

/* The codec has taken or recorded samples */
static void fsl_ssi_data_req(void *opaque)
{
    FslSsiState *s = opaque;

    fsl_ssi_transmit(s);
    fsl_ssi_receive(s);
    fsl_ssi_update(s);
}

static void fsl_ssi_clear_tx(FslSsiState *s)
{
    fsl_ssi_fifo_clear(&s->txfifo[0]);
    fsl_ssi_fifo_clear(&s->txfifo[1]);
}

static void fsl_ssi_clear_rx(FslSsiState *s)
{
    fsl_ssi_fifo_clear(&s->rxfifo[0]);
    fsl_ssi_fifo_clear(&s->rxfifo[1]);
}

/* Read a receive data register, which an empty FIFO repeats */
static uint32_t fsl_ssi_read_rx(FslSsiState *s, int i)
{
    FslSsiFifo *f;

    if (!fsl_ssi_rx_dual_fifo(s)) {
        i = 0;
    }
    f = &s->rxfifo[i];
    if (f->count) {
        s->rxlast[i] = fsl_ssi_fifo_pop(f);
        fsl_ssi_receive(s);
    }
    return s->rxlast[i];
}

static uint64_t fsl_ssi_read(void *opaque, hwaddr addr, unsigned size)
{
    FslSsiState *s = opaque;
    uint32_t val;

    switch (addr) {
    case SSI_SRX0:
    case SSI_SRX1:
        val = fsl_ssi_read_rx(s, addr == SSI_SRX1);
        fsl_ssi_update(s);
        break;
    case SSI_SISR:
        val = fsl_ssi_sisr(s);
        break;
    case SSI_SFCSR:
        val = (s->regs[SSI_SFCSR / 4] & ~SFCSR_CNT_MASK) |
              s->txfifo[0].count << SFCSR_TFCNT0_SHIFT |
              s->rxfifo[0].count << SFCSR_RFCNT0_SHIFT |
              s->txfifo[1].count << SFCSR_TFCNT1_SHIFT |
              s->rxfifo[1].count << SFCSR_RFCNT1_SHIFT;
        break;
    default:
        if (addr >= SSI_NUM_REGS * 4) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx
                          "\n", __func__, addr);
            val = 0;
            break;
        }
        val = s->regs[addr / 4];
        break;
    }
    trace_fsl_ssi_read(addr, val);
    return val;
}

static void fsl_ssi_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    FslSsiState *s = opaque;

    if (addr != SSI_STX0 && addr != SSI_STX1) {
        trace_fsl_ssi_write(addr, val);
    }
    switch (addr) {
    case SSI_STX0:
    case SSI_STX1:
        if (s->regs[SSI_SCR / 4] & SCR_SSIEN) {
            int i = addr == SSI_STX1 && fsl_ssi_dual_fifo(s);

            fsl_ssi_fifo_push(&s->txfifo[i], val);
            fsl_ssi_transmit(s);
        }
        break;
    case SSI_SRX0:
    case SSI_SRX1:
        break;
    case SSI_SCR:
        s->regs[SSI_SCR / 4] = val;
        if (!(val & SCR_SSIEN)) {
            /* Disabling the SSI resets its FIFOs */
            fsl_ssi_clear_tx(s);
            fsl_ssi_clear_rx(s);
        }
        fsl_ssi_update_adc(s);
        fsl_ssi_transmit(s);
        fsl_ssi_receive(s);
        break;
    case SSI_SISR:
        s->sisr &= ~(val & SISR_W1C);
        break;
    case SSI_SFCSR:
        s->regs[SSI_SFCSR / 4] = val & ~SFCSR_CNT_MASK;
        break;
    case SSI_SOR:
        if (val & SOR_TX_CLR) {
            fsl_ssi_clear_tx(s);
        }
        if (val & SOR_RX_CLR) {
            fsl_ssi_clear_rx(s);
        }
        s->regs[SSI_SOR / 4] = val & ~(SOR_TX_CLR | SOR_RX_CLR);
        break;
    case SSI_SACNT:
        if (val & 1) {
            qemu_log_mask(LOG_UNIMP, "fsl-ssi: AC97 mode is not "
                          "implemented\n");
        }
        s->regs[SSI_SACNT / 4] = val;
        break;
    case SSI_SACCST:
        break;
    default:
        if (addr >= SSI_NUM_REGS * 4) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx
                          "\n", __func__, addr);
            return;
        }
        s->regs[addr / 4] = val;
        break;
    }
    fsl_ssi_update(s);
}

static const MemoryRegionOps fsl_ssi_ops = {
    .read = fsl_ssi_read,
    .write = fsl_ssi_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fsl_ssi_reset(DeviceState *dev)
{
    FslSsiState *s = FSL_SSI(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[SSI_SIER / 4] = SISR_TFE0 | SISR_TFE1 | SISR_TDE0 | SISR_TDE1;
    s->regs[SSI_STCR / 4] = 0x00000200;
    s->regs[SSI_SRCR / 4] = 0x00000200;
    s->regs[SSI_STCCR / 4] = 0x00040000;
    s->regs[SSI_SRCCR / 4] = 0x00040000;
    s->regs[SSI_SFCSR / 4] = 0x00810081;
    s->sisr = 0;
    s->rxlast[0] = 0;
    s->rxlast[1] = 0;
    fsl_ssi_clear_tx(s);
    fsl_ssi_clear_rx(s);
    fsl_ssi_update_adc(s);
    fsl_ssi_update(s);
}

static void fsl_ssi_realize(DeviceState *dev, Error **errp)
{
    FslSsiState *s = FSL_SSI(dev);

    if (!s->codec) {
        error_setg(errp, "the codec property must be set");
        return;
    }
    wm8776_set_data_req(s->codec, fsl_ssi_data_req, s);
}

static void fsl_ssi_init(Object *obj)
{
    FslSsiState *s = FSL_SSI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &fsl_ssi_ops, s, TYPE_FSL_SSI,
                          SSI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->dreq, FSL_SSI_DREQ, 2);
}

static int fsl_ssi_post_load(void *opaque, int version_id)
{
    FslSsiState *s = opaque;
    int i;

    for (i = 0; i < 2; i++) {
        if (s->txfifo[i].head >= SSI_FIFO_DEPTH ||
            s->txfifo[i].count > SSI_FIFO_DEPTH ||
            s->rxfifo[i].head >= SSI_FIFO_DEPTH ||
            s->rxfifo[i].count > SSI_FIFO_DEPTH) {
            return -EINVAL;
        }
    }
    return 0;
}

static const VMStateDescription vmstate_fsl_ssi_fifo = {
    .name = "fsl-ssi-fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(data, FslSsiFifo, SSI_FIFO_DEPTH),
        VMSTATE_UINT32(head, FslSsiFifo),
        VMSTATE_UINT32(count, FslSsiFifo),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_fsl_ssi = {
    .name = TYPE_FSL_SSI,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = fsl_ssi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FslSsiState, SSI_NUM_REGS),
        VMSTATE_UINT32(sisr, FslSsiState),
        VMSTATE_STRUCT_ARRAY(txfifo, FslSsiState, 2, 1,
                             vmstate_fsl_ssi_fifo, FslSsiFifo),
        VMSTATE_STRUCT_ARRAY(rxfifo, FslSsiState, 2, 1,
                             vmstate_fsl_ssi_fifo, FslSsiFifo),
        VMSTATE_UINT32_ARRAY(rxlast, FslSsiState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const Property fsl_ssi_properties[] = {
    DEFINE_PROP_LINK("codec", FslSsiState, codec, TYPE_WM8776,
                     WM8776State *),
};

static void fsl_ssi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Freescale Synchronous Serial Interface";
    dc->realize = fsl_ssi_realize;
    dc->vmsd = &vmstate_fsl_ssi;
    device_class_set_legacy_reset(dc, fsl_ssi_reset);
    device_class_set_props(dc, fsl_ssi_properties);
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
}

static const TypeInfo fsl_ssi_types[] = {
    {
        .name          = TYPE_FSL_SSI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslSsiState),
        .instance_init = fsl_ssi_init,
        .class_init    = fsl_ssi_class_init,
    },
};

DEFINE_TYPES(fsl_ssi_types)
