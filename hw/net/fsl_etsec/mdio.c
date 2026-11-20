/*
 * Freescale eTSEC 2.0 MDIO controller ("fsl,etsec2-mdio")
 *
 * On SoCs with eTSEC 2.0 controllers, such as the P1022 and P1010, the
 * MII management registers moved out of the controllers into MDIO blocks
 * of their own, one per controller, at CCSR 0x24000, 0x25000 and so on.
 * Each block has the same MIIMCFG, MIIMCOM, MIIMADD, MIIMCON, MIIMSTAT and
 * MIIMIND registers as an eTSEC 1.x at offset 0x520, as Linux's
 * drivers/net/ethernet/freescale/fsl_pq_mdio.c describes them.
 *
 * The bus has the PHY of one eTSEC at a configurable address; reads of
 * other addresses find no PHY and return all ones.  Management cycles
 * complete at once, so MIIMIND never reads busy.  Not emulated: the MDIO
 * interrupt event, mask and mapping registers, and scan cycles.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/net/fsl_etsec_mdio.h"
#include "migration/vmstate.h"
#include "etsec.h"
#include "hw/net/trace.h"

#define MDIO_MMIO_SIZE      0x1000

#define MDIO_MIIMCFG        0x520
#define MDIO_MIIMCOM        0x524
#define MDIO_MIIMADD        0x528
#define MDIO_MIIMCON        0x52c
#define MDIO_MIIMSTAT       0x530
#define MDIO_MIIMIND        0x534

#define MIIMCFG_RESET       (1U << 31)
#define MIIMCFG_MASK        (MIIMCFG_RESET | 0x37)

#define MIIMCOM_READ        (1 << 0)
#define MIIMCOM_SCAN        (1 << 1)

#define MIIMADD_PHY_SHIFT   8
#define MIIMADD_MASK        0x1f1f

static uint16_t fsl_etsec_mdio_phy_read(FslEtsecMdioState *s)
{
    unsigned int phy = s->miimadd >> MIIMADD_PHY_SHIFT;
    unsigned int reg = s->miimadd & 0x1f;
    uint16_t value = 0xffff;

    if (s->etsec && phy == s->phy_addr) {
        value = etsec_phy_read(s->etsec, reg);
    }
    trace_fsl_etsec_mdio_phy_read(phy, reg, value);

    return value;
}

static void fsl_etsec_mdio_phy_write(FslEtsecMdioState *s, uint16_t value)
{
    unsigned int phy = s->miimadd >> MIIMADD_PHY_SHIFT;
    unsigned int reg = s->miimadd & 0x1f;

    trace_fsl_etsec_mdio_phy_write(phy, reg, value);
    if (s->etsec && phy == s->phy_addr) {
        etsec_phy_write(s->etsec, reg, value);
    }
}

static uint64_t fsl_etsec_mdio_read(void *opaque, hwaddr addr, unsigned size)
{
    FslEtsecMdioState *s = opaque;
    uint32_t value;

    switch (addr) {
    case MDIO_MIIMCFG:
        value = s->miimcfg;
        break;
    case MDIO_MIIMCOM:
        value = s->miimcom;
        break;
    case MDIO_MIIMADD:
        value = s->miimadd;
        break;
    case MDIO_MIIMCON:
        value = s->miimcon;
        break;
    case MDIO_MIIMSTAT:
        value = s->miimstat;
        break;
    case MDIO_MIIMIND:
        /* Cycles complete at once: never busy, the status always valid */
        value = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      "\n", __func__, addr);
        value = 0;
        break;
    }
    trace_fsl_etsec_mdio_read(addr, value);

    return value;
}

static void fsl_etsec_mdio_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
    FslEtsecMdioState *s = opaque;

    trace_fsl_etsec_mdio_write(addr, value);

    switch (addr) {
    case MDIO_MIIMCFG:
        /* Resetting the management interface completes at once */
        s->miimcfg = value & MIIMCFG_MASK & ~MIIMCFG_RESET;
        break;
    case MDIO_MIIMCOM:
        /* A read cycle starts when the read command bit goes from 0 to 1 */
        if (!(s->miimcom & MIIMCOM_READ) && (value & MIIMCOM_READ)) {
            s->miimstat = fsl_etsec_mdio_phy_read(s);
        }
        if (value & MIIMCOM_SCAN) {
            qemu_log_mask(LOG_UNIMP, "%s: scan cycles are not emulated\n",
                          __func__);
        }
        s->miimcom = value & (MIIMCOM_READ | MIIMCOM_SCAN);
        break;
    case MDIO_MIIMADD:
        s->miimadd = value & MIIMADD_MASK;
        break;
    case MDIO_MIIMCON:
        /* Writing the control register starts a write cycle */
        s->miimcon = value & 0xffff;
        fsl_etsec_mdio_phy_write(s, s->miimcon);
        break;
    case MDIO_MIIMSTAT:
    case MDIO_MIIMIND:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read-only register 0x%03"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps fsl_etsec_mdio_ops = {
    .read = fsl_etsec_mdio_read,
    .write = fsl_etsec_mdio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fsl_etsec_mdio_reset(DeviceState *dev)
{
    FslEtsecMdioState *s = FSL_ETSEC_MDIO(dev);

    s->miimcfg = 0x7;
    s->miimcom = 0;
    s->miimadd = 0;
    s->miimcon = 0;
    s->miimstat = 0;
}

static void fsl_etsec_mdio_init(Object *obj)
{
    FslEtsecMdioState *s = FSL_ETSEC_MDIO(obj);

    memory_region_init_io(&s->iomem, obj, &fsl_etsec_mdio_ops, s,
                          TYPE_FSL_ETSEC_MDIO, MDIO_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_fsl_etsec_mdio = {
    .name = TYPE_FSL_ETSEC_MDIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(miimcfg, FslEtsecMdioState),
        VMSTATE_UINT32(miimcom, FslEtsecMdioState),
        VMSTATE_UINT32(miimadd, FslEtsecMdioState),
        VMSTATE_UINT32(miimcon, FslEtsecMdioState),
        VMSTATE_UINT32(miimstat, FslEtsecMdioState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property fsl_etsec_mdio_properties[] = {
    DEFINE_PROP_LINK("etsec", FslEtsecMdioState, etsec, TYPE_ETSEC_COMMON,
                     eTSEC *),
    DEFINE_PROP_UINT8("phy-addr", FslEtsecMdioState, phy_addr, 0),
};

static void fsl_etsec_mdio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Freescale eTSEC 2.0 MDIO controller";
    dc->vmsd = &vmstate_fsl_etsec_mdio;
    device_class_set_legacy_reset(dc, fsl_etsec_mdio_reset);
    device_class_set_props(dc, fsl_etsec_mdio_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo fsl_etsec_mdio_types[] = {
    {
        .name          = TYPE_FSL_ETSEC_MDIO,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslEtsecMdioState),
        .instance_init = fsl_etsec_mdio_init,
        .class_init    = fsl_etsec_mdio_class_init,
    },
};

DEFINE_TYPES(fsl_etsec_mdio_types)
