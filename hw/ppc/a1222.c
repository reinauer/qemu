/*
 * AmigaOne A1222 (Tabor), a Freescale P1022 (e500v2) board
 *
 * For now the generic e500 machine with the P1022's address map as U-Boot
 * leaves it on the A1222: CCSR at 0xffe00000, where the AmigaOS kernel
 * expects its DUART, MPIC and L2 controller.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "e500.h"
#include "hw/core/or-irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/audio/fsl-ssi.h"
#include "hw/audio/wm8776.h"
#include "hw/display/fsl-diu.h"
#include "hw/dma/fsl-dma.h"
#include "hw/display/i2c-ddc.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/i2c/i2c.h"
#include "hw/ide/fsl-sata.h"
#include "hw/net/fsl_etsec/etsec.h"
#include "hw/net/fsl_etsec_mdio.h"
#include "hw/rtc/mcp7940.h"
#include "hw/usb/hcd-ehci.h"
#include "net/net.h"
#include "system/blockdev.h"
#include "system/system.h"
#include "system/device_tree.h"
#include "system/address-spaces.h"
#include "hw/ppc/openpic.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "target/ppc/cpu.h"

#define A1222_CCSR_BASE     0xFFE00000ULL

/*
 * The P1022's two SATA controllers, with their CCSR offsets and MPIC
 * interrupts in device tree numbering (internal interrupt n is 16 + n)
 */
static const struct {
    hwaddr offset;
    int irq;
} a1222_sata[] = {
    { 0x18000, 74 },
    { 0x19000, 41 },
};

/*
 * The P1022's two USB controllers, in the order of their AmigaOS unit
 * numbers, so that usb-bus.0 is unit 0 and usb-bus.1 unit 1
 */
static const struct {
    hwaddr offset;
    int irq;
} a1222_usb[] = {
    { 0x22000, 28 },
    { 0x23000, 46 },
};

/* The display interface unit */
#define A1222_DIU_OFFSET    0x10000
#define A1222_DIU_IRQ       64

/*
 * The second I2C controller, with the DVI transmitter; it shares its
 * interrupt with the first, which the e500 machine sets up
 */
#define A1222_I2C2_OFFSET   0x3100
#define A1222_I2C_IRQ       43

/* The MCP7940 battery backed clock on the second I2C controller */
#define A1222_RTC_ADDR      0x6f

/*
 * The GPIO bank at 0xf200, whose pins 20 and 21 (in the bank's big-endian
 * numbering) are the SDA and SCL lines of the DVI port's DDC
 */
#define A1222_GPIO3_OFFSET  0xF200
#define A1222_DDC_SDA       20
#define A1222_DDC_SCL       21

/*
 * The two DMA controllers' channels, and their first channel's interrupt;
 * DMA1 is "dma" and DMA2 "dma2"
 */
static const struct {
    const char *name;
    hwaddr offset;
    int irq;
} a1222_dma[] = {
    { "dma", 0x21100, 20 },
    { "dma2", 0x0c100, 76 },
};

/*
 * The SSI, whose transmit and receive DMA requests go to the first DMA
 * controller's channels 0 and 1, as the AmigaOS driver sets up the
 * P1022's DMA request multiplexing (and fsl,playback-dma says in Linux),
 * and its WM8776 codec on the second I2C bus, with a 12.288 MHz master
 * clock
 */
#define A1222_SSI_OFFSET    0x15000
#define A1222_SSI_IRQ       75
#define A1222_SSI_DMA_TX    0
#define A1222_SSI_DMA_RX    1
#define A1222_CODEC_ADDR    0x1a
#define A1222_CODEC_MCLK    12288000

/*
 * The P1022's two eTSEC 2.0 Ethernet controllers, with the interrupts of
 * their first queue group (transmit, receive, error), and the MDIO block
 * and PHY address that AmigaOS's A1222eth.device uses with each, as its
 * unit 0 and 1
 */
