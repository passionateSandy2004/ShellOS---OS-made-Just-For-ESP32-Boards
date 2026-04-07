#ifndef CAM_DRIVER_H
#define CAM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─────────────────────────────────────────
   Camera init / deinit
   ───────────────────────────────────────── */
bool cam_driver_init(void);
void cam_driver_deinit(void);
bool cam_driver_is_ready(void);

/* ─────────────────────────────────────────
   Capture a JPEG frame into a malloc'd buffer.
   Caller must free() the buffer after use.
   Returns NULL on failure.
   ───────────────────────────────────────── */
typedef struct {
    uint8_t *data;
    size_t   len;
    uint32_t width;
    uint32_t height;
} cam_frame_t;

cam_frame_t *cam_capture(void);
void         cam_frame_free(cam_frame_t *frame);

/* ─────────────────────────────────────────
   Resolution names for display
   ───────────────────────────────────────── */
const char *cam_resolution_name(void);

#endif // CAM_DRIVER_H
