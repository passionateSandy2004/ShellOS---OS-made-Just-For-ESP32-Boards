#include "cam_driver.h"
#include "esp_log.h"

static const char *TAG = "cam_driver";

bool cam_driver_init(void)
{
    ESP_LOGW(TAG, "Camera not available on this chip (OV2640 driver targets ESP32 / S2 / S3).");
    return false;
}

void cam_driver_deinit(void) {}

bool cam_driver_is_ready(void)
{
    return false;
}

cam_frame_t *cam_capture(void)
{
    return NULL;
}

void cam_frame_free(cam_frame_t *frame)
{
    (void)frame;
}

const char *cam_resolution_name(void)
{
    return "N/A";
}
