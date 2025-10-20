/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_display.h
 * @brief LVGL integration for camera viewfinder display
 * 
 * This module handles:
 * - LVGL image widget creation
 * - Display refresh timer
 * - Frame buffer presentation
 * - Display zoom calculations
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display for camera
 * 
 * Creates:
 * - LVGL image widget for viewfinder
 * - Display update timer
 * - Canvas descriptor for frame presentation
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if display initialization fails
 */
esp_err_t camera_display_init(void);

/**
 * @brief Set parent LVGL object for camera widget
 * 
 * @param parent Parent LVGL object (screen or container)
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if display not initialized
 */
esp_err_t camera_display_set_parent(lv_obj_t *parent);

/**
 * @brief Update display with latest camera frame
 * 
 * Called by timer callback to refresh the viewfinder
 * Performs software zoom/scale if needed
 */
void camera_display_update(void);

/**
 * @brief Cleanup display resources
 * 
 * Deletes:
 * - LVGL image widget
 * - Display update timer
 * - Canvas resources
 */
void camera_display_deinit(void);

#ifdef __cplusplus
}
#endif
