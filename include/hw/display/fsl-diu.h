/*
 * Freescale Display Interface Unit (DIU), as found in the MPC8610 and P1022
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DISPLAY_FSL_DIU_H
#define HW_DISPLAY_FSL_DIU_H

#include "qom/object.h"

/*
 * QEMU interface:
 *  + QOM property "memory": the memory region the DIU fetches its area
 *    descriptors, frame buffers, palette, gamma table and cursor from
 *  + QOM property "primary-console": whether the DIU's console comes
 *    first (the default), or after those of all cold-plugged devices
 *  + sysbus MMIO region 0: the 4 KiB register block
 *  + sysbus IRQ 0: the DIU interrupt (level, active high)
 */
#define TYPE_FSL_DIU "fsl-diu"
OBJECT_DECLARE_SIMPLE_TYPE(FslDiuState, FSL_DIU)

#endif
