/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_ppa.c
 * @brief Pixel Processing Accelerator (PPA) implementation
 * 
 * This module handles PPA client registration for potential hardware
 * scaling operations. Currently used for registration; hardware scaling
 * could be added later to reduce software downscaling burden.
 */

#include "camera_ppa.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "camera_ppa";

// PPA client handle and semaphore (declared in camera_internal.h)
// ppa_client_handle_t ppa_client = NULL;
// SemaphoreHandle_t ppa_done_sem = NULL;

/**
 * PPA callback: Called when scaling operation completes
 */
static bool ppa_transaction_done_cb(ppa_client_handle_t ppa_client, 
                                   ppa_event_data_t *event_data, 
                                   void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    // Track PPA callback count (separate from camera callbacks)
    ppa_callback_count++;
    
    // Signal that PPA scaling is complete
    if (ppa_done_sem) {
        xSemaphoreGiveFromISR(ppa_done_sem, &high_task_woken);
        // Log every 10th callback to avoid spam
        if (ppa_callback_count <= 10 || ppa_callback_count % 10 == 0) {
            ESP_EARLY_LOGI(TAG, "PPA callback #%lu fired, semaphore signaled", 
                          ppa_callback_count);
        }
    } else {
        ESP_EARLY_LOGE(TAG, "PPA callback fired but semaphore is NULL!");
    }
    
    return high_task_woken == pdTRUE;
}

esp_err_t camera_ppa_init(void)
{
    if (ppa_client) {
        ESP_LOGW(TAG, "PPA already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing PPA for scaling %dx%d → %dx%d...", 
             CAMERA_HRES, CAMERA_VRES, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Register PPA client for Scale-Rotate-Mirror (SRM) operation
    ppa_client_config_t ppa_config = {
        .oper_type = PPA_OPERATION_SRM,  // Scale-Rotate-Mirror
        .max_pending_trans_num = 1,
    };
    
    ESP_RETURN_ON_ERROR(
        ppa_register_client(&ppa_config, &ppa_client),
        TAG, "Failed to register PPA client"
    );
    
    // Register PPA callback
    ppa_event_callbacks_t ppa_cbs = {
        .on_trans_done = ppa_transaction_done_cb,
    };
    
    ESP_RETURN_ON_ERROR(
        ppa_client_register_event_callbacks(ppa_client, &ppa_cbs),
        TAG, "Failed to register PPA callbacks"
    );
    
    // Create semaphore for PPA completion
    ppa_done_sem = xSemaphoreCreateBinary();
    if (!ppa_done_sem) {
        ESP_LOGE(TAG, "Failed to create PPA semaphore");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "PPA initialized successfully");
    return ESP_OK;
}

void camera_ppa_deinit(void)
{
    if (ppa_done_sem) {
        vSemaphoreDelete(ppa_done_sem);
        ppa_done_sem = NULL;
    }
    
    if (ppa_client) {
        ESP_LOGI(TAG, "Cleaning up PPA resources");
        ppa_unregister_client(ppa_client);
        ppa_client = NULL;
    }
    
    ppa_callback_count = 0;
}
