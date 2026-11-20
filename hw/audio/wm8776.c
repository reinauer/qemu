/*
 * Wolfson WM8776 stereo audio codec
 *
 * A 24-bit, 192 kHz stereo codec with a five-input ADC multiplexer and a
 * headphone amplifier, controlled through write-only registers of 9 data
 * bits, which it takes over a two-wire (I2C) or three-wire interface.
 *
 * Emulated: the register file, the DAC's sample rate from the master
 * clock and the master mode control register, and its digital volume,
 * soft mute, power down and output multiplexer as the volume of the audio
 * backend's voice, and the ADC, which records the audio backend's input
 * voice, through its gain, mute, power down and input multiplexer, where
 * every input is the host's, at the sample rate of the master mode control
 * register, or silence in real time when the input voice falls behind or
 * there is none.  The digital audio interface's format registers only
 * describe how the samples go, which the controller attached takes care
 * of.  Not emulated: the headphone amplifier, which leaves the line output
 * as is, the ALC, the limiter and the noise gate.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/audio.h"
#include "qapi/error.h"
#include "hw/audio/wm8776.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

#define WM8776_DACLVOL      0x03
#define WM8776_DACRVOL      0x04
#define WM8776_DACMASTER    0x05
#define WM8776_DACMUTE      0x08
#define WM8776_MSTRCTRL     0x0c
#define WM8776_PWRDOWN      0x0d
#define WM8776_ADCLVOL      0x0e
#define WM8776_ADCRVOL      0x0f
#define WM8776_ADCMUX       0x15
#define WM8776_OUTMUX       0x16
#define WM8776_RESET        0x17
#define WM8776_NUM_REGS     0x17

#define DACMUTE_DMUTE       0x001
#define MSTRCTRL_ADCRATE(v) ((v) & 7)
#define MSTRCTRL_DACRATE(v) (((v) >> 4) & 7)
#define PWRDOWN_PDWN        0x001
#define PWRDOWN_ADCPD       0x002
#define PWRDOWN_DACPD       0x004
#define PWRDOWN_AINPD       0x040
#define ADCMUX_AMX          0x01f
#define ADCMUX_MUTERA       0x040
#define ADCMUX_MUTELA       0x080
#define ADCMUX_LRBOTH       0x100
#define ADC_GAIN_0DB        0xcf
#define OUTMUX_DAC          0x001
#define VOL_MASK            0x0ff

/* The DAC's buffer, and how much of it to use: some 25 ms worth */
#define WM8776_BUF_FRAMES   4096
#define WM8776_BUF_MS       25

/* How often to take samples when no audio backend voice does */
#define WM8776_TICK_NS      (10 * SCALE_MS)

/*
 * How far the input voice may fall behind before the ADC records silence
 * in its place, and how much of that it catches up at most
 */
#define WM8776_IN_LAG_NS    (100 * SCALE_MS)
#define WM8776_IN_MAX_NS    (200 * SCALE_MS)

/* The input voice's samples go through a buffer of this many frames */
#define WM8776_IN_CHUNK     256

struct WM8776State {
    I2CSlave parent_obj;

    uint16_t regs[WM8776_NUM_REGS];
    uint8_t byte;
    int32_t pos;

    /* The DAC's buffer of stereo frames of signed 32-bit samples */
    uint32_t buf[WM8776_BUF_FRAMES][2];
    uint32_t head;
    uint32_t count;
    uint32_t limit;

    uint32_t freq;
    uint32_t mclk_freq;
    AudioBackend *audio_be;
    SWVoiceOut *voice;
    QEMUTimer *timer;
    int64_t tick_ns;

    /* The ADC's buffer, which the controller takes recorded frames from */
    uint32_t adc[WM8776_BUF_FRAMES][2];
    uint32_t adc_head;
    uint32_t adc_count;
    uint32_t adc_limit;
    bool adc_overrun;
    bool adc_on;
    uint32_t adc_freq;
    SWVoiceIn *voice_in;
    /* Whether the audio backend has no input voice to give */
    bool no_voice_in;
    QEMUTimer *adc_timer;
    /* The time up to which the ADC has recorded */
    int64_t in_ns;

    WM8776DataReq data_req;
    void *opaque;
};

