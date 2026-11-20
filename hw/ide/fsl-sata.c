/*
 * Freescale eSATA controller ("fsl,pq-sata-v2")
 *
 * The SATA controller of the MPC8315, MPC837x, P1022 and related SoCs: one
 * port per controller, a 16-slot command list in system memory, and register
 * blocks for the host controller (0x000), the SATA superset/SCR registers
 * (0x100) and link/transport control (0x140).  All registers and in-memory
 * structures are little-endian, even on these big-endian SoCs.
 *
 * Software queues a command by filling in a 16-byte command header in the
 * list at CHBA and a command descriptor it points to, and writing the slot's
 * bit to CQ.  The descriptor holds the H2D FIS, a space where the controller
 * stores the device's final status FIS, an ATAPI packet and the PRD table.
 * The controller sends the FIS, moves the data through the PRD table for
 * both DMA and PIO protocols, and reports completion in CC, or an error in
 * CE/DE, with interrupts summarised in HSTATUS.
 *
 * The register layout and behaviour follow Linux's drivers/ata/sata_fsl.c,
 * which describes the controller in detail; the device behind the port is
 * QEMU's IDE core, driven from the FIS much like the AHCI model does.  Not
 * emulated: native command queuing (the IDE core doesn't offer it here), port
 * multipliers, BIST, interrupt coalescing and the legacy register interface.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/ide/fsl-sata.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "ide-internal.h"
#include "trace.h"

#define FSL_SATA_MMIO_SIZE      0x1000
#define FSL_SATA_NUM_SLOTS      16
#define FSL_SATA_SLOT_MASK      ((1u << FSL_SATA_NUM_SLOTS) - 1)

/* Host controller registers */
#define REG_CQ                  0x000   /* command queue */
#define REG_CA                  0x008   /* command active */
#define REG_CC                  0x010   /* command completed */
#define REG_CE                  0x018   /* command error */
#define REG_DE                  0x020   /* device error */
#define REG_CHBA                0x024   /* command header base address */
#define REG_HSTATUS             0x028
#define REG_HCONTROL            0x02c
#define REG_CQPMP               0x030   /* port multiplier port of CQ */
#define REG_SIGNATURE           0x034
#define REG_ICC                 0x038   /* interrupt coalescing control */

/* SATA superset (SCR) registers */
#define REG_SSTATUS             0x100
#define REG_SERROR              0x104
#define REG_SCONTROL            0x108
#define REG_SNOTIFY             0x10c

/* Link/transport layer control and status registers, which we just store */
#define REG_CSR_BASE            0x140
#define REG_COMMANDSTAT         0x160
#define FSL_SATA_NUM_CSR        ((REG_COMMANDSTAT - REG_CSR_BASE) / 4 + 1)

/* HSTATUS */
#define HSTATUS_ONLINE          (1u << 31)
#define HSTATUS_INT_PHYRDY_CHG  (1u << 4)
#define HSTATUS_INT_SIGNATURE   (1u << 3)
#define HSTATUS_INT_SNOTIFY     (1u << 2)
#define HSTATUS_INT_DEVICE_ERR  (1u << 1)
#define HSTATUS_INT_CMD_DONE    (1u << 0)
#define HSTATUS_INT_MASK        0x3f
#define HSTATUS_FATAL_MASK      0x7ff00 /* bits 8..18, written 1 to clear */

/* HCONTROL; bits 5..0 enable the matching HSTATUS interrupts */
#define HCONTROL_ONLINE_PHY_RST (1u << 31)
#define HCONTROL_FORCE_OFFLINE  (1u << 30)
#define HCONTROL_CLEAR_ERROR    (1u << 27)

/* SStatus/SControl */
#define SSTATUS_DET_PHY_ONLINE  0x3
#define SSTATUS_SPD_SHIFT       4
#define SSTATUS_IPM_ACTIVE      (1u << 8)
#define SCONTROL_SPD_SHIFT      4

