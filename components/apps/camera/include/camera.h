/*
 * Camera Application Header
 * ESP32-P4 with ESP-IDF 5.5.1 Camera Controller Driver API
 * 
 * Uses:
 * - esp_cam_ctlr (Camera Controller Driver for MIPI CSI)
 * - driver/isp (ISP Processor for image processing)
 * - LVGL canvas for display integration with Brookesia
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start camera preview with ISP processing
 * 
 * Initializes:
 * - CSI camera controller for MIPI CSI interface
 * - ISP processor for image processing (demosaic, AWB, AE, etc.)
 * - LVGL canvas for display integration
 * 
 * Uses shared I2C bus from BSP for camera sensor communication.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_start(void);

/**
 * @brief Stop camera preview and cleanup resources
 * 
 * Stops camera controller, disables ISP, and cleans up resources.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_stop(void);

/**
 * @brief Check if camera preview is currently running
 * 
 * @return true if running, false if stopped
 */
bool camera_is_running(void);

/**
 * @brief Set parent object for camera preview canvas
 * 
 * Must be called before camera_start() if you want to
 * display the preview in a specific container.
 * 
 * @param parent LVGL object to use as parent for preview canvas (NULL for screen)
 */
void camera_set_parent(lv_obj_t *parent);

/**
 * @brief Deinitialize and cleanup all camera preview resources
 * 
 * Stops preview, deletes camera controller and ISP processor,
 * and cleans up LVGL objects.
 */
void camera_deinit(void);

#ifdef __cplusplus
}
#endif
