/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_ppa.h
 * @brief Pixel Processing Accelerator (PPA) operations
 * 
 * This module handles:
 * - PPA client registration
 * - Hardware scaling operations
 * - Hardware rotation operations
 * - PPA callbacks
 * 
 * Note: Currently used for registration. Hardware scaling could be added
 * later to reduce software downscaling burden (1288x728 -> 480x271).
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize PPA client
 * 
 * Registers PPA client for potential hardware operations:
 * - Scaling (future: to reduce 3.4x downscale burden)
 * - Rotation (if needed)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if PPA initialization fails
 */
esp_err_t camera_ppa_init(void);

/**
 * @brief Cleanup PPA resources
 * 
 * Unregisters and deletes PPA client
 */
void camera_ppa_deinit(void);

#ifdef __cplusplus
}
#endif
