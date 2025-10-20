/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_isp.h
 * @brief ISP (Image Signal Processor) configuration and management
 * 
 * This module handles:
 * - ISP processor initialization
 * - Demosaic configuration (RAW10 → RGB565)
 * - Color Correction Matrix (CCM)
 * - Auto White Balance (AWB)
 * - Color adjustments (brightness, contrast, saturation, hue)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ISP processor with all modules
 * 
 * Configures and enables:
 * - ISP processor (80MHz clock)
 * - Demosaic module (Bayer RAW10 → RGB565)
 * - Color Correction Matrix (CCM) to fix color cast
 * - Auto White Balance (AWB)
 * - Color adjustments (brightness, contrast, saturation)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if ISP initialization fails
 */
esp_err_t camera_isp_init(void);

/**
 * @brief Cleanup ISP resources
 * 
 * Disables and deletes:
 * - AWB controller
 * - Demosaic module
 * - CCM module
 * - Color adjustment module
 * - ISP processor
 */
void camera_isp_deinit(void);

#ifdef __cplusplus
}
#endif