/* Command header, 16 bytes per slot at CHBA */
#define CMDHDR_SIZE             16
#define CMDHDR_PRDE_COUNT(x)    (((x) >> 16) & 0x3f)

/* Command header descriptor information */
#define DESC_BIST_VENDOR        (1u << 10)
#define DESC_FPDMA_QUEUED       (1u << 8)
#define DESC_SRST               (1u << 7)
#define DESC_BIST               (1u << 6)
#define DESC_ATAPI              (1u << 5)

/* Command descriptor layout */
#define CMDDESC_CFIS            0x00
#define CMDDESC_SFIS            0x20
#define CMDDESC_ACMD            0x40
#define CMDDESC_PRDT            0x60
#define FIS_SIZE                32
#define ACMD_SIZE               16

/* Physical region descriptor entries, 16 bytes each */
#define PRDE_SIZE               16
#define PRDE_EXT                (1u << 31) /* points to a further PRD table */
#define PRDE_DBC_MASK           0x3fffff

#define SATA_FIS_REG_H2D        0x27
#define SATA_FIS_REG_D2H        0x34
#define SATA_FIS_H2D_COMMAND    0x80

/* Device control register, in byte 15 of an H2D register FIS */
#define ATA_SRST                0x04

struct FslSataState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    IDEBus bus;
    IDEDMA dma;
    AddressSpace *as;
    QEMUBH *cmd_bh;
    MemReentrancyGuard mem_reentrancy_guard;

    uint32_t cq;
    uint32_t ca;
    uint32_t cc;
    uint32_t ce;
    uint32_t de;
    uint32_t chba;
    uint32_t hstatus;
    uint32_t hcontrol;
    uint32_t cqpmp;
    uint32_t signature;
    uint32_t icc;
    uint32_t sstatus;
    uint32_t serror;
    uint32_t scontrol;
    uint32_t snotify;
    uint32_t csr[FSL_SATA_NUM_CSR];

    /* The command the device is executing, if busy_slot isn't -1 */
    int32_t busy_slot;
    uint32_t cur_cda;
    uint32_t cur_desc_info;
    uint8_t cur_prde_count;
    uint8_t cur_ata_cmd;
    bool done_first_drq;
    /* Sent an H2D control FIS setting SRST, awaiting the one clearing it */
    bool srst_asserted;
};

static void fsl_sata_update_irq(FslSataState *s)
{
    qemu_set_irq(s->irq, !!(s->hstatus & s->hcontrol & HSTATUS_INT_MASK));
}

static bool fsl_sata_has_device(FslSataState *s)
{
    return s->bus.ifs[0].blk != NULL;
}

static void fsl_sata_kick(FslSataState *s)
{
    qemu_bh_schedule(s->cmd_bh);
}

/* Signature of the device's first D2H register FIS after a reset */
static void fsl_sata_update_signature(FslSataState *s)
{
    IDEState *ide = &s->bus.ifs[0];

    s->signature = ((uint32_t)ide->hcyl << 24) | (ide->lcyl << 16) |
                   (ide->sector << 8) | (ide->nsector & 0xff);
    s->hstatus |= HSTATUS_INT_SIGNATURE;
    trace_fsl_sata_signature(s, s->signature);
}

/* Store the device's status, as a D2H register FIS, in the descriptor */
static void fsl_sata_write_sfis(FslSataState *s)
{
    IDEState *ide = &s->bus.ifs[0];
    uint8_t fis[FIS_SIZE] = {
        [0] = SATA_FIS_REG_D2H,
        [1] = 1 << 6,               /* interrupt */
        [2] = ide->status,
        [3] = ide->error,
        [4] = ide->sector,
        [5] = ide->lcyl,
        [6] = ide->hcyl,
        [7] = ide->select,
        [8] = ide->hob_sector,
        [9] = ide->hob_lcyl,
        [10] = ide->hob_hcyl,
        [12] = ide->nsector & 0xff,
        [13] = (ide->nsector >> 8) & 0xff,
    };

    dma_memory_write(s->as, s->cur_cda + CMDDESC_SFIS, fis, sizeof(fis),
                     MEMTXATTRS_UNSPECIFIED);
}

