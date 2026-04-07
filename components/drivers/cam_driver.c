#include "cam_driver.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cam_driver";
static bool s_cam_ready = false;

/* ─────────────────────────────────────────
   AI Thinker ESP32-CAM pin map
   ───────────────────────────────────────── */
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

/* ─────────────────────────────────────────
   Init camera
   ───────────────────────────────────────── */
bool cam_driver_init(void)
{
    if (s_cam_ready) return true;

    /*
     * XCLK is LEDC on GPIO 0 (AI Thinker). Timer/channel 0 is often already in
     * use elsewhere, which triggers "GPIO 0 is not usable" and leaves the
     * sensor without a clock — SCCB then probes as timeout / NOT_SUPPORTED.
     */
    if (CAM_PIN_XCLK >= 0) {
        gpio_reset_pin((gpio_num_t)CAM_PIN_XCLK);
    }

    camera_config_t config = { 0 };
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_pclk     = CAM_PIN_PCLK;

    config.xclk_freq_hz = 20000000;
    config.ledc_timer   = LEDC_TIMER_2;
    config.ledc_channel = LEDC_CHANNEL_2;

    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_VGA;   /* 640x480 */
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return false;
    }

    s_cam_ready = true;
    return true;
}

void cam_driver_deinit(void)
{
    if (s_cam_ready) {
        esp_camera_deinit();
        s_cam_ready = false;
    }
}

bool cam_driver_is_ready(void)
{
    return s_cam_ready;
}

/* ─────────────────────────────────────────
   Capture one JPEG frame
   Returns heap-allocated cam_frame_t
   ───────────────────────────────────────── */
cam_frame_t *cam_capture(void)
{
    if (!s_cam_ready) return NULL;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return NULL;

    cam_frame_t *frame = malloc(sizeof(cam_frame_t));
    if (!frame) {
        esp_camera_fb_return(fb);
        return NULL;
    }

    frame->data = malloc(fb->len);
    if (!frame->data) {
        free(frame);
        esp_camera_fb_return(fb);
        return NULL;
    }

    memcpy(frame->data, fb->buf, fb->len);
    frame->len    = fb->len;
    frame->width  = fb->width;
    frame->height = fb->height;

    esp_camera_fb_return(fb);
    return frame;
}

void cam_frame_free(cam_frame_t *frame)
{
    if (frame) {
        free(frame->data);
        free(frame);
    }
}

const char *cam_resolution_name(void)
{
    return "VGA (640x480)";
}
