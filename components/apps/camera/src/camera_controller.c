/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_controller.c
 * @brief CSI camera controller initialization and management
 * 
 * This module handles:
 * - CSI controller initialization
 * - MIPI CSI interface configuration
 * - Frame callbacks registration
 * - Controller start/stop
 */

#include "camera_controller.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"

static const char *TAG = "camera_ctrl";

// Forward declarations for callbacks
static bool camera_get_new_vb(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data);
static bool camera_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data);

/**
 * Camera callback: Provide new buffer for next frame
 * 
 * This callback is called by the camera controller to get a buffer for the next frame.
 * We provide our pre-allocated frame buffer which the ISP will write into.
 */
static bool camera_get_new_vb(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    // Dereference to copy the structure by value (matching reference example)
    esp_cam_ctlr_trans_t new_trans = *(esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = new_trans.buffer;
    trans->buflen = new_trans.buflen;

    // Return true to indicate we successfully provided a buffer
    // Returning false would tell the driver we couldn't provide a buffer,
    // which could cause it to stop requesting frames!
    return true;
}

/**
 * Camera callback: Frame reception finished
 * 
 * This callback is fired by the camera controller when a frame is ready.
 * We signal the preview task and return true to release the buffer back to the driver.
 * 
 * Frame skipping: Camera captures at 30 FPS but PPA can only process at 13 FPS.
 * To prevent queue buildup, we skip frames (process every 2nd frame = ~15 FPS target).
 */
static bool camera_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Use system tick count (safe in ISR) - will measure in task
    last_callback_time = xTaskGetTickCountFromISR();
    
    // NO FRAME SKIPPING: With PPA rotation-only (~5-10ms), should handle all 30 FPS
    // Log first 20 frames, then every 30th to monitor performance
    if (callback_count <= 20 || callback_count % 30 == 0) {
        ESP_EARLY_LOGI(TAG, "Frame callback #%lu (processing all frames, PPA rotation-only)", callback_count);
    }
    
    // Signal the preview task that a new frame is ready for processing
    BaseType_t result = pdFAIL;
    if (frame_ready_sem) {
        result = xSemaphoreGiveFromISR(frame_ready_sem, &high_task_woken);
        
        // Track semaphore overflow (semaphore full, frame dropped)
        if (result == pdFAIL) {
            semaphore_overflow_count++;
            if (semaphore_overflow_count % 10 == 0) {
                ESP_EARLY_LOGW(TAG, "⚠️ Frame semaphore FULL! Dropped %lu frames total", semaphore_overflow_count);
            }
        }
    }
    
    // IMPORTANT: Return true to release buffer back to camera controller
    // The ISP outputs to our pre-allocated buffer, so the trans buffer is just metadata
    // and can be reused immediately. Returning false would block the driver!
    return true;
}

esp_err_t camera_controller_init(void)
{
    if (cam_handle) {
        ESP_LOGW(TAG, "Controller already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing CSI camera controller...");
    
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = CAMERA_HRES,
        .v_res = CAMERA_VRES,
        .lane_bit_rate_mbps = CAMERA_LANE_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,  // OV02C10 outputs RAW10
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,  // ISP converts RAW10 → RGB565
        .data_lane_num = CAMERA_DATA_LANES,
        .byte_swap_en = false,
        .queue_items = 2,  // Buffer queue depth
    };
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 🔍 DETAILED CSI/ISP TIMING DIAGNOSTICS
    // ═══════════════════════════════════════════════════════════════════════════
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           CSI CONTROLLER CONFIGURATION                       ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  Resolution:        %d × %d pixels", CAMERA_HRES, CAMERA_VRES);
    ESP_LOGI(TAG, "  Frame size:        %d bytes (RGB565)", CAMERA_HRES * CAMERA_VRES * 2);
    ESP_LOGI(TAG, "  Data lanes:        %d", CAMERA_DATA_LANES);
    ESP_LOGI(TAG, "  Lane bit rate:     %d Mbps per lane", CAMERA_LANE_BITRATE_MBPS);
    ESP_LOGI(TAG, "  Total bandwidth:   %d Mbps (%d lanes × %d Mbps)", 
             CAMERA_LANE_BITRATE_MBPS * CAMERA_DATA_LANES, 
             CAMERA_DATA_LANES, 
             CAMERA_LANE_BITRATE_MBPS);
    ESP_LOGI(TAG, "  Input format:      RAW10 (10 bits per pixel)");
    ESP_LOGI(TAG, "  Output format:     RGB565 (16 bits per pixel, ISP Demosaic)");
    ESP_LOGI(TAG, "");
    
    // Calculate expected frame delivery time
    uint32_t raw10_frame_bits = CAMERA_HRES * CAMERA_VRES * 10;  // 10 bits per pixel
    uint32_t total_bandwidth_bps = (uint64_t)CAMERA_LANE_BITRATE_MBPS * CAMERA_DATA_LANES * 1000000ULL;
    uint32_t expected_frame_time_us = ((uint64_t)raw10_frame_bits * 1000000ULL) / total_bandwidth_bps;
    float expected_fps = 1000000.0f / expected_frame_time_us;
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           THEORETICAL PERFORMANCE                            ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  RAW10 frame size:  %d bits", raw10_frame_bits);
    ESP_LOGI(TAG, "  Expected transfer: %d μs per frame", expected_frame_time_us);
    ESP_LOGI(TAG, "  Expected FPS:      %.1f FPS (MIPI bandwidth only)", expected_fps);
    ESP_LOGI(TAG, "  Sensor configured: ~27 FPS (HTS=2280, VTS=1164, 81.67MHz)");
    ESP_LOGI(TAG, "");
    
    ESP_RETURN_ON_ERROR(
        esp_cam_new_csi_ctlr(&csi_config, &cam_handle),
        TAG, "Failed to create CSI controller"
    );
    
    // Register callbacks
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = camera_get_new_vb,
        .on_trans_finished = camera_trans_finished,
    };
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_register_event_callbacks(cam_handle, &cbs, &frame_trans),
        TAG, "Failed to register camera callbacks"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_enable(cam_handle),
        TAG, "Failed to enable camera controller"
    );
    
    ESP_LOGI(TAG, "CSI camera controller initialized");
    
    return ESP_OK;
}

esp_err_t camera_controller_start(void)
{
    if (!cam_handle) {
        ESP_LOGE(TAG, "Controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_start(cam_handle),
        TAG, "Failed to start camera controller"
    );
    
    ESP_LOGI(TAG, "Camera controller started");
    return ESP_OK;
}

esp_err_t camera_controller_stop(void)
{
    if (!cam_handle) {
        ESP_LOGW(TAG, "Controller not initialized");
        return ESP_OK;
    }
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_stop(cam_handle),
        TAG, "Failed to stop camera controller"
    );
    
    ESP_LOGI(TAG, "Camera controller stopped");
    return ESP_OK;
}

void camera_controller_deinit(void)
{
    if (cam_handle) {
        ESP_LOGI(TAG, "Cleaning up controller resources");
        camera_controller_stop();
        esp_cam_ctlr_disable(cam_handle);
        esp_cam_ctlr_del(cam_handle);
        cam_handle = NULL;
    }
}