/* The power on values, from the datasheet */
static const uint16_t wm8776_defaults[WM8776_NUM_REGS] = {
    0x079, 0x079, 0x079, 0x0ff, 0x0ff, 0x0ff, 0x000, 0x090,
    0x000, 0x000, 0x022, 0x022, 0x022, 0x008, 0x0cf, 0x0cf,
    0x07b, 0x000, 0x032, 0x000, 0x0a6, 0x001, 0x001,
};

/* The master clock to sample rate ratios of MSTRCTRL[DACRATE] */
static const uint16_t wm8776_ratios[8] = {
    128, 192, 256, 384, 512, 768, 768, 768,
};

void wm8776_set_data_req(WM8776State *s, WM8776DataReq data_req,
                         void *opaque)
{
    s->data_req = data_req;
    s->opaque = opaque;
}

int wm8776_dac_space(WM8776State *s)
{
    return s->limit - s->count;
}

void wm8776_dac_frame(WM8776State *s, int32_t left, int32_t right)
{
    uint32_t tail = (s->head + s->count) % WM8776_BUF_FRAMES;

    if (s->count >= s->limit) {
        return;
    }
    s->buf[tail][0] = left;
    s->buf[tail][1] = right;
    s->count++;
}

int wm8776_adc_avail(WM8776State *s)
{
    return s->adc_count;
}

void wm8776_adc_frame(WM8776State *s, int32_t *left, int32_t *right)
{
    *left = s->adc_count ? s->adc[s->adc_head][0] : 0;
    *right = s->adc_count ? s->adc[s->adc_head][1] : 0;
    if (s->adc_count) {
        s->adc_head = (s->adc_head + 1) % WM8776_BUF_FRAMES;
        s->adc_count--;
    }
}

bool wm8776_adc_overrun(WM8776State *s)
{
    bool overrun = s->adc_overrun;

    s->adc_overrun = false;
    return overrun;
}

/* A gain register's 0.5 dB steps from 0 dB, as a 16.16 fixed point factor */
static int64_t wm8776_adc_gain(uint16_t reg)
{
    uint8_t v = reg & VOL_MASK;

    if (!v) {
        return 0;
    }
    return llrint(65536.0 * pow(10.0, (v - ADC_GAIN_0DB) / 2.0 / 20.0));
}

static int32_t wm8776_adc_scale(int32_t sample, int64_t gain)
{
    int64_t v = (sample * gain) >> 16;

    return MIN(MAX(v, INT32_MIN), INT32_MAX);
}

/*
 * Record a frame, through the input multiplexer, gain and mute; when the
 * buffer is full, the oldest frame gives way
 */
static void wm8776_adc_record(WM8776State *s, int32_t left, int32_t right)
{
    uint16_t mux = s->regs[WM8776_ADCMUX];
    uint16_t rreg = (mux & ADCMUX_LRBOTH) ? WM8776_ADCLVOL : WM8776_ADCRVOL;
    uint16_t rmute = (mux & ADCMUX_LRBOTH) ? ADCMUX_MUTELA : ADCMUX_MUTERA;
    uint32_t tail;

    /* Every input is the host's, and powered down ones are silent */
    if (!(mux & ADCMUX_AMX) ||
        (s->regs[WM8776_PWRDOWN] &
         (PWRDOWN_PDWN | PWRDOWN_ADCPD | PWRDOWN_AINPD))) {
        left = right = 0;
    }
    left = (mux & ADCMUX_MUTELA) ? 0 :
           wm8776_adc_scale(left, wm8776_adc_gain(s->regs[WM8776_ADCLVOL]));
    right = (mux & rmute) ? 0 :
            wm8776_adc_scale(right, wm8776_adc_gain(s->regs[rreg]));

    if (s->adc_count >= s->adc_limit) {
        s->adc_head = (s->adc_head + 1) % WM8776_BUF_FRAMES;
        s->adc_count--;
        s->adc_overrun = true;
    }
    tail = (s->adc_head + s->adc_count) % WM8776_BUF_FRAMES;
    s->adc[tail][0] = left;
    s->adc[tail][1] = right;
    s->adc_count++;
}

/* Record what the input voice has */
static void wm8776_audio_in(void *opaque, int avail)
{
    WM8776State *s = opaque;
    int32_t buf[WM8776_IN_CHUNK][2];
    uint32_t frames = 0;

    while (s->adc_on && s->voice_in) {
        size_t n = audio_be_read(s->audio_be, s->voice_in, buf,
                                 sizeof(buf)) / sizeof(buf[0]);
        size_t i;

        for (i = 0; i < n; i++) {
            wm8776_adc_record(s, buf[i][0], buf[i][1]);
        }
        frames += n;
        if (n < ARRAY_SIZE(buf)) {
            break;
        }
    }
    s->in_ns += muldiv64(frames, NANOSECONDS_PER_SECOND, s->adc_freq);
    if (frames && s->data_req) {
        s->data_req(s->opaque);
    }
}

