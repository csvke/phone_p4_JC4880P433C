/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_sensor.h
 * @brief OV02C10 camera sensor management
 * 
 * This module handles:
 * - Sensor detection and initialization
 * - SCCB/I2C communication
 * - Format selection and configuration
 * - Streaming control (start/stop)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OV02C10 camera sensor
 * 
 * Performs:
 * - SCCB I2C communication setup
 * - Sensor detection
 * - Format configuration (1288x728 @ 1-lane or 1920x1080 @ 2-lane)
 * - Initial stream stop (ensures clean state)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if sensor not detected
 *      - ESP_FAIL if configuration fails
 */
esp_err_t camera_sensor_init(void);

/**
 * @brief Start sensor streaming
 * 
 * Enables sensor to output frames on MIPI CSI interface
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if sensor not initialized or stream start fails
 */
esp_err_t camera_sensor_start_stream(void);

/**
 * @brief Stop sensor streaming
 * 
 * Disables sensor frame output
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if sensor not initialized or stream stop fails
 */
esp_err_t camera_sensor_stop_stream(void);

/**
 * @brief Cleanup sensor resources
 * 
 * Stops streaming and releases sensor handle
 */
void camera_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
