#ifndef PPCE500_H
#define PPCE500_H

#include "hw/core/boards.h"
#include "hw/core/platform-bus.h"
#include "qom/object.h"

#define PLATFORM_CLK_FREQ_HZ (400 * 1000 * 1000)

struct PPCE500MachineState {
    /*< private >*/
    MachineState parent_obj;

    /* points to instance of TYPE_PLATFORM_BUS_DEVICE if
     * board supports dynamic sysbus devices
     */
    PlatformBusDevice *pbus_dev;

    /* The local access registers at the start of CCSR */
    MemoryRegion lac;
    uint32_t lac_regs[0x1000 / 4];
};

struct PPCE500MachineClass {
    /*< private >*/
    MachineClass parent_class;

    /* required -- must at least add toplevel board compatible */
    void (*fixup_devtree)(void *fdt);

    int pci_first_slot;
    int pci_nr_slots;

    int mpic_version;
    bool has_mpc8xxx_gpio;
    bool has_esdhc;
    hwaddr platform_bus_base;
    hwaddr platform_bus_size;
    int platform_bus_first_irq;
    int platform_bus_num_irqs;
    hwaddr ccsrbar_base;
    hwaddr pci_pio_base;
    hwaddr pci_mmio_base;
    hwaddr pci_mmio_bus_base;
    /* Offset of the PCI controller in CCSR; 0 means the MPC8544's 0x8000 */
    hwaddr pci_regs_offset;
    hwaddr spin_base;
    uint32_t clock_freq;
    uint32_t bus_freq;
    uint32_t tb_freq;
};

void ppce500_init(MachineState *machine);

#define TYPE_PPCE500_MACHINE      "ppce500-base-machine"
OBJECT_DECLARE_TYPE(PPCE500MachineState, PPCE500MachineClass, PPCE500_MACHINE)

#endif