/*
 * Keep the ADC recording in real time: what the input voice does not
 * deliver, as there is none, or it falls behind, becomes silence
 */
static void wm8776_adc_tick(void *opaque)
{
    WM8776State *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t lag;

    s->in_ns = MAX(s->in_ns, now - WM8776_IN_MAX_NS);
    lag = now - s->in_ns;
    if (lag > (s->voice_in ? WM8776_IN_LAG_NS : 0)) {
        uint64_t frames = muldiv64(lag, s->adc_freq, NANOSECONDS_PER_SECOND);
        uint64_t i;

        for (i = 0; i < frames; i++) {
            wm8776_adc_record(s, 0, 0);
        }
        s->in_ns += muldiv64(frames, NANOSECONDS_PER_SECOND, s->adc_freq);
        if (frames && s->data_req) {
            s->data_req(s->opaque);
        }
    }
    timer_mod(s->adc_timer, now + WM8776_TICK_NS);
}

/* (Re)open the input voice at the ADC's sample rate */
static void wm8776_open_in(WM8776State *s)
{
    struct audsettings as = {
        .freq = s->adc_freq,
        .nchannels = 2,
        .fmt = AUDIO_FORMAT_S32,
        .big_endian = HOST_BIG_ENDIAN,
    };

    if (!s->audio_be || s->no_voice_in) {
        return;
    }
    s->voice_in = audio_be_open_in(s->audio_be, s->voice_in,
                                   TYPE_WM8776 ".in", s, wm8776_audio_in,
                                   &as);
    if (s->voice_in) {
        audio_be_set_active_in(s->audio_be, s->voice_in, true);
    } else {
        /* Record silence from now on, rather than fail again */
        s->no_voice_in = true;
    }
}

void wm8776_adc_enable(WM8776State *s, bool on)
{
    if (on == s->adc_on) {
        return;
    }
    trace_wm8776_adc(on, s->adc_freq);
    s->adc_on = on;
    s->adc_head = 0;
    s->adc_count = 0;
    s->adc_overrun = false;
    if (on) {
        /* Only open the input, such as a microphone, when recording */
        wm8776_open_in(s);
        s->in_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod(s->adc_timer, s->in_ns + WM8776_TICK_NS);
    } else {
        if (s->voice_in) {
            audio_be_set_active_in(s->audio_be, s->voice_in, false);
        }
        timer_del(s->adc_timer);
    }
}

/* Take @frames frames from the buffer, playing them if there is a voice */
static void wm8776_take(WM8776State *s, uint32_t frames)
{
    while (frames && s->count) {
        uint32_t n = MIN(MIN(frames, s->count),
                         WM8776_BUF_FRAMES - s->head);

        if (s->voice) {
            n = audio_be_write(s->audio_be, s->voice, s->buf[s->head],
                               n * sizeof(s->buf[0])) / sizeof(s->buf[0]);
            if (!n) {
                break;
            }
        }
        s->head = (s->head + n) % WM8776_BUF_FRAMES;
        s->count -= n;
        frames -= n;
    }
    if (s->data_req) {
        s->data_req(s->opaque);
    }
}

static void wm8776_audio_out(void *opaque, int free)
{
    WM8776State *s = opaque;

    wm8776_take(s, free / sizeof(s->buf[0]));
}

/* Without an audio backend voice, take the samples in real time */
static void wm8776_tick(void *opaque)
{
    WM8776State *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t frames = muldiv64(now - s->tick_ns, s->freq,
                               NANOSECONDS_PER_SECOND);

    s->tick_ns += muldiv64(frames, NANOSECONDS_PER_SECOND, s->freq);
    wm8776_take(s, MIN(frames, WM8776_BUF_FRAMES));
    timer_mod(s->timer, now + WM8776_TICK_NS);
}

/* An attenuation register's 0.5 dB steps from 0 dB as a linear volume */
static uint8_t wm8776_volume(uint16_t reg)
{
    uint8_t v = reg & VOL_MASK;

    if (!v) {
        return 0;
    }
    return lrint(255.0 * pow(10.0, (v - 255) / 2.0 / 20.0));
}

