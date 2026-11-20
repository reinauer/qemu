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
#include "hw/core/sysbus.h"
#include "hw/ide/fsl-sata.h"
#include "hw/usb/hcd-ehci.h"
#include "system/blockdev.h"
#include "system/device_tree.h"
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

static void a1222_init(MachineState *machine)
{
    if (machine->ram_size > 2 * GiB) {
        error_report("The A1222 machine supports up to 2GB of RAM");
        exit(1);
    }

    ppce500_init(machine);
    a1222_init_sata(machine);
    a1222_init_usb(machine);
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
