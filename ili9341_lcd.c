/*
 * @Author: Liu Chun Yang
 * @Date: 2023-12-14 00:11:15
 * @Last Modified by: Liu Chun Yang
 * @Last Modified time: 2023-12-14 00:39:13
 */
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "pico/stdlib.h"
#include "yuv.h"

#define USE_BIT_BANGING 0
#if USE_BIT_BANGING == 0
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "ili9341_lcd.pio.h"
#else
#define LSBFIRST 0
#define MSBFIRST 1

uint8_t bitOrder = MSBFIRST;
#endif


// ili9341  common registers
#define CASET               0x2A
#define PASET               0x2B
#define RAMWR               0x2C

#define SCREEN_WIDTH        240
#define SCREEN_HEIGHT       320

// #define PIN_LED           -1 // LCD black screen, connect to 3v3
#define PIN_DOUT            20
#define PIN_CLK             21
#define PIN_RS              19
#define PIN_RESET           18

#define SERIAL_CLK_DIV      1.f

// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
// Note the delays have been shortened a little

static const uint8_t ili9341_init_seq[] = {
    1, 10, 0x01,                                                      // Software reset
    1, 5, 0x11,                                                       // Exit sleep mode
    4, 0, 0xEF, 0x03, 0x80, 0x02,
    5, 0, 0xED, 0x64, 0x03, 0x12, 0x81,                               // Power on sequence control
    4, 0, 0xCF, 0x00, 0xc1, 0x30,                                     // Power control B
    4, 0, 0xE8, 0x85, 0x00, 0x78,                                     // Driver timing control A
    2, 2, 0xF7, 0x20,                                                 // Pump ratio control, DDVDH=2xVCl
    3, 0, 0xEA, 0x00, 0x00,                                           // Driver timing control B
    2, 0, 0xC0, 0x23,                                                 // Power Control 1
    2, 0, 0xC1, 0x10,                                                 // Power Control 2
    3, 0, 0xC5, 0x3e, 0x28,                                           // VCOM Control 1
    2, 0, 0xC7, 0x86,                                                 // VCOM Control 2
    2, 2, 0x3A, 0x55,                                                 // Set colour mode to 16 bit
    2, 0, 0x36, 0x68,                                                 // Set MADCTL: row then column, 0x78,0x68, 0x28 will work for ov2640 rgb565
    5, 0, 0x2A, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xFF,   // CASET: column addresses
    5, 0, 0x2B, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xFF, // RASET: row addresses
    3, 2, 0xB1, 0x00, 0x10,                                           // Frame Rate Control (In Normal Mode/Full Colors) 119Hz
    //3, 2, 0xB2, 0x00, 0x10,                                         // Frame Rate Control (In Idle Mode/8 colors) 119Hz
    3, 2, 0xB3, 0x00, 0x10,                                           // Frame Rate control (In Partial Mode/Full Colors) 119Hz
    4, 2, 0xB6, 0x00, 0x82, 0x27,                                     // Display Function Control
    2, 2, 0xF2, 0x00,
    2, 0, 0x26, 0x01,
    16, 0, 0xE0, 0x1F, 0x36, 0x36, 0x3A, 0x0C, 0x05, 0x4F, 0X87, 0x3C, 0x08, 0x11, 0x35, 0x19, 0x13, 0x00,
    16, 0, 0xE1, 0x00, 0x09, 0x09, 0x05, 0x13, 0x0A, 0x30, 0x78, 0x43, 0x07, 0x0E, 0x0A, 0x26, 0x2C, 0x1F,
    1, 0, 0x13,                                                        // Normal display on, then 10 ms delay
    1, 0, 0x29,                                                        // Main screen turn on, then wait 500 ms
    0                                                                  // Terminate list
};

#if USE_BIT_BANGING == 0
PIO tft_pio;
int8_t pio_sm = 1; // camera use 0 ,so tft lcd use 1
pio_sm_config sm_conf;

// Updated later with the loading offset of the PIO program.
void pioinit(uint32_t clock_freq) {

    // Find enough free space on one of the PIO's
    tft_pio = pio1;
    // Load the PIO program
    uint32_t program_offset = pio_add_program(tft_pio, &ili9341_lcd_program);
    // Configure the state machine
    sm_conf = ili9341_lcd_program_get_default_config(program_offset);
    ili9341_lcd_program_init(tft_pio, pio_sm, program_offset, PIN_DOUT, PIN_CLK, (float)clock_freq);
    printf("initial ili9341 with PIO\n");
}
#endif

