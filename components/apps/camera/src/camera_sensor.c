/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_sensor.c
 * @brief OV02C10 camera sensor management implementation
 * 
 * This module handles:
 * - Sensor detection and initialization
 * - SCCB/I2C communication setup
 * - Format configuration (resolution selection)
 * - Streaming control (start/stop)
 */

#include "camera_sensor.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_cam_sensor.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "ov02c10.h"
#include <string.h>

static const char *TAG = "camera_sensor";

esp_err_t camera_sensor_init(void)
{
    if (cam_sensor) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return ESP_OK;
    }
    
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing OV02C10 camera sensor...");
    
    // Create SCCB I2C handle for sensor communication
    esp_sccb_io_handle_t sccb_io_handle = NULL;
    sccb_i2c_config_t i2c_config = {
        .scl_speed_hz = 100000,  // 100kHz for SCCB
        .device_address = OV02C10_SCCB_ADDR,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    };
    
    esp_err_t ret = sccb_new_i2c_io(i2c_bus, &i2c_config, &sccb_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SCCB I2C handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure and detect OV02C10 sensor
    esp_cam_sensor_config_t sensor_config = {
        .sccb_handle = sccb_io_handle,
        .reset_pin = -1,  // Reset handled by BSP
        .pwdn_pin = -1,   // Power down handled by BSP
        .xclk_pin = -1,   // XCLK not used (sensor has internal clock)
        .xclk_freq_hz = 0,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };
    
    cam_sensor = ov02c10_detect(&sensor_config);
    if (!cam_sensor) {
        ESP_LOGE(TAG, "Failed to detect OV02C10 sensor");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "OV02C10 sensor detected successfully");
    
    // Configure sensor for target resolution
    esp_cam_sensor_format_array_t formats;
    cam_sensor->ops->query_support_formats(cam_sensor, &formats);
    
    // Log all available formats
    ESP_LOGI(TAG, "Sensor supports %d formats:", formats.count);
    for (uint32_t i = 0; i < formats.count; i++) {
        ESP_LOGI(TAG, "  [%d] %s: %dx%d @ %d fps", 
                i, 
                formats.format_array[i].name,
                formats.format_array[i].width, 
                formats.format_array[i].height, 
                formats.format_array[i].fps);
    }
    
    // Find matching format (resolution and lane count)
    const esp_cam_sensor_format_t *target_format = NULL;
    const char *lane_keyword = (CAMERA_DATA_LANES == 2) ? "2lane" : "1lane";
    
    for (uint32_t i = 0; i < formats.count; i++) {
        if (formats.format_array[i].width == CAMERA_HRES &&
            formats.format_array[i].height == CAMERA_VRES) {
            // Check if lane count matches
            if (strstr(formats.format_array[i].name, lane_keyword)) {
                target_format = &formats.format_array[i];
                ESP_LOGI(TAG, "Selected format: %s (%dx%d @ %d fps)",
                        target_format->name,
                        target_format->width, target_format->height, target_format->fps);
                break;
            } else if (!target_format) {
                // Fallback to first matching resolution if no lane-specific format found
                target_format = &formats.format_array[i];
            }
        }
    }
    
    if (target_format && !strstr(target_format->name, lane_keyword)) {
        ESP_LOGW(TAG, "Could not find %s format, using: %s", lane_keyword, target_format->name);
    }
    
    if (!target_format) {
        ESP_LOGE(TAG, "Sensor does not support %dx%d resolution", CAMERA_HRES, CAMERA_VRES);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Set the format
    ret = cam_sensor->ops->set_format(cam_sensor, target_format);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sensor format: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Sensor format set successfully");
    
    // CRITICAL: Stop sensor streaming if it's already running (first boot scenario)
    // On first boot, sensor hardware may be in unknown state (powered on by bootloader
    // or residual state from previous power cycle). Explicitly stop to ensure clean state.
    int stream_off = 0;
    ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_off);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sensor stream stopped (ensuring clean state on first init)");
    } else {
        ESP_LOGW(TAG, "Sensor stream stop returned: %s (may already be stopped)", esp_err_to_name(ret));
    }
    
    return ESP_OK;
}

esp_err_t camera_sensor_start_stream(void)
{
    if (!cam_sensor) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    int stream_on = 1;
    esp_err_t ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor streaming: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Sensor stream started");
    
    return ESP_OK;
}

esp_err_t camera_sensor_stop_stream(void)
{
    if (!cam_sensor) {
        ESP_LOGW(TAG, "Sensor not initialized");
        return ESP_OK;  // Not an error if already stopped
    }
    
    int stream_off = 0;
    esp_err_t ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_off);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sensor stream stop returned: %s", esp_err_to_name(ret));
        // Don't return error - sensor might already be stopped
    } else {
        ESP_LOGI(TAG, "Sensor stream stopped");
    }
    
    return ESP_OK;
}

void camera_sensor_deinit(void)
{
    if (cam_sensor) {
        ESP_LOGI(TAG, "Cleaning up sensor resources");
        camera_sensor_stop_stream();
        // Note: cam_sensor deletion is handled by the sensor driver
        cam_sensor = NULL;
    }
}
