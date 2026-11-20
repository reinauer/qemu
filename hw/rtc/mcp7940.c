/*
 * Microchip MCP7940N / MCP7941x I2C real-time clock with 64 bytes of SRAM
 *
 * The clock runs in sync with the host, offset by whatever time the guest
 * sets.  The registers from 0x00 to 0x1f wrap around to 0x00 and the SRAM
 * from 0x20 to 0x5f wraps around to 0x20, as on the real chip.  Not
 * emulated: the alarms, the power-fail time stamps and the trimming, which
 * read back what was written.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/rtc/mcp7940.h"
#include "migration/vmstate.h"
#include "qemu/bcd.h"
#include "qom/object.h"
#include "system/rtc.h"
#include "trace.h"

#define MCP7940_RTCSEC          0x00
#define MCP7940_RTCMIN          0x01
#define MCP7940_RTCHOUR         0x02
#define MCP7940_RTCWKDAY        0x03
#define MCP7940_RTCDATE         0x04
#define MCP7940_RTCMTH          0x05
#define MCP7940_RTCYEAR         0x06
#define MCP7940_CONTROL         0x07
#define MCP7940_REGS_END        0x20
#define MCP7940_SRAM_END        0x60

#define RTCSEC_ST               0x80    /* start the oscillator */
#define RTCHOUR_12H             0x40
#define RTCHOUR_PM              0x20
#define RTCWKDAY_OSCRUN         0x20
#define RTCWKDAY_PWRFAIL        0x10
#define RTCWKDAY_VBATEN         0x08
#define RTCMTH_LPYR             0x20
#define CONTROL_OUT             0x80

OBJECT_DECLARE_SIMPLE_TYPE(MCP7940State, MCP7940)

struct MCP7940State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t regs[MCP7940_SRAM_END];
    uint8_t ptr;
    bool addr_byte;
};

static const VMStateDescription vmstate_mcp7940 = {
    .name = "mcp7940",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, MCP7940State),
        VMSTATE_INT64(offset, MCP7940State),
        VMSTATE_UINT8(wday_offset, MCP7940State),
        VMSTATE_UINT8_ARRAY(regs, MCP7940State, MCP7940_SRAM_END),
        VMSTATE_UINT8(ptr, MCP7940State),
        VMSTATE_BOOL(addr_byte, MCP7940State),
        VMSTATE_END_OF_LIST()
    }
};

/* Latch the current time into the time keeping registers */
static void mcp7940_capture_time(MCP7940State *s)
{
    uint8_t *r = s->regs;
    struct tm now;
    int year;

    qemu_get_timedate(&now, s->offset);
    year = now.tm_year + 1900;

    r[MCP7940_RTCSEC] = (r[MCP7940_RTCSEC] & RTCSEC_ST) |
                        to_bcd(now.tm_sec);
    r[MCP7940_RTCMIN] = to_bcd(now.tm_min);
    if (r[MCP7940_RTCHOUR] & RTCHOUR_12H) {
        int hour = now.tm_hour % 12 ? now.tm_hour % 12 : 12;

        r[MCP7940_RTCHOUR] = RTCHOUR_12H | to_bcd(hour) |
                             (now.tm_hour >= 12 ? RTCHOUR_PM : 0);
    } else {
        r[MCP7940_RTCHOUR] = to_bcd(now.tm_hour);
    }
    /* The oscillator runs whenever software has started it */
    r[MCP7940_RTCWKDAY] = (r[MCP7940_RTCWKDAY] &
                           (RTCWKDAY_PWRFAIL | RTCWKDAY_VBATEN)) |
                          (r[MCP7940_RTCSEC] & RTCSEC_ST ?
                           RTCWKDAY_OSCRUN : 0) |
                          ((now.tm_wday + s->wday_offset) % 7 + 1);
    r[MCP7940_RTCDATE] = to_bcd(now.tm_mday);
    r[MCP7940_RTCMTH] = to_bcd(now.tm_mon + 1) |
                        ((year % 4 == 0 && year % 100) || year % 400 == 0 ?
                         RTCMTH_LPYR : 0);
    r[MCP7940_RTCYEAR] = to_bcd(now.tm_year % 100);
}