static const struct {
    hwaddr offset;
    int irq[3];
    hwaddr mdio_offset;
    uint8_t phy_addr;
} a1222_etsec[] = {
    { 0xb0000, { 29, 30, 34 }, 0x24000, 3 },
    { 0xb1000, { 35, 36, 40 }, 0x25000, 7 },
};

static void a1222_fixup_devtree(void *fdt)
{
    const char model[] = "AmigaOne A1222";
    const char compatible[] = "fsl,P1022\0fsl,P1022DS";
    const char usb_compatible[] = "fsl-usb2-dr-v1.6\0fsl-usb2-dr";
    g_autofree char *soc = g_strdup_printf("/soc@%llx", A1222_CCSR_BASE);
    /* The MPIC node as ppce500_load_device_tree() names it */
    g_autofree char *mpic = g_strdup_printf("%s/pic@40000", soc);
    int i;

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));

    /* Backwards, as each new node goes first, to list them in order */
    for (i = ARRAY_SIZE(a1222_etsec) - 1; i >= 0; i--) {
        g_autofree char *mdio = g_strdup_printf("%s/mdio@%" HWADDR_PRIx, soc,
                                                a1222_etsec[i].mdio_offset);
        g_autofree char *phy = g_strdup_printf("%s/ethernet-phy@%x", mdio,
                                               a1222_etsec[i].phy_addr);
        g_autofree char *node = g_strdup_printf("%s/ethernet@%" HWADDR_PRIx,
                                                soc, a1222_etsec[i].offset);
        g_autofree char *group = g_strdup_printf("%s/queue-group@%" HWADDR_PRIx,
                                                 node, a1222_etsec[i].offset);
        g_autofree char *name = g_strdup_printf("etsec%d", i);
        Object *obj = object_resolve_path_component(OBJECT(qdev_get_machine()),
                                                    name);
        eTSEC *etsec;

        if (!obj) {
            continue;
        }
        etsec = ETSEC_COMMON(obj);

        qemu_fdt_add_subnode(fdt, mdio);
        qemu_fdt_setprop_string(fdt, mdio, "compatible", "fsl,etsec2-mdio");
        qemu_fdt_setprop_cells(fdt, mdio, "reg", a1222_etsec[i].mdio_offset,
                               0x1000, a1222_etsec[i].offset + 0x30, 4);
        qemu_fdt_setprop_cell(fdt, mdio, "#address-cells", 1);
        qemu_fdt_setprop_cell(fdt, mdio, "#size-cells", 0);
        qemu_fdt_add_subnode(fdt, phy);
        qemu_fdt_setprop_cell(fdt, phy, "reg", a1222_etsec[i].phy_addr);
        qemu_fdt_setprop_string(fdt, phy, "device_type", "ethernet-phy");
        qemu_fdt_setprop_cell(fdt, phy, "phandle",
                              qemu_fdt_alloc_phandle(fdt));

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string(fdt, node, "device_type", "network");
        qemu_fdt_setprop_string(fdt, node, "model", "eTSEC");
        qemu_fdt_setprop_string(fdt, node, "compatible", "fsl,etsec2");
        qemu_fdt_setprop_cell(fdt, node, "cell-index", i);
        qemu_fdt_setprop_cell(fdt, node, "fsl,num_rx_queues", 1);
        qemu_fdt_setprop_cell(fdt, node, "fsl,num_tx_queues", 1);
        qemu_fdt_setprop(fdt, node, "local-mac-address",
                         etsec->conf.macaddr.a, sizeof(etsec->conf.macaddr.a));
        qemu_fdt_setprop_phandle(fdt, node, "phy-handle", phy);
        qemu_fdt_setprop_string(fdt, node, "phy-connection-type", "rgmii-id");
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
        qemu_fdt_setprop_cell(fdt, node, "#address-cells", 1);
        qemu_fdt_setprop_cell(fdt, node, "#size-cells", 1);
        qemu_fdt_setprop(fdt, node, "ranges", NULL, 0);
        qemu_fdt_add_subnode(fdt, group);
        qemu_fdt_setprop_cells(fdt, group, "reg", a1222_etsec[i].offset,
                               0x1000);
        qemu_fdt_setprop_cells(fdt, group, "interrupts",
                               a1222_etsec[i].irq[0], 2,
                               a1222_etsec[i].irq[1], 2,
                               a1222_etsec[i].irq[2], 2);
    }

    for (i = ARRAY_SIZE(a1222_sata) - 1; i >= 0; i--) {
        g_autofree char *node = g_strdup_printf("%s/sata@%" HWADDR_PRIx, soc,
                                                a1222_sata[i].offset);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string(fdt, node, "compatible", "fsl,pq-sata-v2");
        qemu_fdt_setprop_cells(fdt, node, "reg", a1222_sata[i].offset, 0x1000);
        qemu_fdt_setprop_cell(fdt, node, "cell-index", i + 1);
        qemu_fdt_setprop_cells(fdt, node, "interrupts", a1222_sata[i].irq, 2);
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
    }

    for (i = ARRAY_SIZE(a1222_usb) - 1; i >= 0; i--) {
        g_autofree char *node = g_strdup_printf("%s/usb@%" HWADDR_PRIx, soc,
                                                a1222_usb[i].offset);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop(fdt, node, "compatible", usb_compatible,
                         sizeof(usb_compatible));
        qemu_fdt_setprop_cells(fdt, node, "reg", a1222_usb[i].offset, 0x1000);
        qemu_fdt_setprop_cells(fdt, node, "interrupts", a1222_usb[i].irq, 2);
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
        qemu_fdt_setprop_cell(fdt, node, "#address-cells", 1);
        qemu_fdt_setprop_cell(fdt, node, "#size-cells", 0);
        qemu_fdt_setprop_string(fdt, node, "dr_mode", "host");
        qemu_fdt_setprop_string(fdt, node, "phy_type", "ulpi");
    }

    {
        const char diu_compatible[] = "fsl,diu\0fsl,p1022-diu";
        g_autofree char *node = g_strdup_printf("%s/display@%x", soc,
                                                A1222_DIU_OFFSET);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop(fdt, node, "compatible", diu_compatible,
                         sizeof(diu_compatible));
        qemu_fdt_setprop_cells(fdt, node, "reg", A1222_DIU_OFFSET, 0x1000);
        qemu_fdt_setprop_cells(fdt, node, "interrupts", A1222_DIU_IRQ, 2);
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
    }

    {
        g_autofree char *node = g_strdup_printf("%s/i2c@%x", soc,
                                                A1222_I2C2_OFFSET);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string(fdt, node, "compatible", "fsl-i2c");
        qemu_fdt_setprop_cells(fdt, node, "reg", A1222_I2C2_OFFSET, 0x100);
        qemu_fdt_setprop_cell(fdt, node, "cell-index", 1);
        qemu_fdt_setprop_cells(fdt, node, "interrupts", A1222_I2C_IRQ, 2);
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
        qemu_fdt_setprop_cell(fdt, node, "#address-cells", 1);
        qemu_fdt_setprop_cell(fdt, node, "#size-cells", 0);
        qemu_fdt_setprop(fdt, node, "dfsrr", NULL, 0);
    }

    {
        g_autofree char *i2c = g_strdup_printf("%s/i2c@%x", soc,
                                               A1222_I2C2_OFFSET);
        g_autofree char *codec = g_strdup_printf("%s/codec@%x", i2c,
                                                 A1222_CODEC_ADDR);
        g_autofree char *node = g_strdup_printf("%s/ssi@%x", soc,
                                                A1222_SSI_OFFSET);

        qemu_fdt_add_subnode(fdt, codec);
        qemu_fdt_setprop_string(fdt, codec, "compatible", "wlf,wm8776");
        qemu_fdt_setprop_cell(fdt, codec, "reg", A1222_CODEC_ADDR);
        qemu_fdt_setprop_cell(fdt, codec, "clock-frequency",
                              A1222_CODEC_MCLK);
        qemu_fdt_setprop_cell(fdt, codec, "phandle",
                              qemu_fdt_alloc_phandle(fdt));

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string(fdt, node, "compatible", "fsl,mpc8610-ssi");
        qemu_fdt_setprop_cell(fdt, node, "cell-index", 0);
        qemu_fdt_setprop_cells(fdt, node, "reg", A1222_SSI_OFFSET, 0x100);
        qemu_fdt_setprop_cells(fdt, node, "interrupts", A1222_SSI_IRQ, 2);
        qemu_fdt_setprop_phandle(fdt, node, "interrupt-parent", mpic);
        qemu_fdt_setprop_string(fdt, node, "fsl,mode", "i2s-slave");
        qemu_fdt_setprop_phandle(fdt, node, "codec-handle", codec);
    }

    {
        g_autofree char *node = g_strdup_printf("%s/i2c@%x/rtc@%x", soc,
                                                A1222_I2C2_OFFSET,
                                                A1222_RTC_ADDR);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string(fdt, node, "compatible", "microchip,mcp7940x");
        qemu_fdt_setprop_cell(fdt, node, "reg", A1222_RTC_ADDR);
    }
}

