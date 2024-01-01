#include "ov2640.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "image.pio.h"
#include "ov2640_init.h"
#include "usb_descriptors.h"
#include <stdio.h>

#define OV2640_ADDR 0x30

struct ov2640_config *vconfig = NULL;

// #define PIN_PWND   -1  // Also called PWDN, or set to -1 and tie to GND

// struct ov2640_config config = {
//     .sccb = i2c_default,
//     .pin_sioc = PICO_DEFAULT_I2C_SCL_PIN,
//     .pin_siod = PICO_DEFAULT_I2C_SDA_PIN,
//     .pin_vsync = PIN_CAM_VSYNC,
//     .pin_y2_pio_base = PIN_CAM_Y2_PIO_BASE,

//     .pio = pio0,
//     .pio_sm = 0,
//     .dma_channel = 0,
//     .image_buf = image_buf,
//     .image_buf_size = sizeof(image_buf),
// };
static void
ov2640_reg_write(uint8_t reg, uint8_t value) {
    // printf("write reg: 0x%02x, value: 0x%02x\n", reg, value);
    i2c_write_blocking(vconfig->sccb, OV2640_ADDR, (uint8_t[]){reg, value}, 2, false);
    sleep_ms(1);
}

// v4l2-ctl --stream-mmap=0 --stream-count=1 --stream-to=test.jpg

static uint8_t ov2640_reg_read(uint8_t reg) {
    i2c_write_blocking(vconfig->sccb, OV2640_ADDR, &reg, 1, false);
    uint8_t value;
    i2c_read_blocking(vconfig->sccb, OV2640_ADDR, &value, 1, false);
    return value;
}

/* Select the nearest higher resolution for capture */
static const struct ov2640_win_size *ov2640_select_win(uint32_t width, uint32_t height) {
    int i, default_size = 7;
    for (i = 0; i < 8; i++) {
        if (ov2640_supported_win_sizes[i].width >= width &&
            ov2640_supported_win_sizes[i].height >= height)
            return &ov2640_supported_win_sizes[i];
    }
    return &ov2640_supported_win_sizes[default_size];
}

static void ov2640_regs_write(const OV2640_command *cmd) {
    while (1) {
        if (cmd->reg == 0xff && cmd->value == 0xff)
            break;
        ov2640_reg_write(cmd->reg, cmd->value);
        cmd++;
    }
}

void ov2640_set_params(struct ov2640_config *config) {
    ov2640_regs_write(ov2640_init_regs);
    ov2640_regs_write(ov2640_size_change_preamble_regs);
    const struct ov2640_win_size *win = ov2640_select_win(320, 240);
    ov2640_regs_write(win->regs);
    ov2640_regs_write(ov2640_format_change_preamble_regs);

    switch(config->pixformat){
        case PIXFORMAT_RGB565:
            ov2640_regs_write(ov2640_rgb565_be_regs); // Displays normally on ILI9341 TFT LCD screen.
            // ov2640_regs_write(ov2640_rgb565_le_regs);
            break;
        case PIXFORMAT_YUV422:
            ov2640_regs_write(ov2640_uyvy_regs); // Transmission to Linux system via UVC displays normally.
            // ov2640_regs_write(ov2640_yuyv_regs);
            break;
        case PIXFORMAT_JPEG:
            ov2640_regs_write(ov2640_settings_jpeg);
            break;
        default:
            ov2640_regs_write(ov2640_uyvy_regs);
            break;
    }
}

static bool ov2640_probe(struct ov2640_config *config)
{
    (void *)config;
    uint16_t reg = 0;
    ov2640_reg_write(BANK_SEL, BANK_SEL_SENS);
    ov2640_reg_write(COM7, 0x80); // soft reset
    sleep_ms(1000);
    reg = ov2640_reg_read(MIDH);
    reg <<= 8;
    reg |= ov2640_reg_read(MIDL);
    printf("Read vendor MID: 0x%0x \n", reg);
    reg = ov2640_reg_read(REG_PID);
    reg <<= 8;
    reg |= ov2640_reg_read(REG_VER);
    printf("Read vendor PID: 0x%0x \n", reg);
    return true;
}

void ov2640_init(struct ov2640_config *config) {
    vconfig = config;
    // SCCB I2C @ 100 kHz
    i2c_init(config->sccb, 100 * 1000);
    gpio_set_function(config->pin_sioc, GPIO_FUNC_I2C);
    gpio_set_function(config->pin_siod, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    // Initialise reset pin
    gpio_init(config->pin_resetb);
    gpio_set_dir(config->pin_resetb, GPIO_OUT);

    // Reset camera, and give it some time to wake back up
    gpio_put(config->pin_resetb, 0);
    sleep_ms(100);
    gpio_put(config->pin_resetb, 1);
    sleep_ms(100);
    ov2640_probe(config);
    ov2640_set_params(config);

    uint offset = pio_add_program(config->pio, &image_program);
    image_program_init(config->pio, config->pio_sm, offset, config->pin_y2_pio_base);
}

void ov2640_capture_frame(struct ov2640_config *config) {
    // config->dma_channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(config->dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(config->pio, config->pio_sm, false));

    dma_channel_configure(
        config->dma_channel, &c,
        config->image_buf,
        &config->pio->rxf[config->pio_sm],
        config->image_buf_size,
        false);

    // Wait for vsync rising edge to start frame
    while (gpio_get(config->pin_vsync) == true)
        ;
    while (gpio_get(config->pin_vsync) == false)
        ;

    dma_channel_start(config->dma_channel);
    dma_channel_wait_for_finish_blocking(config->dma_channel);

    dma_channel_abort(config->dma_channel);

#if 0
    irq_set_exclusive_handler(DMA_IRQ_0, dma_complete_handler);
    dma_channel_set_irq0_enabled(DMA_CHANNEL, true);
    irq_set_enabled(DMA_IRQ_0, true);
#endif
}
