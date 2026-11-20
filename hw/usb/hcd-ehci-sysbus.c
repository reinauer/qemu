/*
 * QEMU USB EHCI Emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "hw/usb/hcd-ehci.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

static const VMStateDescription vmstate_ehci_sysbus = {
    .name        = "ehci-sysbus",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ehci, EHCISysBusState, 2, vmstate_ehci, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property ehci_sysbus_properties[] = {
    DEFINE_EHCI_COMMON_PROPERTIES(EHCISysBusState),
    DEFINE_PROP_BOOL("companion-enable", EHCISysBusState, ehci.companion_enable,
                     false),
};

static void usb_ehci_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(dev);
    EHCIState *s = &i->ehci;

    usb_ehci_realize(s, dev, errp);
    sysbus_init_irq(d, &s->irq);
}

static void usb_ehci_sysbus_reset(DeviceState *dev)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(d);
    EHCIState *s = &i->ehci;

    ehci_reset(s);
}

static void ehci_sysbus_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_GET_CLASS(obj);
    EHCIState *s = &i->ehci;

    s->capsbase = sec->capsbase;
    s->opregbase = sec->opregbase;
    s->portscbase = sec->portscbase;
    s->portnr = sec->portnr;
    s->port_power_control = sec->port_power_control;
    s->embedded_tt = sec->embedded_tt;
    s->as = &address_space_memory;

    usb_ehci_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void ehci_sysbus_finalize(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    EHCIState *s = &i->ehci;

    usb_ehci_finalize(s);
}

static void ehci_sysbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(klass);

    sec->portscbase = 0x44;
    sec->portnr = EHCI_PORTS;

    dc->realize = usb_ehci_sysbus_realize;
    dc->vmsd = &vmstate_ehci_sysbus;
    device_class_set_props(dc, ehci_sysbus_properties);
    device_class_set_legacy_reset(dc, usb_ehci_sysbus_reset);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_platform_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x20;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_exynos4210_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_aw_h3_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_npcm7xx_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x44;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_tegra2_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_ppc4xx_init(Object *o)
{
    EHCISysBusState *s = SYS_BUS_EHCI(o);

    s->ehci.companion_enable = true;
}

static void ehci_ppc4xx_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

/*
 * Faraday FUSBH200 USB 2.0 EHCI
 */

/**
 * FUSBH200EHCIRegs:
 * @FUSBH200_REG_EOF_ASTR: EOF/Async. Sleep Timer Register
 * @FUSBH200_REG_BMCSR: Bus Monitor Control/Status Register
 */
enum FUSBH200EHCIRegs {
    FUSBH200_REG_EOF_ASTR = 0x34,
    FUSBH200_REG_BMCSR    = 0x40,
};

static uint64_t fusbh200_ehci_read(void *opaque, hwaddr addr, unsigned size)
{
    EHCIState *s = opaque;
    hwaddr off = s->opregbase + s->portscbase + 4 * s->portnr + addr;

    switch (off) {
    case FUSBH200_REG_EOF_ASTR:
        return 0x00000041;
    case FUSBH200_REG_BMCSR:
        /* High-Speed, VBUS valid, interrupt level-high active */
        return (2 << 9) | (1 << 8) | (1 << 3);
    }

    return 0;
}

static void fusbh200_ehci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
}