static void a1222_init_sata(MachineState *machine)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    MemoryRegion *ccsr_space = sysbus_mmio_get_region(ccsr, 0);
    int i;

    for (i = 0; i < ARRAY_SIZE(a1222_sata); i++) {
        DeviceState *dev = qdev_new(TYPE_FSL_SATA);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

        object_property_add_child(OBJECT(machine), "sata[*]", OBJECT(dev));
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(mpic, a1222_sata[i].irq));
        memory_region_add_subregion(ccsr_space, a1222_sata[i].offset,
                                    sysbus_mmio_get_region(sbd, 0));
        /* -drive if=ide,index=i (or -hda/-hdb) attaches to SATA port i */
        fsl_sata_init_drive(dev, drive_get(IF_IDE, i, 0));
    }
}

static void a1222_init_usb(MachineState *machine)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    MemoryRegion *ccsr_space = sysbus_mmio_get_region(ccsr, 0);
    int i;

    for (i = 0; i < ARRAY_SIZE(a1222_usb); i++) {
        DeviceState *dev = qdev_new(TYPE_FSL_EHCI);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        g_autofree char *name = g_strdup_printf("usb%d", i);

        object_property_add_child(OBJECT(machine), name, OBJECT(dev));
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(mpic, a1222_usb[i].irq));
        memory_region_add_subregion(ccsr_space, a1222_usb[i].offset,
                                    sysbus_mmio_get_region(sbd, 0));
    }
}

