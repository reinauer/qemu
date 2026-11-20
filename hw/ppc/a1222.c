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
#include "system/device_tree.h"
#include "hw/ppc/openpic.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "target/ppc/cpu.h"

static void a1222_fixup_devtree(void *fdt)
{
    const char model[] = "AmigaOne A1222";
    const char compatible[] = "fsl,P1022\0fsl,P1022DS";

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));
}

static void a1222_init(MachineState *machine)
{
    if (machine->ram_size > 2 * GiB) {
        error_report("The A1222 machine supports up to 2GB of RAM");
        exit(1);
    }

    ppce500_init(machine);
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
    pmc->ccsrbar_base = 0xFFE00000ULL;
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
