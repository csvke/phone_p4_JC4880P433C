/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_controller.h
 * @brief CSI camera controller initialization and management
 * 
 * This module handles:
 * - CSI controller initialization
 * - MIPI CSI interface configuration
 * - Frame callbacks registration
 * - Controller start/stop
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CSI camera controller
 * 
 * Configures:
 * - CSI controller (MIPI interface)
 * - Resolution and lane configuration
 * - Input format (RAW10) and output format (RGB565 via ISP)
 * - Frame callbacks (on_get_new_trans, on_trans_finished)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if controller initialization fails
 */
esp_err_t camera_controller_init(void);

/**
 * @brief Start CSI controller streaming
 * 
 * Begins receiving frames from the camera sensor
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if controller not initialized or start fails
 */
esp_err_t camera_controller_start(void);

/**
 * @brief Stop CSI controller streaming
 * 
 * Stops receiving frames from the camera sensor
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if controller not initialized or stop fails
 */
esp_err_t camera_controller_stop(void);

/**
 * @brief Cleanup controller resources
 * 
 * Disables and deletes CSI controller
 */
void camera_controller_deinit(void);

#ifdef __cplusplus
}
#endif