/*
 * The Ethernet controllers, each with its MDIO block and the PHY on it.
 * The eTSECs exist before the rest of the machine, so that they get the
 * first -nic without a model before PCI does.
 */
static void a1222_init_net(MachineState *machine, DeviceState **etsec,
                           int num_etsec)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    MemoryRegion *ccsr_space = sysbus_mmio_get_region(ccsr, 0);
    int i, j;

    for (i = 0; i < num_etsec; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(etsec[i]);
        DeviceState *mdio = qdev_new(TYPE_FSL_ETSEC_MDIO);
        g_autofree char *name = g_strdup_printf("mdio%d", i);

        sysbus_realize_and_unref(sbd, &error_fatal);
        for (j = 0; j < ARRAY_SIZE(a1222_etsec[i].irq); j++) {
            sysbus_connect_irq(sbd, j,
                               qdev_get_gpio_in(mpic, a1222_etsec[i].irq[j]));
        }
        memory_region_add_subregion(ccsr_space, a1222_etsec[i].offset,
                                    sysbus_mmio_get_region(sbd, 0));

        object_property_add_child(OBJECT(machine), name, OBJECT(mdio));
        object_property_set_link(OBJECT(mdio), "etsec", OBJECT(etsec[i]),
                                 &error_fatal);
        qdev_prop_set_uint8(mdio, "phy-addr", a1222_etsec[i].phy_addr);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(mdio), &error_fatal);
        memory_region_add_subregion(ccsr_space, a1222_etsec[i].mdio_offset,
                                    sysbus_mmio_get_region(
                                        SYS_BUS_DEVICE(mdio), 0));
    }
}