/* Retire the command in @slot, successfully or with a device error */
static void fsl_sata_finish_slot(FslSataState *s, int slot, bool error)
{
    trace_fsl_sata_cmd_done(s, slot, error);

    s->ca &= ~(1u << slot);
    if (error) {
        /*
         * The controller stops processing the queue until software clears
         * CE, see fsl_sata_cmd_bh().
         */
        s->ce |= 1u << slot;
        s->de |= 1u << (s->cqpmp & 0xf);
        s->hstatus |= HSTATUS_INT_DEVICE_ERR;
    } else {
        s->cc |= 1u << slot;
        s->hstatus |= HSTATUS_INT_CMD_DONE;
    }
    fsl_sata_update_irq(s);
    fsl_sata_kick(s);
}

/* The device finished the command in busy_slot */
static void fsl_sata_complete(FslSataState *s)
{
    IDEState *ide = &s->bus.ifs[0];
    int slot = s->busy_slot;

    if (slot < 0) {
        return;
    }
    s->busy_slot = -1;
    fsl_sata_write_sfis(s);
    fsl_sata_finish_slot(s, slot, ide->status & ERR_STAT);
}

/*
 * Turn the PRD table of the current command into a scatter/gather list of
 * up to @limit bytes, starting @offset bytes into the transfer.
 */
static int fsl_sata_build_sglist(FslSataState *s, QEMUSGList *sg,
                                 int64_t limit, uint64_t offset)
{
    dma_addr_t prde_addr = s->cur_cda + CMDDESC_PRDT;
    uint64_t pos = 0;
    int i;

    qemu_sglist_init(sg, DEVICE(s), s->cur_prde_count, s->as);

    /*
     * The count in the command header includes entries that link to a
     * further table (the controller only has room for 16 entries in the
     * descriptor itself), which bounds the walk.
     */
    for (i = 0; i < s->cur_prde_count && sg->size < limit; i++) {
        uint32_t prde[PRDE_SIZE / 4];
        uint32_t dba, ddc, len;

        if (dma_memory_read(s->as, prde_addr, prde, sizeof(prde),
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "fsl-sata: can't read PRD at 0x%"
                          HWADDR_PRIx "\n", (hwaddr)prde_addr);
            break;
        }
        dba = le32_to_cpu(prde[0]);
        ddc = le32_to_cpu(prde[3]);

        if (ddc & PRDE_EXT) {
            prde_addr = dba & ~3u;
            continue;
        }
        prde_addr += PRDE_SIZE;

        len = ddc & PRDE_DBC_MASK;
        if (offset < pos + len) {
            uint64_t skip = offset > pos ? offset - pos : 0;

            qemu_sglist_add(sg, dba + skip,
                            MIN(len - skip, limit - sg->size));
        }
        pos += len;
    }

    trace_fsl_sata_sglist(s, s->cur_prde_count, limit, offset, sg->size);
    if (!sg->size) {
        qemu_sglist_destroy(sg);
        return -1;
    }
    return 0;
}

/*
 * The command header has no direction bit: the controller learns it from the
 * device's PIO Setup FIS.  Our device is the IDE core, so tell PIO data-out
 * commands from data-in ones by their opcode.  ATAPI PIO data is always
 * data-in, as QEMU's ATAPI emulation has no data-out commands.
 */
static bool fsl_sata_is_pio_out(uint8_t cmd)
{
    switch (cmd) {
    case WIN_WRITE:
    case WIN_WRITE_ONCE:
    case WIN_WRITE_EXT:
    case CFA_WRITE_SECT_WO_ERASE:
    case WIN_MULTWRITE_EXT:
    case WIN_WRITE_VERIFY:
    case WIN_MULTWRITE:
    case CFA_WRITE_MULTI_WO_ERASE:
        return true;
    default:
        return false;
    }
}