static const MemoryRegionOps fusbh200_ehci_mmio_ops = {
    .read = fusbh200_ehci_read,
    .write = fusbh200_ehci_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fusbh200_ehci_init(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    FUSBH200EHCIState *f = FUSBH200_EHCI(obj);
    EHCIState *s = &i->ehci;

    memory_region_init_io(&f->mem_vendor, OBJECT(f), &fusbh200_ehci_mmio_ops, s,
                          "fusbh200", 0x4c);
    memory_region_add_subregion(&s->mem,
                                s->opregbase + s->portscbase + 4 * s->portnr,
                                &f->mem_vendor);
}

static void fusbh200_ehci_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x20;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

/*
 * Freescale (now NXP) dual-role USB controller of the MPC83xx, MPC85xx and
 * QorIQ P10xx/P20xx SoCs, in host mode.  It is a ChipIdea (TDI) core: the
 * EHCI capability and operational registers sit at 0x100 and 0x140, between
 * the core's own registers, and the root hub port has an embedded TT and
 * port power control.  Freescale's system interface registers follow at
 * 0x400; unlike the rest, they are big-endian.
 */

/* ChipIdea core registers outside of the EHCI blocks */
#define FSL_EHCI_DCCPARAMS      0x124
#define FSL_EHCI_DCCPARAMS_HC   (1 << 8)        /* host capable */
#define FSL_EHCI_USBMODE        0x1a8
#define FSL_EHCI_USBMODE_CM     (3 << 0)        /* controller mode */
#define FSL_EHCI_USBMODE_CM_HOST 3
#define FSL_EHCI_USBMODE_ES     (1 << 2)        /* big-endian buffers */
#define FSL_EHCI_USBMODE_MASK   0x1f

/* System interface registers, as offsets in their block at 0x400 */
#define FSL_EHCI_SYSIF_BASE     0x400
#define FSL_EHCI_SNOOP1         0x000
#define FSL_EHCI_SI_CTRL        0x010
#define FSL_EHCI_CONTROL        0x100
#define FSL_EHCI_CONTROL_PHY_CLK_VALID (1 << 17)

static uint64_t fsl_ehci_core_read(void *opaque, hwaddr addr, unsigned size)
{
    FSLEHCIState *f = opaque;
    uint32_t val = 0;

    switch (addr) {
    case FSL_EHCI_DCCPARAMS:
        /* The device side of the dual-role controller is not modelled */
        val = FSL_EHCI_DCCPARAMS_HC;
        break;
    case FSL_EHCI_USBMODE:
        val = f->usbmode;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      "\n", __func__, addr);
        break;
    }

    trace_usb_ehci_fsl_core_read(addr, val);
    return val;
}

static void fsl_ehci_core_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    FSLEHCIState *f = opaque;

    trace_usb_ehci_fsl_core_write(addr, val);

    switch (addr) {
    case FSL_EHCI_USBMODE:
        f->usbmode = val & FSL_EHCI_USBMODE_MASK;
        if ((val & FSL_EHCI_USBMODE_CM) &&
            (val & FSL_EHCI_USBMODE_CM) != FSL_EHCI_USBMODE_CM_HOST) {
            qemu_log_mask(LOG_UNIMP, "%s: device mode not supported\n",
                          __func__);
        }
        if (val & FSL_EHCI_USBMODE_ES) {
            qemu_log_mask(LOG_UNIMP, "%s: big-endian buffers not supported\n",
                          __func__);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps fsl_ehci_core_ops = {
    .read = fsl_ehci_core_read,
    .write = fsl_ehci_core_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t fsl_ehci_sysif_read(void *opaque, hwaddr addr, unsigned size)
{
    FSLEHCIState *f = opaque;
    uint32_t val = 0;

    switch (addr) {
    case FSL_EHCI_SNOOP1 ... FSL_EHCI_SI_CTRL:
        val = f->sysif[(addr - FSL_EHCI_SNOOP1) >> 2];
        break;
    case FSL_EHCI_CONTROL:
        /* The PHY is always up and running */
        val = f->control | FSL_EHCI_CONTROL_PHY_CLK_VALID;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      "\n", __func__, addr + FSL_EHCI_SYSIF_BASE);
        break;
    }

    trace_usb_ehci_fsl_sysif_read(addr + FSL_EHCI_SYSIF_BASE, val);
    return val;
}

static void fsl_ehci_sysif_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    FSLEHCIState *f = opaque;

    trace_usb_ehci_fsl_sysif_write(addr + FSL_EHCI_SYSIF_BASE, val);

    switch (addr) {
    case FSL_EHCI_SNOOP1 ... FSL_EHCI_SI_CTRL:
        /*
         * Snooping windows, bus priorities and the like: QEMU's DMA is
         * coherent anyway, so these only need to read back.
         */
        f->sysif[(addr - FSL_EHCI_SNOOP1) >> 2] = val;
        break;
    case FSL_EHCI_CONTROL:
        /* USB_EN, PHY selection etc. have no effect on the model */
        f->control = val & ~FSL_EHCI_CONTROL_PHY_CLK_VALID;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%03" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n", __func__,
                      addr + FSL_EHCI_SYSIF_BASE, val);
        break;
    }
}