static inline void shiftout( uint16_t val,uint8_t bits) {

#if USE_BIT_BANGING == 0
    if(bits == 8)
    {
        ili9341_lcd_wait_idle(tft_pio,pio_sm);
        ili9341_lcd_put(tft_pio,pio_sm,(uint8_t)(val & 0xff));
        ili9341_lcd_wait_idle(tft_pio, pio_sm);
    } else {
        ili9341_lcd_put(tft_pio, pio_sm, (uint8_t)(val >> 8));
        ili9341_lcd_put(tft_pio, pio_sm, (uint8_t)(val & 0xff));
        // *(volatile uint8_t *)&tft_pio->txf[pio_sm] = (val >> 8);
        // *(volatile uint8_t *)&tft_pio->txf[pio_sm] =  val & 0xff;
    }

#else
    uint8_t i;
    uint8_t max_len = bits - 1;
    for (i = 0; i < bits; i++) {
        if (bitOrder == MSBFIRST)
        {
            gpio_put(PIN_DOUT, (val & (1 << (max_len - i))) >> (max_len - i));
        } else {
            gpio_put(PIN_DOUT, (val & (1 << i)) >> i);
        }
        gpio_put(PIN_CLK, 1);
        gpio_put(PIN_CLK, 0);
    }
#endif
}

static inline void lcd_send_cmd(const uint8_t cmd) {
    gpio_put(PIN_RS, 0);
    shiftout(cmd, 8);
    gpio_put(PIN_RS, 1);
}

static inline void lcd_send_data(const uint8_t data) {
    gpio_put(PIN_RS, 1);
    shiftout(data, 8);
}

#if USE_BIT_BANGING == 1 && defined(USE_74HC165)

static inline uint8_t lcd_74hcxxx_test(uint8_t data) {
    uint8_t value = 0;
    uint8_t i;

    gpio_put(PIN_LATCH, 0);
    for (i = 0; i < 8; ++i) {
        if (bitOrder == MSBFIRST) {
            gpio_put(PIN_DOUT, (data & (1 << (7 - i))) >> (7 - i));
        } else {
            gpio_put(PIN_DOUT, (data & (1 << i)) >> i);
        }
        gpio_put(PIN_CLK, 1);
        gpio_put(PIN_CLK, 0);
    }
    gpio_put(PIN_LATCH, 1);
    gpio_put(PIN_PL, 0);
    sleep_us(1);       // MCU freq too fast need delay.
    gpio_put(PIN_PL, 1);
    sleep_us(1);
    for (i = 0; i < 8; ++i) {
        gpio_put(PIN_CP, 0);
        if (bitOrder == LSBFIRST)
            value |= gpio_get(PIN_DIN) << i;
        else
            value |= gpio_get(PIN_DIN) << (7 - i);
        gpio_put(PIN_CP, 1);
        sleep_us(1);
    }
    static char text[64];
    sprintf(text, "TEST Read 74hcxxx , 0x%04x\r\n", value);
    uart_puts(UART_ID, text);
    return value;
}

static inline uint8_t shiftin() {
    uint8_t value = 0;
    uint8_t i;
    gpio_put(PIN_RD, 0);
    gpio_put(PIN_PL, 0);
    sleep_us(1);
    gpio_put(PIN_PL, 1);
    gpio_put(PIN_RD, 1);
    for (i = 0; i < 8; ++i) {
        gpio_put(PIN_CP, 0);
        if (bitOrder == LSBFIRST)
            value |= gpio_get(PIN_DIN) << i;
        else
            value |= gpio_get(PIN_DIN) << (7 - i);
        gpio_put(PIN_CP, 1);
        sleep_us(1);
    }
    return value;
}

static inline uint32_t lcd_read_device_id() {
    uint32_t id = 0;
    lcd_send_cmd(READ_ID4);
    gpio_put(PIN_WR, 1);
    shiftin();       // dummy data
    shiftin();       // 0x00
    id = shiftin();  // 0x93
    id <<= 8;
    id |= shiftin(); // 0x41
    return id;
}
#endif

static inline void lcd_write_cmd(const uint8_t *cmd, size_t count) {
    lcd_send_cmd(*cmd++);
    if (count >= 2) {
        for (size_t i = 0; i < count - 1; ++i)
        {
            lcd_send_data(*cmd++);
        }
    }
}

static inline void lcd_init(const uint8_t *init_seq) {
    const uint8_t *cmd = init_seq;
    while (*cmd) {
        lcd_write_cmd( cmd + 2, *cmd);
        sleep_ms(*(cmd + 1) * 5);
        cmd += *cmd + 2;
    }
}

