/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_dma.c
 * @brief AXI GDMA async memcpy implementation for camera frame transfers
 * 
 * This module provides non-blocking PSRAM-to-PSRAM memory transfers
 * using ESP32-P4's AXI GDMA engine. Achieves ~16ms transfer time for
 * 1.8MB frames, leaving headroom for 30+ FPS operation.
 */

#include "camera_dma.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_async_memcpy.h"

static const char *TAG = "camera_dma";

/**
 * DMA callback: Called when async memcpy completes
 * Runs in ISR context - must be fast and use ISR-safe functions only
 * 
 * This signals that the frame is ready for display after DMA completes.
 */
static bool IRAM_ATTR dma_done_callback(async_memcpy_handle_t mcp_hdl, 
                                        async_memcpy_event_t *event, 
                                        void *cb_args)
{
    // Mark frame as ready for display (DMA completed successfully)
    // The LVGL timer will poll this flag and update the display
    frame_ready_for_display = true;
    
    // DO NOT signal frame_ready_sem here! That semaphore is for camera frames only.
    // The camera ISR signals when a new frame is captured (camera_trans_finished).
    // The preview task processes camera frames immediately (launches DMA).
    // The LVGL timer polls frame_ready_for_display to update display when DMA completes.
    
    // No need to wake any task - LVGL timer runs independently
    return false;  // No higher priority task woken
}

esp_err_t camera_dma_init(void)
{
    if (dma_handle) {
        ESP_LOGW(TAG, "DMA already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing AXI GDMA for async PSRAM memcpy...");
    
    async_memcpy_config_t dma_config = {
        .backlog = 4,  // Queue up to 4 pending transfers
        .sram_trans_align = 4,  // 4-byte alignment for SRAM
        .psram_trans_align = 64,  // 64-byte alignment for PSRAM (optimal for cache)
        .dma_burst_size = 64,  // 64-byte bursts for PSRAM efficiency
        .flags = 0
    };
    
    // ESP32-P4 has AXI GDMA which can access PSRAM efficiently
    // Note: Default install() uses AHB GDMA which doesn't support PSRAM!
    esp_err_t ret = esp_async_memcpy_install_gdma_axi(&dma_config, &dma_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AXI GDMA: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "AXI GDMA initialized (async, non-blocking, ~16ms per 1.8MB frame)");
    return ESP_OK;
}

esp_err_t camera_dma_transfer(void *dst, const void *src, size_t size)
{
    if (!dma_handle) {
        ESP_LOGE(TAG, "DMA not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!dst || !src || size == 0) {
        ESP_LOGE(TAG, "Invalid transfer parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Launch async memcpy (non-blocking)
    // Callback will fire when complete
    // Cast away const - esp_async_memcpy doesn't modify src but API doesn't use const
    esp_err_t ret = esp_async_memcpy(dma_handle, dst, (void *)src, size, 
                                     dma_done_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Async memcpy failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

void camera_dma_deinit(void)
{
    if (dma_handle) {
        ESP_LOGI(TAG, "Cleaning up DMA resources");
        esp_async_memcpy_uninstall(dma_handle);
        dma_handle = NULL;
    }
}
