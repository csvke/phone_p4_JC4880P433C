/**
 * @file camera_task.h
 * @brief Camera preview task management
 * 
 * Manages the FreeRTOS task that receives frames from the camera controller
 * and copies them to the display buffer using async DMA.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the camera preview task
 * 
 * Creates a FreeRTOS task that continuously receives frames from the camera
 * controller and copies them to the display buffer using async DMA.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL if task creation fails
 */
esp_err_t camera_task_start(void);

/**
 * @brief Stop the camera preview task
 * 
 * Signals the task to stop and waits for it to finish.
 * Safe to call even if task is not running.
 */
void camera_task_stop(void);

/**
 * @brief Check if preview task is running
 * 
 * @return true if task is running, false otherwise
 */
bool camera_task_is_running(void);

/**
 * @brief Get the task handle
 * 
 * @return Task handle, or NULL if task is not running
 */
TaskHandle_t camera_task_get_handle(void);

#ifdef __cplusplus
}
#endif
