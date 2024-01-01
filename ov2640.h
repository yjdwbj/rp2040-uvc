#ifndef OV2640_H
#define OV2640_H
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ov2640_init.h"
#include <stdint.h>

struct ov2640_config {
    i2c_inst_t *sccb;
    uint pin_sioc;
    uint pin_siod;

    uint pin_resetb;
    uint pin_xclk;
    uint pin_vsync;
    // Y2, Y3, Y4, Y5, Y6, Y7, Y8, PCLK, HREF
    uint pin_y2_pio_base;

    PIO pio;
    uint pio_sm;

    uint dma_channel;
    uint8_t *image_buf;
    size_t image_buf_size;
    pixformat_t pixformat;
};

void ov2640_init(struct ov2640_config *config);

void ov2640_capture_frame(struct ov2640_config *config);

void OV2640_JPEG_Mode(void);
void OV2640_RGB565_Mode(void);
void OV2640_Auto_Exposure(uint8_t level);
void OV2640_Light_Mode(uint8_t mode);
void OV2640_Color_Saturation(uint8_t sat);
void OV2640_Brightness(uint8_t bright);
void OV2640_Contrast(uint8_t contrast);
void OV2640_Special_Effects(uint8_t eft);
void OV2640_Color_Bar(uint8_t sw);
void OV2640_Window_Set(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);
uint8_t OV2640_OutSize_Set(uint16_t width, uint16_t height);
uint8_t OV2640_ImageWin_Set(uint16_t offx, uint16_t offy, uint16_t width, uint16_t height);
uint8_t OV2640_ImageSize_Set(uint16_t width, uint16_t height);

#endif