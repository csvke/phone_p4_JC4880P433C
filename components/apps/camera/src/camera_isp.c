/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_isp.c
 * @brief ISP (Image Signal Processor) configuration implementation
 * 
 * This module configures the ESP32-P4's ISP for RAW10 to RGB565 conversion
 * including demosaicing, color correction, white balance, and color adjustments.
 */

#include "camera_isp.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/isp.h"
#include "driver/isp_demosaic.h"
#include "driver/isp_ccm.h"
#include "driver/isp_awb.h"
#include "driver/isp_color.h"

static const char *TAG = "camera_isp";

// ISP clock (80MHz is recommended for ESP32-P4)
#define ISP_CLK_HZ (80 * 1000 * 1000)

esp_err_t camera_isp_init(void)
{
    if (isp_proc) {
        ESP_LOGW(TAG, "ISP already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           ISP PROCESSOR CONFIGURATION                        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = ISP_CLK_HZ,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW10,  // OV02C10 outputs RAW10
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = CAMERA_HRES,
        .v_res = CAMERA_VRES,
    };
    
    ESP_LOGI(TAG, "  ISP Clock:         %d Hz (%.1f MHz)", ISP_CLK_HZ, ISP_CLK_HZ / 1000000.0f);
    ESP_LOGI(TAG, "  Input:             RAW10 (10-bit Bayer from sensor)");
    ESP_LOGI(TAG, "  Output:            RGB565 (16-bit after Demosaic)");
    ESP_LOGI(TAG, "  Resolution:        %d × %d", CAMERA_HRES, CAMERA_VRES);
    ESP_LOGI(TAG, "  Processing:        Demosaic, AWB, Color Correction, Sharpen");
    ESP_LOGI(TAG, "");
    
    ESP_RETURN_ON_ERROR(
        esp_isp_new_processor(&isp_config, &isp_proc),
        TAG, "Failed to create ISP processor"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_enable(isp_proc),
        TAG, "Failed to enable ISP processor"
    );
    
    // ═══════════════════════════════════════════════════════════════════════
    // Configure Demosaic module for RAW10 → RGB conversion (Bayer demosaicing)
    // ═══════════════════════════════════════════════════════════════════════
    esp_isp_demosaic_config_t demosaic_config = {
        .grad_ratio = {
            .integer = 2,  // Gradient ratio integer part (0-3 valid range)
            .decimal = 0,
        },
        .padding_mode = ISP_DEMOSAIC_EDGE_PADDING_MODE_SRND_DATA,
        .padding_data = 0x00,
        .padding_line_tail_valid_start_pixel = 0,
        .padding_line_tail_valid_end_pixel = 0,
    };
    
    ESP_RETURN_ON_ERROR(
        esp_isp_demosaic_configure(isp_proc, &demosaic_config),
        TAG, "Failed to configure Demosaic"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_demosaic_enable(isp_proc),
        TAG, "Failed to enable Demosaic (RAW to RGB conversion)"
    );
    
    ESP_LOGI(TAG, "ISP Demosaic module enabled (RAW10 → RGB conversion)");
    
    // ═══════════════════════════════════════════════════════════════════════
    // Configure Color Correction Matrix (CCM) to fix purple tint
    // ═══════════════════════════════════════════════════════════════════════
    esp_isp_ccm_config_t ccm_config = {
        .matrix = {
            {1.0,  0.0,  0.0},   // Red channel: pass through
            {0.0,  1.0,  0.0},   // Green channel: pass through
            {0.0,  0.0,  0.75}   // Blue channel: reduce by 25% to counteract purple tint
        },
        .saturation = true
    };
    
    ESP_RETURN_ON_ERROR(
        esp_isp_ccm_configure(isp_proc, &ccm_config),
        TAG, "Failed to configure CCM"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_ccm_enable(isp_proc),
        TAG, "Failed to enable CCM"
    );
    
    // ═══════════════════════════════════════════════════════════════════════
    // Configure Automatic White Balance (AWB)
    // ═══════════════════════════════════════════════════════════════════════
    esp_isp_awb_config_t awb_config = {
        .sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM,  // Sample after CCM correction
        .window = {
            .top_left = {
                .x = CAMERA_HRES / 6,      // Start 1/6 from left
                .y = CAMERA_VRES / 6       // Start 1/6 from top
            },
            .btm_right = {
                .x = CAMERA_HRES * 5 / 6,  // End 5/6 from left (middle 2/3)
                .y = CAMERA_VRES * 5 / 6   // End 5/6 from top (middle 2/3)
            }
        },
        .white_patch = {
            .luminance = {
                .min = 0,       // Allow low light operation
                .max = 220 * 3  // Exclude overexposed pixels (not 255*3)
            },
            .red_green_ratio = {
                .min = 0.0,
                .max = 3.999    // Wide range to catch all color casts
            },
            .blue_green_ratio = {
                .min = 0.0,
                .max = 3.999    // Wide range to catch all color casts
            }
        },
        .intr_priority = 0  // Let driver choose priority
    };
    
    ESP_RETURN_ON_ERROR(
        esp_isp_new_awb_controller(isp_proc, &awb_config, &awb_ctrl),
        TAG, "Failed to create AWB controller"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_awb_controller_enable(awb_ctrl),
        TAG, "Failed to enable AWB controller"
    );
    
    // ═══════════════════════════════════════════════════════════════════════
    // Configure Color Adjustments (brightness, saturation, contrast, hue)
    // ═══════════════════════════════════════════════════════════════════════
    esp_isp_color_config_t color_config = {
        .color_contrast = {
            .integer = 1,
            .decimal = 0        // Contrast: 1.0 (normal, range 0-1.0)
        },
        .color_saturation = {
            .integer = 1,
            .decimal = 0        // Saturation: 1.0 (normal, range 0-1.0)
        },
        .color_hue = 0,         // Hue: 0 degrees (no shift, range 0-360)
        .color_brightness = 0   // Brightness: 0 (normal, range -128 to +127)
    };
    
    ESP_RETURN_ON_ERROR(
        esp_isp_color_configure(isp_proc, &color_config),
        TAG, "Failed to configure color adjustments"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_color_enable(isp_proc),
        TAG, "Failed to enable color adjustments"
    );
    
    ESP_LOGI(TAG, "ISP processor initialized with Demosaic, CCM, AWB, and color adjustments");
    return ESP_OK;
}

void camera_isp_deinit(void)
{
    if (awb_ctrl) {
        ESP_LOGI(TAG, "Disabling AWB controller");
        esp_isp_awb_controller_disable(awb_ctrl);
        esp_isp_del_awb_controller(awb_ctrl);
        awb_ctrl = NULL;
    }
    
    if (isp_proc) {
        ESP_LOGI(TAG, "Cleaning up ISP resources");
        esp_isp_disable(isp_proc);
        esp_isp_del_processor(isp_proc);
        isp_proc = NULL;
    }
}