/* IDEDMAOps */

static bool fsl_sata_pio_transfer(const IDEDMA *dma)
{
    FslSataState *s = container_of(dma, FslSataState, dma);
    IDEState *ide = &s->bus.ifs[0];
    uint32_t size = ide->data_end - ide->data_ptr;
    bool is_atapi = s->cur_desc_info & DESC_ATAPI;
    bool is_write = !is_atapi && fsl_sata_is_pio_out(s->cur_ata_cmd);

    if (s->busy_slot < 0) {
        return false;
    }

    /* The ATAPI packet itself came from the descriptor's ACMD field */
    if (is_atapi && !s->done_first_drq) {
        goto out;
    }

    trace_fsl_sata_pio_transfer(s, is_write, size);
    if (size && fsl_sata_build_sglist(s, &ide->sg, size,
                                      ide->io_buffer_offset) == 0) {
        if (is_write) {
            dma_buf_write(ide->data_ptr, size, NULL, &ide->sg,
                          MEMTXATTRS_UNSPECIFIED);
        } else {
            dma_buf_read(ide->data_ptr, size, NULL, &ide->sg,
                         MEMTXATTRS_UNSPECIFIED);
        }
    }
    ide_dma_buf_commit(ide, size);

out:
    ide->data_ptr = ide->data_end;
    s->done_first_drq = true;
    return true;
}

static void fsl_sata_start_dma(const IDEDMA *dma, IDEState *ide,
                               BlockCompletionFunc *dma_cb)
{
    ide->io_buffer_offset = 0;
    dma_cb(ide, 0);
}

static int32_t fsl_sata_prepare_buf(const IDEDMA *dma, int32_t limit)
{
    FslSataState *s = container_of(dma, FslSataState, dma);
    IDEState *ide = &s->bus.ifs[0];

    if (fsl_sata_build_sglist(s, &ide->sg, limit,
                              ide->io_buffer_offset) < 0) {
        ide->io_buffer_size = 0;
        return -1;
    }
    ide->io_buffer_size = ide->sg.size;
    return ide->io_buffer_size;
}

static int fsl_sata_rw_buf(const IDEDMA *dma, bool is_write)
{
    FslSataState *s = container_of(dma, FslSataState, dma);
    IDEState *ide = &s->bus.ifs[0];
    uint8_t *p = ide->io_buffer + ide->io_buffer_index;
    int len = ide->io_buffer_size - ide->io_buffer_index;

    if (fsl_sata_build_sglist(s, &ide->sg, len, ide->io_buffer_offset) < 0) {
        return 0;
    }

    /* is_write means from guest memory to the device */
    if (is_write) {
        dma_buf_read(p, len, NULL, &ide->sg, MEMTXATTRS_UNSPECIFIED);
    } else {
        dma_buf_write(p, len, NULL, &ide->sg, MEMTXATTRS_UNSPECIFIED);
    }
    ide_dma_buf_commit(ide, len);
    ide->io_buffer_index += len;
    return 1;
}

static void fsl_sata_restart_dma(const IDEDMA *dma)
{
    /* fsl_sata_start_dma() resets io_buffer_offset */
}

static void fsl_sata_cmd_done(const IDEDMA *dma)
{
    FslSataState *s = container_of(dma, FslSataState, dma);

    fsl_sata_complete(s);
}

static const IDEDMAOps fsl_sata_dma_ops = {
    .start_dma = fsl_sata_start_dma,
    .pio_transfer = fsl_sata_pio_transfer,
    .prepare_buf = fsl_sata_prepare_buf,
    .rw_buf = fsl_sata_rw_buf,
    .restart_dma = fsl_sata_restart_dma,
    .cmd_done = fsl_sata_cmd_done,
};

