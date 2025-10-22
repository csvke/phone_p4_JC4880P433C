/**
 * @file camera_display.h
 * @brief Camera display module - LVGL integration for camera preview
 * 
 * This module handles:
 * - LVGL widget creation and configuration for camera preview
 * - Display update timer management (30 FPS)
 * - Image scaling and positioning on screen
 * - Thread-safe display updates from camera frames
 * 
 * Phase 4 of camera refactoring - extracted from camera.c
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL display for camera preview
 * 
 * Creates LVGL image widget, calculates scaling factors, and positions the preview
 * on screen. Must be called after camera_set_parent().
 * 
 * @return ESP_OK on success, ESP_FAIL if display initialization fails
 */
esp_err_t camera_display_init(void);

/**
 * @brief Set parent LVGL object for camera preview
 * 
 * Must be called before camera_display_init()
 * 
 * @param parent LVGL object to contain the camera preview
 * @return esp_err_t ESP_OK on success
 */
esp_err_t camera_display_set_parent(lv_obj_t *parent);

/**
 * @brief Start display update timer
 * 
 * Creates LVGL timer that runs at 30 FPS to update the display from camera frames.
 * Timer callback runs in LVGL thread context (thread-safe).
 * 
 * @return ESP_OK on success, ESP_FAIL if timer creation fails
 */
esp_err_t camera_display_start_timer(void);

/**
 * @brief Stop and delete display update timer
 * 
 * Stops the LVGL timer that updates the display. Safe to call even if timer not running.
 */
void camera_display_stop_timer(void);

/**
 * @brief Clean up display resources
 * 
 * Deletes LVGL widgets and timer. Safe to call multiple times.
 */
void camera_display_deinit(void);

#ifdef __cplusplus
}
#endif
