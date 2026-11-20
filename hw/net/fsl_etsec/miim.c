/*
 * QEMU Freescale eTSEC Emulator
 *
 * Copyright (c) 2011-2013 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/net/mii.h"
#include "etsec.h"
#include "registers.h"
#include "hw/net/trace.h"

uint16_t etsec_phy_read(eTSEC *etsec, unsigned int reg)
{
    bool link = etsec->phy_status & MII_BMSR_LINK_ST;
    uint16_t value;

    switch (reg) {
    case MII_BMCR:
        value = etsec->phy_control;
        break;
    case MII_BMSR:
        value = etsec->phy_status;
        break;
    case MII_ANAR:
        value = MII_ANAR_PAUSE | MII_ANAR_TXFD | MII_ANAR_TX |
                MII_ANAR_10FD | MII_ANAR_10 | MII_ANAR_CSMACD;
        break;
    case MII_ANLPAR:
        /* A link partner that can do everything we can */
        value = link ? MII_ANLPAR_ACK | MII_ANLPAR_PAUSE | MII_ANLPAR_TXFD |
                       MII_ANLPAR_TX | MII_ANLPAR_10FD | MII_ANLPAR_10 |
                       MII_ANLPAR_CSMACD : 0;
        break;
    case MII_CTRL1000:
        value = MII_CTRL1000_FULL | MII_CTRL1000_HALF;
        break;
    case MII_STAT1000:
        value = MII_STAT1000_LOK | MII_STAT1000_ROK;
        if (link) {
            value |= MII_STAT1000_FULL | MII_STAT1000_HALF;
        }
        break;
    case MII_EXTSTAT:
        value = MII_EXTSTAT_1000T_FD | MII_EXTSTAT_1000T_HD;
        break;
    default:
        value = 0x0;
        break;
    };

    trace_etsec_phy_read(reg, value);

    return value;
}

void etsec_phy_write(eTSEC *etsec, unsigned int reg, uint16_t value)
{
    trace_etsec_phy_write(reg, value);

    switch (reg) {
    case MII_BMCR:
        etsec->phy_control = value & ~(MII_BMCR_RESET | MII_BMCR_FD);
        break;
    default:
        break;
    };
}

static void miim_read_cycle(eTSEC *etsec)
{
    uint8_t addr = etsec->regs[MIIMADD].value & 0x1F;

    etsec->regs[MIIMSTAT].value = etsec_phy_read(etsec, addr);
}

static void miim_write_cycle(eTSEC *etsec)
{
    uint8_t addr = etsec->regs[MIIMADD].value & 0x1F;

    etsec_phy_write(etsec, addr, etsec->regs[MIIMCON].value & 0xffff);
}

void etsec_write_miim(eTSEC          *etsec,
                      eTSEC_Register *reg,
                      uint32_t        reg_index,
                      uint32_t        value)
{

    switch (reg_index) {

    case MIIMCOM:
        /* Read and scan cycle */

        if ((!(reg->value & MIIMCOM_READ)) && (value & MIIMCOM_READ)) {
            /* Read */
            miim_read_cycle(etsec);
        }
        reg->value = value;
        break;

    case MIIMCON:
        reg->value = value & 0xffff;
        miim_write_cycle(etsec);
        break;

    default:
        /* Default handling */
        switch (reg->access) {

        case ACC_RW:
        case ACC_WO:
            reg->value = value;
            break;

        case ACC_W1C:
            reg->value &= ~value;
            break;

        case ACC_RO:
        default:
            /* Read Only or Unknown register */
            break;
        }
    }

}

void etsec_miim_link_status(eTSEC *etsec, NetClientState *nc)
{
    /* Set link status */
    if (nc->link_down) {
        etsec->phy_status &= ~MII_BMSR_LINK_ST;
    } else {
        etsec->phy_status |= MII_BMSR_LINK_ST;
    }
}