static void wm8776_update_volume(WM8776State *s)
{
    bool mute = (s->regs[WM8776_DACMUTE] & DACMUTE_DMUTE) ||
                (s->regs[WM8776_PWRDOWN] & (PWRDOWN_PDWN | PWRDOWN_DACPD)) ||
                !(s->regs[WM8776_OUTMUX] & OUTMUX_DAC);
    uint8_t left = wm8776_volume(s->regs[WM8776_DACLVOL]);
    uint8_t right = wm8776_volume(s->regs[WM8776_DACRVOL]);

    trace_wm8776_volume(mute, left, right);
    if (s->voice) {
        audio_be_set_volume_out_lr(s->audio_be, s->voice, mute, left, right);
    }
}

/* (Re)open the voice at the sample rate the registers select */
static void wm8776_update_rate(WM8776State *s)
{
    uint32_t freq = s->mclk_freq /
                    wm8776_ratios[MSTRCTRL_DACRATE(s->regs[WM8776_MSTRCTRL])];
    uint32_t adc_freq = s->mclk_freq /
                    wm8776_ratios[MSTRCTRL_ADCRATE(s->regs[WM8776_MSTRCTRL])];
    struct audsettings as = {
        .freq = freq,
        .nchannels = 2,
        .fmt = AUDIO_FORMAT_S32,
        .big_endian = HOST_BIG_ENDIAN,
    };

    if (adc_freq != s->adc_freq) {
        s->adc_freq = adc_freq;
        s->adc_limit = MIN(WM8776_BUF_FRAMES,
                           MAX(adc_freq * WM8776_BUF_MS / 1000, 1));
        s->adc_count = MIN(s->adc_count, s->adc_limit);
        if (s->adc_on) {
            trace_wm8776_adc(true, adc_freq);
            wm8776_open_in(s);
        }
    }
    if (freq == s->freq) {
        return;
    }
    trace_wm8776_rate(freq);
    s->freq = freq;
    s->limit = MIN(WM8776_BUF_FRAMES,
                   MAX(freq * WM8776_BUF_MS / 1000, 1));
    s->count = MIN(s->count, s->limit);
    if (s->audio_be) {
        s->voice = audio_be_open_out(s->audio_be, s->voice, TYPE_WM8776, s,
                                     wm8776_audio_out, &as);
    }
    if (s->voice) {
        timer_del(s->timer);
        audio_be_set_active_out(s->audio_be, s->voice, true);
        wm8776_update_volume(s);
    } else {
        s->tick_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod(s->timer, s->tick_ns + WM8776_TICK_NS);
    }
}

static void wm8776_reset_regs(WM8776State *s)
{
    memcpy(s->regs, wm8776_defaults, sizeof(s->regs));
    wm8776_update_rate(s);
    wm8776_update_volume(s);
}

static void wm8776_write(WM8776State *s, uint8_t reg, uint16_t val)
{
    trace_wm8776_write(reg, val);
    switch (reg) {
    case WM8776_RESET:
        wm8776_reset_regs(s);
        return;
    case 0x02:
        /* The headphone master volume sets both channels */
        s->regs[0x00] = val;
        s->regs[0x01] = val;
        break;
    case WM8776_DACMASTER:
        s->regs[WM8776_DACLVOL] = val;
        s->regs[WM8776_DACRVOL] = val;
        break;
    default:
        if (reg >= WM8776_NUM_REGS) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: no register 0x%02x\n",
                          __func__, reg);
            return;
        }
        break;
    }
    s->regs[reg] = val;

    switch (reg) {
    case WM8776_DACLVOL:
    case WM8776_DACRVOL:
    case WM8776_DACMASTER:
    case WM8776_DACMUTE:
    case WM8776_PWRDOWN:
    case WM8776_OUTMUX:
        wm8776_update_volume(s);
        break;
    case WM8776_MSTRCTRL:
        wm8776_update_rate(s);
        break;
    }
}