static void a1222_init_display(MachineState *machine)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    MemoryRegion *ccsr_space = sysbus_mmio_get_region(ccsr, 0);
    SysBusDevice *i2c1 = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "i2c"));
    DeviceState *dev, *irq_or, *gpio;
    MemoryRegion *gpio_alias;
    I2CSlave *ddc;
    SysBusDevice *sbd;
    I2CBus *i2c;

    /* The display interface unit */
    dev = qdev_new(TYPE_FSL_DIU);
    sbd = SYS_BUS_DEVICE(dev);
    object_property_add_child(OBJECT(machine), "diu", OBJECT(dev));
    object_property_set_link(OBJECT(dev), "memory",
                             OBJECT(get_system_memory()), &error_fatal);
    /* A display card given with -device stays the primary display */
    qdev_prop_set_bit(dev, "primary-console",
                      vga_interface_type != VGA_DEVICE);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(mpic, A1222_DIU_IRQ));
    memory_region_add_subregion(ccsr_space, A1222_DIU_OFFSET,
                                sysbus_mmio_get_region(sbd, 0));

    /* Both I2C controllers share an interrupt */
    irq_or = qdev_new(TYPE_OR_IRQ);
    object_property_add_child(OBJECT(machine), "i2c-irq-or", OBJECT(irq_or));
    qdev_prop_set_uint16(irq_or, "num-lines", 2);
    qdev_realize_and_unref(irq_or, NULL, &error_fatal);
    qdev_connect_gpio_out(irq_or, 0, qdev_get_gpio_in(mpic, A1222_I2C_IRQ));
    sysbus_connect_irq(i2c1, 0, qdev_get_gpio_in(irq_or, 0));

    /* The second I2C controller, with the DVI transmitter and the clock */
    dev = qdev_new("mpc-i2c");
    sbd = SYS_BUS_DEVICE(dev);
    object_property_add_child(OBJECT(machine), "i2c2", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(irq_or, 1));
    memory_region_add_subregion(ccsr_space, A1222_I2C2_OFFSET,
                                sysbus_mmio_get_region(sbd, 0));
    i2c_slave_create_simple(I2C_BUS(qdev_get_child_bus(dev, "i2c")),
                            TYPE_MCP7940, A1222_RTC_ADDR);

    /*
     * The DVI port's DDC, which software bit-bangs through two GPIO pins,
     * with the monitor's EDID
     */
    gpio = qdev_new("mpc8xxx_gpio");
    object_property_add_child(OBJECT(machine), "gpio3", OBJECT(gpio));
    qdev_prop_set_uint32(gpio, "pullups",
                         BIT(31 - A1222_DDC_SDA) | BIT(31 - A1222_DDC_SCL));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio), &error_fatal);
    gpio_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(gpio_alias, OBJECT(gpio), "gpio3",
                             sysbus_mmio_get_region(SYS_BUS_DEVICE(gpio), 0),
                             0, 0x100);
    memory_region_add_subregion(ccsr_space, A1222_GPIO3_OFFSET, gpio_alias);

    dev = qdev_new(TYPE_GPIO_I2C);
    object_property_add_child(OBJECT(machine), "ddc", OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    qdev_connect_gpio_out(gpio, A1222_DDC_SDA,
                          qdev_get_gpio_in(dev, BITBANG_I2C_SDA));
    qdev_connect_gpio_out(gpio, A1222_DDC_SCL,
                          qdev_get_gpio_in(dev, BITBANG_I2C_SCL));
    qdev_connect_gpio_out(dev, 0, qdev_get_gpio_in(gpio, A1222_DDC_SDA));
    i2c = I2C_BUS(qdev_get_child_bus(dev, "i2c"));
    ddc = i2c_slave_new(TYPE_I2CDDC, 0x50);
    /* The AmigaOS recovery image's default Workbench mode */
    qdev_prop_set_uint32(DEVICE(ddc), "xres", 1280);
    qdev_prop_set_uint32(DEVICE(ddc), "yres", 768);
    i2c_slave_realize_and_unref(ddc, i2c, &error_fatal);
}