/* An H2D register FIS without the C bit: a device control register update */
static void fsl_sata_control_fis(FslSataState *s, int slot,
                                 const uint8_t *cfis)
{
    if (cfis[15] & ATA_SRST) {
        s->srst_asserted = true;
    } else if (s->srst_asserted) {
        /*
         * Clearing SRST again makes the device reset and send a D2H register
         * FIS with its signature (SATA 3.5a 11.4).
         */
        s->srst_asserted = false;
        ide_bus_reset(&s->bus, IDE_RESET_SOFTWARE);
        fsl_sata_update_signature(s);
        fsl_sata_write_sfis(s);
    }
    fsl_sata_finish_slot(s, slot, false);
}

static void fsl_sata_exec(FslSataState *s, int slot)
{
    IDEState *ide = &s->bus.ifs[0];
    uint32_t hdr[CMDHDR_SIZE / 4];
    uint8_t cfis[FIS_SIZE];

    if (dma_memory_read(s->as, s->chba + slot * CMDHDR_SIZE, hdr, sizeof(hdr),
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "fsl-sata: can't read command header"
                      " at 0x%" PRIx32 "\n", s->chba + slot * CMDHDR_SIZE);
        fsl_sata_finish_slot(s, slot, true);
        return;
    }
    s->cur_cda = le32_to_cpu(hdr[0]) & ~3u;
    s->cur_prde_count = CMDHDR_PRDE_COUNT(le32_to_cpu(hdr[1]));
    s->cur_desc_info = le32_to_cpu(hdr[3]);

    if (dma_memory_read(s->as, s->cur_cda + CMDDESC_CFIS, cfis, sizeof(cfis),
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "fsl-sata: can't read command"
                      " descriptor at 0x%" PRIx32 "\n", s->cur_cda);
        fsl_sata_finish_slot(s, slot, true);
        return;
    }
    trace_fsl_sata_exec(s, slot, s->cur_cda, s->cur_desc_info,
                        s->cur_prde_count, cfis[0], cfis[1], cfis[2]);

    if (s->cur_desc_info & (DESC_BIST | DESC_BIST_VENDOR)) {
        qemu_log_mask(LOG_UNIMP, "fsl-sata: BIST not implemented\n");
        fsl_sata_finish_slot(s, slot, false);
        return;
    }
    if (cfis[0] != SATA_FIS_REG_H2D) {
        qemu_log_mask(LOG_GUEST_ERROR, "fsl-sata: unsupported FIS type"
                      " 0x%02x\n", cfis[0]);
        fsl_sata_finish_slot(s, slot, true);
        return;
    }

    /*
     * The PM port field (cfis[1] bits 3..0) is ignored: there is no port
     * multiplier, and a device that isn't one ignores the field, which is
     * what lets drivers probe for a port multiplier with SRST to port 15.
     */
    if (!(cfis[1] & SATA_FIS_H2D_COMMAND)) {
        fsl_sata_control_fis(s, slot, cfis);
        return;
    }

    if (!fsl_sata_has_device(s)) {
        fsl_sata_finish_slot(s, slot, true);
        return;
    }

    ide->feature = cfis[3];
    ide->sector = cfis[4];
    ide->lcyl = cfis[5];
    ide->hcyl = cfis[6];
    /* SATA has no master/slave: always address the one device */
    ide->select = cfis[7] & ~ATA_DEV_SELECT;
    ide->hob_sector = cfis[8];
    ide->hob_lcyl = cfis[9];
    ide->hob_hcyl = cfis[10];
    ide->hob_feature = cfis[11];
    ide->nsector = (cfis[13] << 8) | cfis[12];

    if (s->cur_desc_info & DESC_ATAPI) {
        dma_memory_read(s->as, s->cur_cda + CMDDESC_ACMD, ide->io_buffer,
                        ACMD_SIZE, MEMTXATTRS_UNSPECIFIED);
    }

    ide->error = 0;
    s->busy_slot = slot;
    s->cur_ata_cmd = cfis[2];
    s->done_first_drq = false;
    ide_bus_exec_cmd(&s->bus, cfis[2]);

    /*
     * A command the IDE core refuses outright (an unknown or, for this kind
     * of device, invalid opcode) is aborted without going through cmd_done;
     * complete it here.
     */
    if (s->busy_slot == slot && !(ide->status & (BUSY_STAT | DRQ_STAT))) {
        fsl_sata_complete(s);
    }
}

