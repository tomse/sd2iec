/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2011  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   ds1307-3231.c: RTC support for DS1307/DS3231 chips

   This file implements the functions defined in rtc.h.

*/

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "i2c.h"
#include "progmem.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"
#include "time.h"
#include "rtc.h"

#if CONFIG_RTC_VARIANT == 4
#  define RTC_IS_3231
#elif CONFIG_RTC_VARIANT == 5
#  define RTC_IS_1307
#else
#  error CONFIG_RTC_VARIANT is set to an unknown value!
#endif

#define RTC_ADDR 0xd0

#define REG_SECOND      0
#define REG_MINUTE      1
#define REG_HOUR        2
#define REG_DOW         3
#define REG_DOM         4
#define REG_MONTH       5
#define REG_YEAR        6

#ifdef RTC_IS_3231
#  define REG_AL1_SECOND  7
#  define REG_AL1_MINUTE  8
#  define REG_AL1_HOUR    9
#  define REG_AL1_DAY    10
#  define REG_AL2_MINUTE 11
#  define REG_AL2_HOUR   12
#  define REG_AL2_DAY    13
#  define REG_CONTROL    14
#  define REG_CTLSTATUS  15
#  define REG_AGING      16
#  define REG_TEMP_MSB   17
#  define REG_TEMP_LSB   18
#else
#  define REG_CONTROL    7
#endif

#define STATUS_OSF     0x80  // oscillator stopped (1307: CH bit in reg 0)

/* Default date/time if the RTC isn't preset or not set: 2011-01-01 00:00:00 */
static const PROGMEM struct tm defaultdate = {
  0, 0, 0, 1, 1, 11, 5
};

rtcstate_t rtc_state;

/* Read the current time from the RTC */
void read_rtc(struct tm *time) {
  uint8_t tmp[7];

  /* Set to default value in case we abort */
  memcpy_P(time, &defaultdate, sizeof(struct tm));
  if (rtc_state != RTC_OK)
    return;

  if (i2c_read_registers(RTC_ADDR, REG_SECOND, 7, &tmp))
    return;

  time->tm_sec  = bcd2int(tmp[REG_SECOND] & 0x7f);
  time->tm_min  = bcd2int(tmp[REG_MINUTE]);
  time->tm_hour = bcd2int(tmp[REG_HOUR]);
  time->tm_mday = bcd2int(tmp[REG_DOM]);
  time->tm_mon  = bcd2int(tmp[REG_MONTH] & 0x7f) - 1;
  time->tm_wday = bcd2int(tmp[REG_DOW]) - 1;
  time->tm_year = bcd2int(tmp[REG_YEAR]) + 100 * !!(tmp[REG_MONTH] & 0x80) + 100;
  // FIXME: Leap year calculation is wrong in 2100
}

/* Set the time in the RTC */
void set_rtc(struct tm *time) {
  uint8_t tmp[7];

  if (rtc_state == RTC_NOT_FOUND)
    return;

  tmp[REG_SECOND] = int2bcd(time->tm_sec);
  tmp[REG_MINUTE] = int2bcd(time->tm_min);
  tmp[REG_HOUR]   = int2bcd(time->tm_hour);
  tmp[REG_DOW]    = int2bcd(time->tm_wday+1);
  tmp[REG_DOM]    = int2bcd(time->tm_mday);
  tmp[REG_MONTH]  = int2bcd(time->tm_mon+1) | 0x80 * (time->tm_year >= 2100);
  tmp[REG_YEAR]   = int2bcd(time->tm_year % 100);
  i2c_write_registers(RTC_ADDR, REG_SECOND, 7, tmp);
  i2c_write_register(RTC_ADDR, REG_CONTROL, 0);   // 3231: enable oscillator on battery, interrupts off
                                                  // 1307: disable SQW output
#ifdef RTC_IS_3231
  i2c_write_register(RTC_ADDR, REG_CTLSTATUS, 0); // clear "oscillator stopped" flag
#endif
  rtc_state = RTC_OK;
}

#ifdef RTC_IS_3231
/* DS3231 version, checks oscillator stop flag in status register */
void rtc_init(void) {
  int16_t tmp;

  rtc_state = RTC_NOT_FOUND;

  uart_puts_P(PSTR("DS3231 "));
  tmp = i2c_read_register(RTC_ADDR, REG_CTLSTATUS);
  if (tmp < 0) {
    uart_puts_P(PSTR("not found"));
  } else {
    if (tmp & STATUS_OSF) {
      rtc_state = RTC_INVALID;
      uart_puts_P(PSTR("invalid"));
    } else {
      rtc_state = RTC_OK;
      uart_puts_P(PSTR("ok"));
    }
  }
  uart_putcrlf();
}
#else
/* DS1307 version, checks clock halt bit in seconds register */
void rtc_init(void) {
  int16_t tmp;

  rtc_state = RTC_NOT_FOUND;
  uart_puts_P(PSTR("DS1307 "));
  tmp = i2c_read_register(RTC_ADDR, REG_SECOND);
  if (tmp < 0) {
    uart_puts_P(PSTR("not found"));
  } else {
    if (tmp & STATUS_OSF) {
      rtc_state = RTC_INVALID;
      uart_puts_P(PSTR("invalid"));
    } else {
      rtc_state = RTC_OK;
      uart_puts_P(PSTR("ok"));
    }
  }
  uart_putcrlf();
}
#endif