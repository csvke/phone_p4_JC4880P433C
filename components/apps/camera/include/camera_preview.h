/*
 * Camera Preview Application Header
 * ESP32-P4 with ESP-IDF 5.5.1 and latest APIs
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start camera preview with video display
 * 
 * Initializes camera sensor, video pipeline, and display preview.
 * Uses shared I2C bus from BSP for camera communication.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_preview_start(void);

/**
 * @brief Stop camera preview and cleanup resources
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_preview_stop(void);

/**
 * @brief Check if camera preview is currently running
 * 
 * @return true if running, false if stopped
 */
bool camera_preview_is_running(void);

/**
 * @brief Set parent object for camera preview canvas
 * 
 * @param parent LVGL object to use as parent for preview canvas (NULL for screen)
 */
void camera_preview_set_parent(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif