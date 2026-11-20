/*
 * Freescale Synchronous Serial Interface (SSI)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AUDIO_FSL_SSI_H
#define HW_AUDIO_FSL_SSI_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: the registers
 *  + sysbus IRQ 0: the interrupt (level, active high)
 *  + named GPIO outputs FSL_SSI_DREQ: the transmit (0) and receive (1)
 *    DMA requests, active high, for DMA controller channels
 *  + property "codec": the WM8776 codec on the I2S link, which plays the
 *    transmitted samples and paces the transmitter
 */
#define TYPE_FSL_SSI "fsl-ssi"
OBJECT_DECLARE_SIMPLE_TYPE(FslSsiState, FSL_SSI)

#define FSL_SSI_DREQ "dreq"
#define FSL_SSI_DREQ_TX 0
#define FSL_SSI_DREQ_RX 1

#endif