/*
 * The DMA controllers, which the AmigaOS kernel hands out to its users
 * channel by channel: graphics.library copies its shadow of the DIU's frame
 * buffer through any free channel of either, the audio driver feeds the
 * SSI through DMA1's first one
 */
static void a1222_init_dma(MachineState *machine)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    int i, j;

    for (i = 0; i < ARRAY_SIZE(a1222_dma); i++) {
        DeviceState *dev = qdev_new(TYPE_FSL_DMA);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

        object_property_add_child(OBJECT(machine), a1222_dma[i].name,
                                  OBJECT(dev));
        sysbus_realize_and_unref(sbd, &error_fatal);
        for (j = 0; j < 4; j++) {
            sysbus_connect_irq(sbd, j,
                               qdev_get_gpio_in(mpic, a1222_dma[i].irq + j));
        }
        memory_region_add_subregion(sysbus_mmio_get_region(ccsr, 0),
                                    a1222_dma[i].offset,
                                    sysbus_mmio_get_region(sbd, 0));
    }
}

/*
 * The SSI and its codec, which play what DMA channel 0 feeds the SSI, and
 * record what channel 1 takes from it
 */
static void a1222_init_audio(MachineState *machine)
{
    DeviceState *mpic = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "pic"));
    SysBusDevice *ccsr = SYS_BUS_DEVICE(
        object_resolve_path_component(OBJECT(machine), "e500-ccsr"));
    DeviceState *dma = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                            "dma"));
    DeviceState *i2c2 = DEVICE(object_resolve_path_component(OBJECT(machine),
                                                             "i2c2"));
    I2CSlave *codec = i2c_slave_new(TYPE_WM8776, A1222_CODEC_ADDR);
    DeviceState *dev = qdev_new(TYPE_FSL_SSI);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    object_property_add_child(OBJECT(machine), "codec", OBJECT(codec));
    if (machine->audiodev) {
        qdev_prop_set_string(DEVICE(codec), "audiodev", machine->audiodev);
    }
    qdev_prop_set_uint32(DEVICE(codec), "mclk-freq", A1222_CODEC_MCLK);
    i2c_slave_realize_and_unref(codec,
                                I2C_BUS(qdev_get_child_bus(i2c2, "i2c")),
                                &error_fatal);

    object_property_add_child(OBJECT(machine), "ssi", OBJECT(dev));
    object_property_set_link(OBJECT(dev), "codec", OBJECT(codec),
                             &error_fatal);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(mpic, A1222_SSI_IRQ));
    qdev_connect_gpio_out_named(dev, FSL_SSI_DREQ, FSL_SSI_DREQ_TX,
                                qdev_get_gpio_in_named(dma, FSL_DMA_DREQ,
                                                       A1222_SSI_DMA_TX));
    qdev_connect_gpio_out_named(dev, FSL_SSI_DREQ, FSL_SSI_DREQ_RX,
                                qdev_get_gpio_in_named(dma, FSL_DMA_DREQ,
                                                       A1222_SSI_DMA_RX));
    memory_region_add_subregion(sysbus_mmio_get_region(ccsr, 0),
                                A1222_SSI_OFFSET,
                                sysbus_mmio_get_region(sbd, 0));
}