/* Start the next queued command when the device is free */
static void fsl_sata_cmd_bh(void *opaque)
{
    FslSataState *s = opaque;
    uint32_t pending = s->cq & ~s->ca;
    int slot;

    if (!(s->hstatus & HSTATUS_ONLINE) || s->busy_slot >= 0 || s->ce ||
        !pending) {
        return;
    }

    slot = ctz32(pending);
    s->cq &= ~(1u << slot);
    s->ca |= 1u << slot;
    fsl_sata_exec(s, slot);
}

/* Abort whatever the device is doing and forget queued commands */
static void fsl_sata_abort_all(FslSataState *s)
{
    if (s->busy_slot >= 0) {
        s->busy_slot = -1;
        ide_bus_reset(&s->bus, IDE_RESET_HARDWARE);
    }
    s->cq = 0;
    s->ca = 0;
    s->srst_asserted = false;
}

static void fsl_sata_go_offline(FslSataState *s)
{
    trace_fsl_sata_link(s, false);
    fsl_sata_abort_all(s);
    s->hstatus &= ~HSTATUS_ONLINE;
    s->sstatus = 0;
}

/*
 * Bringing the controller online sends a COMRESET; a device answers with
 * COMINIT, the link comes up and the device sends its signature FIS.  All of
 * that is instantaneous here.
 */
static void fsl_sata_go_online(FslSataState *s)
{
    unsigned spd_limit = (s->scontrol >> SCONTROL_SPD_SHIFT) & 0xf;

    trace_fsl_sata_link(s, true);
    s->hstatus |= HSTATUS_ONLINE;
    if (!fsl_sata_has_device(s)) {
        s->sstatus = 0;
        return;
    }

    ide_bus_reset(&s->bus, IDE_RESET_HARDWARE);
    s->srst_asserted = false;
    /* Gen1 if software limited the speed to it, otherwise Gen2 */
    s->sstatus = SSTATUS_IPM_ACTIVE |
                 ((spd_limit == 1 ? 1 : 2) << SSTATUS_SPD_SHIFT) |
                 SSTATUS_DET_PHY_ONLINE;
    s->hstatus |= HSTATUS_INT_PHYRDY_CHG;
    fsl_sata_update_signature(s);
}

static void fsl_sata_write_hcontrol(FslSataState *s, uint32_t val)
{
    uint32_t old = s->hcontrol;

    s->hcontrol = val & ~HCONTROL_CLEAR_ERROR;

    if (val & HCONTROL_CLEAR_ERROR) {
        s->ce = 0;
        s->de = 0;
        s->hstatus &= ~HSTATUS_FATAL_MASK;
    }

    if (!(val & HCONTROL_ONLINE_PHY_RST) || (val & HCONTROL_FORCE_OFFLINE)) {
        if (s->hstatus & HSTATUS_ONLINE) {
            fsl_sata_go_offline(s);
        }
    } else if (!(old & HCONTROL_ONLINE_PHY_RST) ||
               !(s->hstatus & HSTATUS_ONLINE)) {
        fsl_sata_go_online(s);
    }

    fsl_sata_update_irq(s);
    fsl_sata_kick(s);
}