static const MemoryRegionOps fsl_ehci_sysif_ops = {
    .read = fsl_ehci_sysif_read,
    .write = fsl_ehci_sysif_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsl_ehci_reset(DeviceState *dev)
{
    EHCISysBusState *i = SYS_BUS_EHCI(dev);
    FSLEHCIState *f = FSL_EHCI(dev);

    ehci_reset(&i->ehci);
    f->usbmode = 0;
    memset(f->sysif, 0, sizeof(f->sysif));
    f->control = 0;
}

static void fsl_ehci_init(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    FSLEHCIState *f = FSL_EHCI(obj);
    EHCIState *s = &i->ehci;

    /*
     * The core registers surround the EHCI ones, which take precedence
     * where they overlap.
     */
    memory_region_init_io(&f->mem_core, obj, &fsl_ehci_core_ops, f,
                          "fsl-ehci-core", FSL_EHCI_SYSIF_BASE);
    memory_region_add_subregion_overlap(&s->mem, 0, &f->mem_core, -1);
    memory_region_init_io(&f->mem_sysif, obj, &fsl_ehci_sysif_ops, f,
                          "fsl-ehci-sysif", 0x200);
    memory_region_add_subregion(&s->mem, FSL_EHCI_SYSIF_BASE, &f->mem_sysif);
}

static const VMStateDescription vmstate_fsl_ehci = {
    .name = "fsl-ehci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, FSLEHCIState, 1, vmstate_ehci_sysbus,
                       EHCISysBusState),
        VMSTATE_UINT32(usbmode, FSLEHCIState),
        VMSTATE_UINT32_ARRAY(sysif, FSLEHCIState, FSL_EHCI_SYSIF_REGS),
        VMSTATE_UINT32(control, FSLEHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void fsl_ehci_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
    sec->portscbase = 0x44;
    sec->portnr = 1;
    sec->port_power_control = true;
    sec->embedded_tt = true;
    dc->vmsd = &vmstate_fsl_ehci;
    device_class_set_legacy_reset(dc, fsl_ehci_reset);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_sysbus_types[] = {
    {
        .name          = TYPE_SYS_BUS_EHCI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(EHCISysBusState),
        .instance_init = ehci_sysbus_init,
        .instance_finalize = ehci_sysbus_finalize,
        .abstract      = true,
        .class_init    = ehci_sysbus_class_init,
        .class_size    = sizeof(SysBusEHCIClass),
    },
    {
        .name          = TYPE_PLATFORM_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_platform_class_init,
    },
    {
        .name          = TYPE_EXYNOS4210_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_exynos4210_class_init,
    },
    {
        .name          = TYPE_AW_H3_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_aw_h3_class_init,
    },
    {
        .name          = TYPE_NPCM7XX_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_npcm7xx_class_init,
    },
    {
        .name          = TYPE_TEGRA2_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_tegra2_class_init,
    },
    {
        .name          = TYPE_PPC4xx_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_ppc4xx_class_init,
        .instance_init = ehci_ppc4xx_init,
    },
    {
        .name          = TYPE_FUSBH200_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .instance_size = sizeof(FUSBH200EHCIState),
        .instance_init = fusbh200_ehci_init,
        .class_init    = fusbh200_ehci_class_init,
    },
    {
        .name          = TYPE_FSL_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .instance_size = sizeof(FSLEHCIState),
        .instance_init = fsl_ehci_init,
        .class_init    = fsl_ehci_class_init,
    },
};

DEFINE_TYPES(ehci_sysbus_types)
