/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include "bsp/board_api.h"
// #define USE_FREERTOS
#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#endif

#include "hardware/dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #include "bsp/board.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "ov2640.h"

// refs https://blog.usedbytes.com/2022/02/pico-pio-camera/
//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum {
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const int PIN_LED = 25;

const int PIN_CAM_RESETB = 2;
// const int PIN_CAM_XCLK = 3;
const int PIN_CAM_VSYNC = 3;
const int PIN_CAM_Y2_PIO_BASE = 6;

const uint8_t CMD_REG_WRITE = 0xAA;
const uint8_t CMD_REG_READ = 0xBB;
const uint8_t CMD_CAPTURE = 0xCC;

static uint8_t image_buf[FRAME_WIDTH * FRAME_HEIGHT * 2];
extern void ili9341_show_rgb565_data(uint16_t *data, int len);
extern void ili9341_show_yuv422_data(uint32_t *data, int len);
extern void rgb565_to_yuv422(uint32_t *data, int len);
extern int main_lcd_init();

void led_blinking_task(void);
void video_task(void);

static struct ov2640_config config = {
    .sccb = i2c_default,
    .pin_sioc = PICO_DEFAULT_I2C_SCL_PIN,
    .pin_siod = PICO_DEFAULT_I2C_SDA_PIN,

    .pin_resetb = PIN_CAM_RESETB,
    .pin_vsync = PIN_CAM_VSYNC,
    .pin_y2_pio_base = PIN_CAM_Y2_PIO_BASE,

    .pio = pio0,
    .pio_sm = 0,
    .dma_channel = 0,
    .image_buf = image_buf,
    .image_buf_size = sizeof(image_buf),
    .pixformat = PIXFORMAT_RGB565,
    // .pixformat = PIXFORMAT_YUV422,  // FIXME: have to green/inverted block.
};

#define PLL_SYS_KHZ (133 * 1000)

#ifdef USE_FREERTOS
#define THREADED 1
TickType_t last_wake, interval = 100;
TimerHandle_t blinky_tm;

#define TUD_TASK_PRIO (tskIDLE_PRIORITY + 2)
#define CAM_TASK_PRIO (tskIDLE_PRIORITY + 1)
TaskHandle_t cam_taskhandle, tud_taskhandle, video_taskhandle;


void usb_thread(void *ptr) {
    TickType_t wake;
    wake = xTaskGetTickCount();
    do {
        tud_task();
        if (!tud_task_event_ready())
            xTaskDelayUntil(&wake, 1);
    } while (1);
}
#endif


/*------------- MAIN -------------*/
int main(void) {
    set_sys_clock_khz(PLL_SYS_KHZ, true);
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    tusb_init();
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    ov2640_init(&config);
    main_lcd_init();

#ifdef USE_FREERTOS
    printf("Running on FreeRTOS\n");
    if (THREADED) {
        blinky_tm = xTimerCreate(NULL, pdMS_TO_TICKS(BLINK_MOUNTED), true, NULL, led_blinking_task);
        xTaskCreate(usb_thread, "USB", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &tud_taskhandle);
        xTaskCreate(video_task, "CAMERA", configMINIMAL_STACK_SIZE, NULL, CAM_TASK_PRIO, &cam_taskhandle);
        // xTaskCreate(video_task, "CAMERA", configMINIMAL_STACK_SIZE, NULL, CAM_TASK_PRIO, &cam_taskhandle);
        xTimerStart(blinky_tm, 0);
        vTaskStartScheduler();
    }
#else
    printf("Start main loop\n");
    while (1) {
        tud_task(); // tinyusb device task
        led_blinking_task();
        video_task();
    }
#endif


}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB Video
//--------------------------------------------------------------------+
static unsigned frame_num = 0;
static unsigned tx_busy = 0;
static unsigned interval_ms = 1000 / FRAME_RATE;

static const uint32_t JPEG_SOI_MARKER = 0xFFD8FF; // written in little-endian for esp32
static const uint16_t JPEG_EOI_MARKER = 0xD9FF;   // written in little-endian for esp32

static int cam_verify_jpeg_soi(const uint8_t *inbuf, int length) {
    for (int i = 0; i < (int )length; i++) {
        if (memcmp(&inbuf[i], &JPEG_SOI_MARKER, 3) == 0) {
            return i;
        }
    }
    printf("NO-SOI,%d %d %d %d\n", inbuf[0], inbuf[1], inbuf[2], inbuf[3]);
    return -1;
}