static uint64_t fsl_sata_read(void *opaque, hwaddr addr, unsigned size)
{
    FslSataState *s = opaque;
    uint32_t val;

    switch (addr) {
    case REG_CQ:
        val = s->cq;
        break;
    case REG_CA:
        val = s->ca;
        break;
    case REG_CC:
        val = s->cc;
        break;
    case REG_CE:
        val = s->ce;
        break;
    case REG_DE:
        val = s->de;
        break;
    case REG_CHBA:
        val = s->chba;
        break;
    case REG_HSTATUS:
        val = s->hstatus;
        break;
    case REG_HCONTROL:
        val = s->hcontrol;
        break;
    case REG_CQPMP:
        val = s->cqpmp;
        break;
    case REG_SIGNATURE:
        val = s->signature;
        break;
    case REG_ICC:
        val = s->icc;
        break;
    case REG_SSTATUS:
        val = s->sstatus;
        break;
    case REG_SERROR:
        val = s->serror;
        break;
    case REG_SCONTROL:
        val = s->scontrol;
        break;
    case REG_SNOTIFY:
        val = s->snotify;
        break;
    case REG_CSR_BASE ... REG_COMMANDSTAT:
        val = s->csr[(addr - REG_CSR_BASE) / 4];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "fsl-sata: read of unimplemented register"
                      " 0x%03" HWADDR_PRIx "\n", addr);
        val = 0;
        break;
    }

    trace_fsl_sata_read(s, addr, val);
    return val;
}

static void fsl_sata_write(void *opaque, hwaddr addr, uint64_t val64,
                           unsigned size)
{
    FslSataState *s = opaque;
    uint32_t val = val64;

    trace_fsl_sata_write(s, addr, val);

    switch (addr) {
    case REG_CQ:
        s->cq |= val & FSL_SATA_SLOT_MASK & ~s->ca;
        fsl_sata_kick(s);
        break;
    case REG_CA:
    case REG_SIGNATURE:
    case REG_SSTATUS:
        break;
    case REG_CC:
        s->cc &= ~val;
        /* Clearing the completions also retires their interrupt */
        if (!s->cc) {
            s->hstatus &= ~HSTATUS_INT_CMD_DONE;
        }
        fsl_sata_update_irq(s);
        break;
    case REG_CE:
        s->ce &= ~val;
        fsl_sata_kick(s);
        break;
    case REG_DE:
        s->de &= ~val;
        break;
    case REG_CHBA:
        s->chba = val & ~3u;
        break;
    case REG_HSTATUS:
        s->hstatus &= ~(val & (HSTATUS_INT_MASK | HSTATUS_FATAL_MASK));
        fsl_sata_update_irq(s);
        break;
    case REG_HCONTROL:
        fsl_sata_write_hcontrol(s, val);
        break;
    case REG_CQPMP:
        s->cqpmp = val & 0xf;
        break;
    case REG_ICC:
        s->icc = val;
        break;
    case REG_SERROR:
        s->serror &= ~val;
        break;
    case REG_SCONTROL:
        s->scontrol = val;
        break;
    case REG_SNOTIFY:
        s->snotify &= ~val;
        break;
    case REG_CSR_BASE ... REG_COMMANDSTAT:
        s->csr[(addr - REG_CSR_BASE) / 4] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "fsl-sata: write of unimplemented register"
                      " 0x%03" HWADDR_PRIx " = 0x%08" PRIx32 "\n", addr, val);
        break;
    }
}