static int wm8776_event(I2CSlave *i2c, enum i2c_event event)
{
    WM8776State *s = WM8776(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->pos = 0;
        break;
    case I2C_START_RECV:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: the codec's registers are "
                      "write-only\n", __func__);
        break;
    case I2C_FINISH:
        if (s->pos == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: incomplete register "
                          "write\n", __func__);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t wm8776_recv(I2CSlave *i2c)
{
    return 0xff;
}

static int wm8776_send(I2CSlave *i2c, uint8_t data)
{
    WM8776State *s = WM8776(i2c);

    switch (s->pos++) {
    case 0:
        s->byte = data;
        break;
    case 1:
        /* A 7-bit register address, then 9 data bits */
        wm8776_write(s, s->byte >> 1, (s->byte & 1) << 8 | data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: extra byte 0x%02x\n", __func__,
                      data);
        break;
    }
    return 0;
}

static void wm8776_reset(DeviceState *dev)
{
    WM8776State *s = WM8776(dev);

    s->pos = 0;
    s->head = 0;
    s->count = 0;
    s->adc_head = 0;
    s->adc_count = 0;
    s->adc_overrun = false;
    wm8776_reset_regs(s);
}

static void wm8776_realize(DeviceState *dev, Error **errp)
{
    WM8776State *s = WM8776(dev);

    if (!audio_be_check(&s->audio_be, errp)) {
        return;
    }
    if (!s->mclk_freq) {
        error_setg(errp, "mclk-freq must not be zero");
        return;
    }
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, wm8776_tick, s);
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, wm8776_adc_tick, s);
}

static void wm8776_unrealize(DeviceState *dev)
{
    WM8776State *s = WM8776(dev);

    if (s->voice) {
        audio_be_close_out(s->audio_be, s->voice);
        s->voice = NULL;
    }
    if (s->voice_in) {
        audio_be_close_in(s->audio_be, s->voice_in);
        s->voice_in = NULL;
    }
    timer_free(s->timer);
    timer_free(s->adc_timer);
}

static int wm8776_post_load(void *opaque, int version_id)
{
    WM8776State *s = opaque;

    bool adc_on = s->adc_on;

    if (s->head >= WM8776_BUF_FRAMES || s->count > WM8776_BUF_FRAMES ||
        s->adc_head >= WM8776_BUF_FRAMES ||
        s->adc_count > WM8776_BUF_FRAMES) {
        return -EINVAL;
    }
    s->freq = 0;
    s->adc_freq = 0;
    s->adc_on = false;
    wm8776_update_rate(s);
    wm8776_update_volume(s);
    if (adc_on) {
        uint32_t head = s->adc_head, count = s->adc_count;

        wm8776_adc_enable(s, true);
        s->adc_head = head;
        s->adc_count = MIN(count, s->adc_limit);
    }
    return 0;
}

static const VMStateDescription vmstate_wm8776 = {
    .name = TYPE_WM8776,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = wm8776_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, WM8776State),
        VMSTATE_UINT16_ARRAY(regs, WM8776State, WM8776_NUM_REGS),
        VMSTATE_UINT8(byte, WM8776State),
        VMSTATE_INT32(pos, WM8776State),
        VMSTATE_UINT32_2DARRAY(buf, WM8776State, WM8776_BUF_FRAMES, 2),
        VMSTATE_UINT32(head, WM8776State),
        VMSTATE_UINT32(count, WM8776State),
        VMSTATE_UINT32_2DARRAY_V(adc, WM8776State, WM8776_BUF_FRAMES, 2, 2),
        VMSTATE_UINT32_V(adc_head, WM8776State, 2),
        VMSTATE_UINT32_V(adc_count, WM8776State, 2),
        VMSTATE_BOOL_V(adc_overrun, WM8776State, 2),
        VMSTATE_BOOL_V(adc_on, WM8776State, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const Property wm8776_properties[] = {
    DEFINE_AUDIO_PROPERTIES(WM8776State, audio_be),
    DEFINE_PROP_UINT32("mclk-freq", WM8776State, mclk_freq, 12288000),
};

static void wm8776_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->desc = "Wolfson WM8776 audio codec";
    dc->realize = wm8776_realize;
    dc->unrealize = wm8776_unrealize;
    dc->vmsd = &vmstate_wm8776;
    device_class_set_legacy_reset(dc, wm8776_reset);
    device_class_set_props(dc, wm8776_properties);
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    sc->event = wm8776_event;
    sc->recv = wm8776_recv;
    sc->send = wm8776_send;
}

static const TypeInfo wm8776_types[] = {
    {
        .name          = TYPE_WM8776,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(WM8776State),
        .class_init    = wm8776_class_init,
    },
};

DEFINE_TYPES(wm8776_types)
