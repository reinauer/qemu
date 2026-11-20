/*
 * Wolfson WM8776 stereo audio codec
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AUDIO_WM8776_H
#define HW_AUDIO_WM8776_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

/*
 * QEMU interface:
 *  + an I2C slave, whose 7-bit address the board sets (0x1a or 0x1b on
 *    real hardware, as the CE pin selects), taking two-byte register writes
 *  + property "audiodev": the audio backend for the DAC
 *  + property "mclk-freq": the master clock, from which the master mode
 *    control register's DAC ratio gives the sample rate
 *
 * The DAC plays what the digital audio interface (I2S) controller hands
 * it with wm8776_dac_frame(); when it has taken samples from its buffer,
 * or the ADC has recorded some, it calls back the controller, which
 * wm8776_set_data_req() registers, to hand it more or take them with
 * wm8776_adc_frame().  The ADC records, from the audio backend's input
 * voice, while the controller enables it with wm8776_adc_enable().
 */
#define TYPE_WM8776 "wm8776"
OBJECT_DECLARE_SIMPLE_TYPE(WM8776State, WM8776)

typedef void (*WM8776DataReq)(void *opaque);

void wm8776_set_data_req(WM8776State *s, WM8776DataReq data_req,
                         void *opaque);

/* The number of stereo frames the DAC can take now */
int wm8776_dac_space(WM8776State *s);

/* Play a stereo frame, as signed 32-bit samples */
void wm8776_dac_frame(WM8776State *s, int32_t left, int32_t right);

/* Start or stop recording */
void wm8776_adc_enable(WM8776State *s, bool on);

/* The number of recorded stereo frames to take */
int wm8776_adc_avail(WM8776State *s);

/* Take a recorded stereo frame, as signed 32-bit samples */
void wm8776_adc_frame(WM8776State *s, int32_t *left, int32_t *right);

/* Whether recorded frames were lost since the last call, as none were taken */
bool wm8776_adc_overrun(WM8776State *s);

#endif