static void ili9341_openwindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    lcd_send_cmd(CASET);
    lcd_send_data(x1 >> 8);
    lcd_send_data(x1 & 0xFF);
    lcd_send_data((x1 + x2 - 1) >> 8);
    lcd_send_data((x1 + x2 - 1) & 0xFF);

    lcd_send_cmd(PASET);
    lcd_send_data(y1 >> 8);
    lcd_send_data(y1 & 0xFF);
    lcd_send_data((y1 + y2 - 1) >> 8);
    lcd_send_data((y1 + y2 - 1) & 0xFF);
    sleep_us(100);
    lcd_send_cmd(RAMWR);
}

void ili9341_show_rgb565_data(uint16_t *data, int len) {
    for (int i = 0; i < len; i++) {
        shiftout(*data++, 16);
    }
}

uint16_t yuv422_to_rgb565(int y, int u, int v) {
    uint16_t rgb = 0;
    VP8YuvToRgb565(y,u,v,(uint8_t *)&rgb);
    *(volatile uint8_t *)&tft_pio->txf[pio_sm] = (rgb >> 8);
    *(volatile uint8_t *)&tft_pio->txf[pio_sm] = rgb & 0xff;
    return rgb;
}

void rgb565_to_yuv422(uint32_t *data, int len) {
    //  FIXME: This conversion will lose a lot of color, approaching a grayscale display.

    for (int i = 0; i < len; i++, data++) {
        uint16_t first = *data & 0xffff;
        uint16_t second = (*data >> 16) & 0xffff;
        uint8_t *dst = (uint8_t *)data;

#if 0
        uint8_t r1,r2,g1,g2,b1,b2;
        r1 = ((first & (0x1f << 11)) >> 8);
        g1 = ((first & (0x3f << 5)) >> 3);
        b1 = ((first & (0x1f << 0)) << 3);
        dst[0] = VP8RGBToY(r1, g1, b1, YUV_HALF);

        r2 = ((second & (0x1f << 11)) >> 8);
        g2 = ((second & (0x3f << 5)) >> 3);
        b2 = ((second & (0x1f << 0)) << 3);
        dst[2] = VP8RGBToY(r2, g2, b2, YUV_HALF);

        dst[1] = VP8RGBToU(r1 + r2, g1 + g2, b1 + b2, YUV_HALF << 2);
        dst[3] = VP8RGBToV(r1 + r2, g1 + g2, b1 + b2, YUV_HALF << 2);
#else
        uint8_t rgb1[3], rgb2[3];
        color16to24(first, rgb1);
        color16to24(second, rgb2);
        dst[0] = VP8RGBToY(rgb1[0], rgb1[1], rgb1[2], YUV_HALF);
        dst[2] = VP8RGBToY(rgb2[0], rgb2[1], rgb2[2], YUV_HALF);

        dst[1] = VP8RGBToU(rgb1[0] + rgb2[0], rgb1[1] + rgb2[1], rgb1[2] + rgb2[2], YUV_HALF << 2);
        dst[3] = VP8RGBToV(rgb1[0] + rgb2[0], rgb1[1] + rgb2[1], rgb1[2] + rgb2[2], YUV_HALF << 2);
#endif
    }
}

void ili9341_show_yuv422_data(uint32_t *data, int len) {
    int y1, y2, u, v;
    uint16_t pixels = 0;

    // Extract yuv components
    for (int i = 0; i < len; i++) {
#if 1
        y1 = *data & 0xff;
        u = (*data >> 8) & 0xff;
        y2 = (*data >> 16) & 0xff;
        v = (*data++ >> 24) & 0xff;
#else
        u = raw & 0xff;
        y1 = (raw >> 8) & 0xff;
        v = (raw >> 16) & 0xff;
        y2 = (raw >> 24) & 0xff;
#endif
        yuv422_to_rgb565(y1, u, v);
        yuv422_to_rgb565(y2, u, v);
    }
}

int main_lcd_init() {

#if USE_BIT_BANGING == 0
    pioinit(128000000);
#else
    gpio_init(PIN_DOUT);
    gpio_init(PIN_CLK);
    gpio_set_dir(PIN_DOUT, GPIO_OUT);
    gpio_set_dir(PIN_CLK, GPIO_OUT);
#endif

    gpio_init(PIN_RS);
    gpio_init(PIN_RESET);

    gpio_set_dir(PIN_RS, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);

    gpio_put(PIN_RS, 1);
    gpio_put(PIN_RESET, 1);

    lcd_init(ili9341_init_seq);
    ili9341_openwindow(0, 0, SCREEN_HEIGHT, SCREEN_WIDTH);
    return 0;
}