static void mcp7940_set_time(MCP7940State *s, uint8_t reg, uint8_t data)
{
    struct tm now;

    qemu_get_timedate(&now, s->offset);
    switch (reg) {
    case MCP7940_RTCSEC:
        now.tm_sec = from_bcd(data & 0x7f);
        s->regs[reg] = data & RTCSEC_ST;
        break;
    case MCP7940_RTCMIN:
        now.tm_min = from_bcd(data & 0x7f);
        break;
    case MCP7940_RTCHOUR:
        if (data & RTCHOUR_12H) {
            now.tm_hour = from_bcd(data & 0x1f) % 12 +
                          (data & RTCHOUR_PM ? 12 : 0);
        } else {
            now.tm_hour = from_bcd(data & 0x3f);
        }
        s->regs[reg] = data & RTCHOUR_12H;
        break;
    case MCP7940_RTCWKDAY:
        s->wday_offset = ((data & 7) - 1 - now.tm_wday + 7) % 7;
        s->regs[reg] = data & (RTCWKDAY_PWRFAIL | RTCWKDAY_VBATEN);
        break;
    case MCP7940_RTCDATE:
        now.tm_mday = from_bcd(data & 0x3f);
        break;
    case MCP7940_RTCMTH:
        now.tm_mon = from_bcd(data & 0x1f) - 1;
        break;
    case MCP7940_RTCYEAR:
        now.tm_year = from_bcd(data) + 100;
        break;
    }
    s->offset = qemu_timedate_diff(&now);
}

static void mcp7940_inc_ptr(MCP7940State *s)
{
    s->ptr++;
    if (s->ptr == MCP7940_REGS_END) {
        s->ptr = 0;
        mcp7940_capture_time(s);
    } else if (s->ptr == MCP7940_SRAM_END) {
        s->ptr = MCP7940_REGS_END;
    }
}

static int mcp7940_event(I2CSlave *i2c, enum i2c_event event)
{
    MCP7940State *s = MCP7940(i2c);

    switch (event) {
    case I2C_START_RECV:
        mcp7940_capture_time(s);
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t mcp7940_recv(I2CSlave *i2c)
{
    MCP7940State *s = MCP7940(i2c);
    uint8_t val = s->regs[s->ptr];

    trace_mcp7940_recv(s->ptr, val);
    mcp7940_inc_ptr(s);
    return val;
}

static int mcp7940_send(I2CSlave *i2c, uint8_t data)
{
    MCP7940State *s = MCP7940(i2c);

    if (s->addr_byte) {
        /* Addresses past the SRAM aren't implemented and read as 0 */
        s->ptr = data < MCP7940_SRAM_END ? data : 0;
        s->addr_byte = false;
        return 0;
    }
    trace_mcp7940_send(s->ptr, data);
    if (s->ptr <= MCP7940_RTCYEAR) {
        mcp7940_set_time(s, s->ptr, data);
    } else {
        s->regs[s->ptr] = data;
    }
    mcp7940_inc_ptr(s);
    return 0;
}

static void mcp7940_reset_hold(Object *obj, ResetType type)
{
    MCP7940State *s = MCP7940(obj);

    /*
     * As on a board whose battery kept the clock running: oscillator
     * started, battery backup enabled, 24-hour mode, host time
     */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[MCP7940_RTCSEC] = RTCSEC_ST;
    s->regs[MCP7940_RTCWKDAY] = RTCWKDAY_VBATEN;
    s->regs[MCP7940_CONTROL] = CONTROL_OUT;
    s->ptr = 0;
    s->addr_byte = false;
    mcp7940_capture_time(s);
}

static void mcp7940_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->event = mcp7940_event;
    k->recv = mcp7940_recv;
    k->send = mcp7940_send;
    rc->phases.hold = mcp7940_reset_hold;
    dc->desc = "MCP7940 I2C RTC with 64-byte SRAM";
    dc->vmsd = &vmstate_mcp7940;
}

static const TypeInfo mcp7940_types[] = {
    {
        .name          = TYPE_MCP7940,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(MCP7940State),
        .class_init    = mcp7940_class_init,
    },
};

DEFINE_TYPES(mcp7940_types)