static void a1222_init(MachineState *machine)
{
    DeviceState *etsec[ARRAY_SIZE(a1222_etsec)];
    int i;

    if (machine->ram_size > 2 * GiB) {
        error_report("The A1222 machine supports up to 2GB of RAM");
        exit(1);
    }

    /*
     * The first eTSEC, the A1222's Ethernet port, takes the first -nic
     * without a model.  The second one, which the board leaves unused,
     * is there only for a further -nic with model=eTSEC.
     */
    for (i = 0; i < ARRAY_SIZE(a1222_etsec); i++) {
        g_autofree char *name = g_strdup_printf("etsec%d", i);

        if (i > 0 && !qemu_find_nic_info(TYPE_ETSEC_COMMON, false, NULL)) {
            break;
        }
        etsec[i] = qdev_new(TYPE_ETSEC_COMMON);
        object_property_add_child(OBJECT(machine), name, OBJECT(etsec[i]));
        qemu_configure_nic_device(etsec[i], i == 0, NULL);
    }

    ppce500_init(machine);
    a1222_init_net(machine, etsec, i);
    a1222_init_sata(machine);
    a1222_init_usb(machine);
    a1222_init_display(machine);
    a1222_init_dma(machine);
    a1222_init_audio(machine);
}

static void a1222_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PPCE500MachineClass *pmc = PPCE500_MACHINE_CLASS(oc);

    pmc->pci_first_slot = 0x11;
    pmc->pci_nr_slots = 2;
    pmc->fixup_devtree = a1222_fixup_devtree;
    pmc->mpic_version = OPENPIC_MODEL_FSL_MPIC_20;
    pmc->platform_bus_base = 0xF0000000ULL;
    pmc->platform_bus_size = 8 * MiB;
    pmc->platform_bus_first_irq = 5;
    pmc->platform_bus_num_irqs = 10;
    pmc->ccsrbar_base = A1222_CCSR_BASE;
    /*
     * The PCI controller as the first of the P1022's PCIe controllers, with
     * U-Boot's 36-bit windows for it, which the AmigaOS kernel has built in
     */
    pmc->pci_regs_offset = 0xA000;
    pmc->pci_mmio_base = 0xC40000000ULL;
    pmc->pci_mmio_bus_base = 0xE0000000ULL;
    pmc->pci_pio_base = 0xFFFC20000ULL;
    /* The AmigaOS kernel has INTA-INTD of this controller at MPIC 0-3 */
    pmc->pci_first_irq = 0;
    pmc->spin_base = 0xFF700000ULL;
    pmc->clock_freq = PLATFORM_CLK_FREQ_HZ;
    pmc->bus_freq = PLATFORM_CLK_FREQ_HZ;
    /*
     * The e500 time base counts at an eighth of the platform clock, as the
     * AmigaOS kernel works out from the bus frequency the boot loader
     * passes it; at the full platform clock, AmigaOS time ran 8 times fast
     */
    pmc->tb_freq = PLATFORM_CLK_FREQ_HZ / 8;

    mc->desc = "AmigaOne A1222 (P1022)";
    mc->init = a1222_init;
    mc->max_cpus = 2;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("e500v2_v30");
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "a1222.ram";
    mc->default_nic = "virtio-net-pci";
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    machine_add_audiodev_property(mc);
}

#define TYPE_A1222_MACHINE  MACHINE_TYPE_NAME("a1222")

static const TypeInfo a1222_info = {
    .name          = TYPE_A1222_MACHINE,
    .parent        = TYPE_PPCE500_MACHINE,
    .class_init    = a1222_machine_class_init,
};

static void a1222_register_types(void)
{
    type_register_static(&a1222_info);
}

type_init(a1222_register_types)
