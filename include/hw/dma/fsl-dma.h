/*
 * Freescale Elo Plus DMA controller ("fsl,eloplus-dma")
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_FSL_DMA_H
#define HW_DMA_FSL_DMA_H

#include "qom/object.h"

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: the four channels' registers at 0x80 apart,
 *    followed by the general status register at 0x200
 *  + sysbus IRQs 0-3: the channels' interrupts (level, active high)
 *  + named GPIO inputs FSL_DMA_DREQ 0-3: the channels' DMA request lines
 *    (active high), which pace a channel whose mode register enables
 *    external master start or pause
 *
 * The controller moves data within the system memory address space.
 */
#define TYPE_FSL_DMA "fsl-dma"
#define FSL_DMA_DREQ "dreq"
OBJECT_DECLARE_SIMPLE_TYPE(FslDmaState, FSL_DMA)

#endif
