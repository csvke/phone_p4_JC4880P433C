/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_dma.h
 * @brief AXI GDMA async memcpy for camera frame transfers
 * 
 * This module handles:
 * - AXI GDMA initialization for PSRAM access
 * - Async memory copy from camera buffer to display buffer
 * - DMA completion callbacks
 * - Transfer statistics
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize AXI GDMA for async memcpy
 * 
 * Configures AXI GDMA with optimal settings for PSRAM access:
 * - 64-byte alignment for PSRAM efficiency
 * - 64-byte burst size
 * - Backlog queue for multiple pending transfers
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM if DMA initialization fails
 */
esp_err_t camera_dma_init(void);

/**
 * @brief Start async DMA transfer from camera buffer to display buffer
 * 
 * This function initiates a non-blocking DMA transfer. The DMA callback
 * will be invoked when the transfer completes.
 * 
 * @param[in] dst Destination buffer (display buffer)
 * @param[in] src Source buffer (camera frame buffer)
 * @param[in] size Transfer size in bytes
 * 
 * @return
 *      - ESP_OK if DMA started successfully
 *      - ESP_FAIL if DMA is not initialized or start failed
 */
esp_err_t camera_dma_transfer(void *dst, const void *src, size_t size);

/**
 * @brief Cleanup DMA resources
 */
void camera_dma_deinit(void);

#ifdef __cplusplus
}
#endif