static int cam_verify_jpeg_eoi(const uint8_t *inbuf, int length) {
    int offset = -1;
    const uint8_t *dptr = inbuf + length - 2;
    while (dptr > inbuf) {
        if (memcmp(dptr, &JPEG_EOI_MARKER, 2) == 0) {
            offset = dptr - inbuf;
            return offset;
        }
        dptr--;
    }
    printf("NO-EOI, %d %d %d %d\n", inbuf[length - 4], inbuf[length - 3], inbuf[length - 2], inbuf[length - 1]);
    return -1;
}

void video_task(void) {

#ifdef USE_FREERTOS
    do {
        vTaskDelay(pdMS_TO_TICKS(100));
        ov2640_capture_frame(&config);

        if (config.pixformat == PIXFORMAT_JPEG) {
            cam_verify_jpeg_eoi(config.image_buf, (int)config.image_buf_size);
            cam_verify_jpeg_soi(config.image_buf, (int)config.image_buf_size);
        }
        if (config.pixformat == PIXFORMAT_RGB565) {
            ili9341_show_rgb565_data((void *)config.image_buf, (int)(config.image_buf_size / 2));
        } else if (config.pixformat == PIXFORMAT_YUV422) {
            ili9341_show_yuv422_data((void *)config.image_buf, (int)(config.image_buf_size / 4));
        }
        vTaskSuspendAll();
        if (tud_video_n_streaming(0, 0)) {
            if (config.pixformat == PIXFORMAT_RGB565) {
                rgb565_to_yuv422((void *)config.image_buf, (int)(config.image_buf_size / 4));
            }

            tud_video_n_frame_xfer(0, 0, (void *)config.image_buf, config.image_buf_size);
        }
        xTaskResumeAll();
    } while (1);
#else
    static unsigned start_ms = 0;
    static unsigned already_sent = 0;
    if (!tud_video_n_streaming(0, 0)) {
        already_sent = 0;
        frame_num = 0;
        return;
    }
    if(config.pixformat == PIXFORMAT_JPEG)
    {
        cam_verify_jpeg_eoi(config.image_buf, (int)config.image_buf_size);
        cam_verify_jpeg_soi(config.image_buf, (int)config.image_buf_size);
    }
    if (!already_sent) {
        already_sent = 1;
        tx_busy = 1;
        start_ms = board_millis();
        ov2640_capture_frame(&config);
        if (config.pixformat == PIXFORMAT_RGB565) {
            ili9341_show_rgb565_data((void *)config.image_buf, (int)(config.image_buf_size / 2));
            rgb565_to_yuv422((void *)config.image_buf, (int)(config.image_buf_size / 4));
        } else if (config.pixformat == PIXFORMAT_YUV422) {
            ili9341_show_yuv422_data((void *)config.image_buf, (int)(config.image_buf_size / 4));
        }

        tud_video_n_frame_xfer(0, 0, (void *)config.image_buf, config.image_buf_size );
        return;
    }

    unsigned cur = board_millis();
    if (cur - start_ms < interval_ms)
        return; // not enough time
    if (tx_busy)
        return;
    tx_busy = 1;

    start_ms += interval_ms;
    ov2640_capture_frame(&config);
    if (config.pixformat == PIXFORMAT_RGB565) {
        ili9341_show_rgb565_data((void *)config.image_buf, (int)(config.image_buf_size / 2));

    } else if (config.pixformat == PIXFORMAT_YUV422) {
        ili9341_show_yuv422_data((void *)config.image_buf, (int)(config.image_buf_size / 4));
    }

    tud_video_n_frame_xfer(0, 0, (void *)config.image_buf, config.image_buf_size);
#endif
}

void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx) {
    (void)ctl_idx;
    (void)stm_idx;
    tx_busy = 0;
    /* flip buffer */
    ++frame_num;
}

int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                        video_probe_and_commit_control_t const *parameters) {
    (void)ctl_idx;
    (void)stm_idx;
    /* convert unit to ms from 100 ns */
    interval_ms = parameters->dwFrameInterval / 10000;

    return VIDEO_ERROR_NONE;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {
    static bool led_state = false;
#ifdef USE_FREERTOS
    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
#else
    static uint32_t start_ms = 0;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms)
        return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
#endif
}

#ifdef USE_FREERTOS
void vApplicationTickHook(void){};

void vApplicationStackOverflowHook(TaskHandle_t Task, char *pcTaskName) {
    panic("stack overflow (not the helpful kind) for %s\n", *pcTaskName);
}

void vApplicationMallocFailedHook(void) {
    panic("Malloc Failed\n");
};
#endif