static const MemoryRegionOps fsl_sata_ops = {
    .read = fsl_sata_read,
    .write = fsl_sata_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fsl_sata_reset(DeviceState *dev)
{
    FslSataState *s = FSL_SATA(dev);

    fsl_sata_abort_all(s);
    ide_bus_reset(&s->bus, IDE_RESET_HARDWARE);
    s->cc = 0;
    s->ce = 0;
    s->de = 0;
    s->chba = 0;
    s->hstatus = 0;
    s->hcontrol = 0;
    s->cqpmp = 0;
    s->signature = 0;
    s->icc = 0;
    s->sstatus = 0;
    s->serror = 0;
    s->scontrol = 0;
    s->snotify = 0;
    memset(s->csr, 0, sizeof(s->csr));
    fsl_sata_update_irq(s);
}

static void fsl_sata_realize(DeviceState *dev, Error **errp)
{
    FslSataState *s = FSL_SATA(dev);

    s->as = &address_space_memory;
    s->busy_slot = -1;
    s->cmd_bh = qemu_bh_new_guarded(fsl_sata_cmd_bh, s,
                                    &s->mem_reentrancy_guard);

    /*
     * The controller learns about the device's progress through the IDEDMA
     * callbacks, so the IDE interrupt line isn't connected to anything.
     */
    ide_bus_init_output_irq(&s->bus, NULL);
    s->dma.ops = &fsl_sata_dma_ops;
    s->bus.dma = &s->dma;
    ide_bus_register_restart_cb(&s->bus);
}

static void fsl_sata_init(Object *obj)
{
    FslSataState *s = FSL_SATA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &fsl_sata_ops, s, "fsl-sata",
                          FSL_SATA_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    ide_bus_init(&s->bus, sizeof(s->bus), DEVICE(obj), 0, 1);
}

static int fsl_sata_post_load(void *opaque, int version_id)
{
    FslSataState *s = opaque;

    /* Resume processing the queue, in case the bottom half was pending */
    fsl_sata_kick(s);
    return 0;
}

static const VMStateDescription vmstate_fsl_sata = {
    .name = "fsl-sata",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = fsl_sata_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cq, FslSataState),
        VMSTATE_UINT32(ca, FslSataState),
        VMSTATE_UINT32(cc, FslSataState),
        VMSTATE_UINT32(ce, FslSataState),
        VMSTATE_UINT32(de, FslSataState),
        VMSTATE_UINT32(chba, FslSataState),
        VMSTATE_UINT32(hstatus, FslSataState),
        VMSTATE_UINT32(hcontrol, FslSataState),
        VMSTATE_UINT32(cqpmp, FslSataState),
        VMSTATE_UINT32(signature, FslSataState),
        VMSTATE_UINT32(icc, FslSataState),
        VMSTATE_UINT32(sstatus, FslSataState),
        VMSTATE_UINT32(serror, FslSataState),
        VMSTATE_UINT32(scontrol, FslSataState),
        VMSTATE_UINT32(snotify, FslSataState),
        VMSTATE_UINT32_ARRAY(csr, FslSataState, FSL_SATA_NUM_CSR),
        VMSTATE_INT32(busy_slot, FslSataState),
        VMSTATE_UINT32(cur_cda, FslSataState),
        VMSTATE_UINT32(cur_desc_info, FslSataState),
        VMSTATE_UINT8(cur_prde_count, FslSataState),
        VMSTATE_UINT8(cur_ata_cmd, FslSataState),
        VMSTATE_BOOL(done_first_drq, FslSataState),
        VMSTATE_BOOL(srst_asserted, FslSataState),
        VMSTATE_IDE_BUS(bus, FslSataState),
        VMSTATE_IDE_DRIVES(bus.ifs, FslSataState),
        VMSTATE_END_OF_LIST()
    }
};

static void fsl_sata_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fsl_sata_realize;
    dc->vmsd = &vmstate_fsl_sata;
    device_class_set_legacy_reset(dc, fsl_sata_reset);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo fsl_sata_types[] = {
    {
        .name          = TYPE_FSL_SATA,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslSataState),
        .instance_init = fsl_sata_init,
        .class_init    = fsl_sata_class_init,
    },
};

DEFINE_TYPES(fsl_sata_types)

void fsl_sata_init_drive(DeviceState *dev, DriveInfo *hd)
{
    FslSataState *s = FSL_SATA(dev);

    if (hd) {
        ide_bus_create_drive(&s->bus, 0, hd);
    }
}
