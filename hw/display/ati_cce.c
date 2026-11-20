/*
 * QEMU ATI SVGA emulation
 * Command processor (CCE/CP) in PIO mode
 *
 * In PIO mode software writes PM4 packets into the command queue aperture
 * in register space rather than into a ring buffer in memory.  The packets
 * are executed as they come in: type 0 packets write registers, type 2
 * packets are fillers, and type 3 packets are commands, of which the 2D
 * ones are carried out through the drawing engine registers.
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "qemu/log.h"

#define PM4_TYPE(h)         extract32(h, 30, 2)
#define PM4_COUNT(h)        extract32(h, 16, 14)
#define PM4_TYPE0_REG(h)    (extract32(h, 0, 13) << 2)
#define PM4_TYPE0_ONE_REG   BIT(15)
#define PM4_TYPE3_OP(h)     extract32(h, 8, 8)

#define PM4_TYPE3_NOP           0x10
#define PM4_TYPE3_HOSTDATA_BLT  0x14

/*
 * HOSTDATA_BLT: DP_SRC_FRGD_CLR, DP_SRC_BKGD_CLR, DST_Y_X, DST_HEIGHT_WIDTH
 * and the number of data dwords to follow, with the other settings as they
 * are in the registers
 */
#define HOSTDATA_BLT_HDR_DWORDS 5

static void ati_cce_reg_write(ATIVGAState *s, hwaddr reg, uint32_t data)
{
    if (reg >= CP_CSQ_APER_PRIMARY && reg <= CP_CSQ_APER_INDIRECT_END) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ati: CCE packet writes the command queue at 0x%"
                      HWADDR_PRIx "\n", reg);
        return;
    }
    ati_mm_write(s, reg, data, 4);
}

static void ati_cce_type3(ATIVGAState *s, uint32_t data)
{
    ATICCEState *c = &s->cce;

    switch (PM4_TYPE3_OP(c->header)) {
    case PM4_TYPE3_NOP:
        break;
    case PM4_TYPE3_HOSTDATA_BLT:
        if (c->idx < HOSTDATA_BLT_HDR_DWORDS) {
            c->args[c->idx] = data;
            if (c->idx == HOSTDATA_BLT_HDR_DWORDS - 1) {
                ati_cce_reg_write(s, DP_SRC_FRGD_CLR, c->args[0]);
                ati_cce_reg_write(s, DP_SRC_BKGD_CLR, c->args[1]);
                ati_cce_reg_write(s, DST_Y_X, c->args[2]);
                /* Starts the blit, which takes host data */
                ati_cce_reg_write(s, DST_HEIGHT_WIDTH, c->args[3]);
            }
        } else {
            ati_cce_reg_write(s, c->left ? HOST_DATA0 : HOST_DATA_LAST, data);
        }
        break;
    default:
        if (c->idx == 0) {
            qemu_log_mask(LOG_UNIMP, "ati: CCE type 3 packet 0x%02x\n",
                          PM4_TYPE3_OP(c->header));
        }
        break;
    }
}

void ati_cce_write(ATIVGAState *s, uint32_t data)
{
    ATICCEState *c = &s->cce;

    if (!c->left) {
        /* A new packet */
        c->header = data;
        c->idx = 0;
        switch (PM4_TYPE(data)) {
        case 0:
        case 3:
            c->left = PM4_COUNT(data) + 1;
            break;
        case 2:
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ati: CCE type 1 packet\n");
            c->left = 2;
            break;
        }
        return;
    }

    c->left--;
    switch (PM4_TYPE(c->header)) {
    case 0:
        ati_cce_reg_write(s, PM4_TYPE0_REG(c->header) +
                          (c->header & PM4_TYPE0_ONE_REG ? 0 : c->idx * 4),
                          data);
        break;
    case 3:
        ati_cce_type3(s, data);
        break;
    }
    c->idx++;
}
