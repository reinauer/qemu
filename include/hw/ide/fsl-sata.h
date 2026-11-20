/*
 * Freescale eSATA controller ("fsl,pq-sata-v2"), as found in the
 * MPC8315/MPC837x/P1022/P1020 family
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IDE_FSL_SATA_H
#define HW_IDE_FSL_SATA_H

#include "qom/object.h"

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: the controller's 4 KiB register block
 *  + sysbus IRQ 0: the controller interrupt (level, active high)
 *  + one IDE bus with a single unit, for the device on the SATA port
 */
#define TYPE_FSL_SATA "fsl-sata"
OBJECT_DECLARE_SIMPLE_TYPE(FslSataState, FSL_SATA)

void fsl_sata_init_drive(DeviceState *dev, DriveInfo *hd);

#endif
