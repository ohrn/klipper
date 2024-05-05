// Main starting point for AVR boards.
//
// Copyright (C) 2016,2017  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_MCU
#define F_CPU CONFIG_CLOCK_FREQ
#include <util/delay.h>

#include <avr/io.h> // AVR_STACK_POINTER_REG
#include <util/crc16.h> // _crc_ccitt_update
#include "board/misc.h" // dynmem_start
#include "command.h" // DECL_CONSTANT
#include "irq.h" // irq_enable
#include "sched.h" // sched_main
#include "gpio.h"
#include "spi_software.h"

DECL_CONSTANT_STR("MCU", CONFIG_MCU);


/****************************************************************
 * Dynamic memory
 ****************************************************************/

// Return the start of memory available for dynamic allocations
void *
dynmem_start(void)
{
    extern char _end;
    return &_end;
}

// Return the end of memory available for dynamic allocations
void *
dynmem_end(void)
{
    return (void*)ALIGN(AVR_STACK_POINTER_REG, 256) - CONFIG_AVR_STACK_SIZE;
}


/****************************************************************
 * Misc functions
 ****************************************************************/

// Initialize the clock prescaler (if necessary)
void
prescaler_init(void)
{
    if (CONFIG_AVR_CLKPR != -1 && (uint8_t)CONFIG_AVR_CLKPR != CLKPR) {
        irqstatus_t flag = irq_save();
        CLKPR = 0x80;
        CLKPR = CONFIG_AVR_CLKPR;
        irq_restore(flag);
    }
}
DECL_INIT(prescaler_init);

// Optimized crc16_ccitt for the avr processor
uint16_t
crc16_ccitt(uint8_t *buf, uint_fast8_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--)
        crc = _crc_ccitt_update(crc, *buf++);
    return crc;
}

/****************************************************************
 * Boot message
 ****************************************************************/

struct spi_software {
    struct gpio_in miso;
    struct gpio_out mosi, sclk;
    uint8_t mode;
};

struct gpio_out strobe;
extern char *firmware_version;

void serial_out(struct spi_software *ss, uint8_t value) {
    gpio_out_write(strobe, 0);
    gpio_out_write(ss->sclk, 0);
    spi_software_transfer(ss, 0, 1, &value);
    gpio_out_write(strobe, 1);
}

void send_nibble(struct spi_software *ss, uint8_t value) {
    serial_out(ss, value | 8);
    serial_out(ss, value);
}

void send_byte(struct spi_software *ss, uint8_t mask, uint8_t value) {
    send_nibble(ss, (value & 0xf0) | mask);
    send_nibble(ss, ((value << 4) & 0xf0) | mask);
    _delay_us(37);
}

void boot_message(void) {
    struct spi_software ss;

    uint8_t setup[] = { 0x28, 0x06, 0x0c, 0x01 };
    char *text = "ZYYX klipper startup";

    strobe = gpio_out_setup(20, 0);
    ss.miso = gpio_in_setup(73, 1);
    ss.mosi = gpio_out_setup(19, 0);
    ss.sclk = gpio_out_setup(18, 0);
    ss.mode = 0;

    serial_out(&ss, 0);

    send_nibble(&ss, 0x30);
    _delay_us(4500);

    send_nibble(&ss, 0x30);
    _delay_us(150);

    send_nibble(&ss, 0x30);
    send_nibble(&ss, 0x20);

    for (int i = 0; i < 4; i ++)
        send_byte(&ss, 0, setup[i]);

    _delay_us(2000);

    send_byte(&ss, 0, 0x80);

    for (int i = 0; i < 20; i ++)
        send_byte(&ss, 2, text[i]);

    send_byte(&ss, 0, 0x80 + 20);

    for (int i = 0; (firmware_version[i] != 0) && (i < 20); i ++)
        send_byte(&ss, 2, firmware_version[i]);
}

// Main entry point for avr code.
int
main(void)
{
    irq_enable();
    boot_message();
    sched_main();
    return 0;
}
