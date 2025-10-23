/**
 * @file camera_task.c
 * @brief Camera preview task implementation
 * 
 * FreeRTOS task that receives frames from camera controller and copies them
 * to display buffer using async DMA.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera_task.h"
#include "camera_internal.h"
#include "camera_dma.h"

static const char *TAG = "camera_task";

// Task state
static TaskHandle_t preview_task_handle = NULL;
static volatile bool task_running = false;

/**
 * Preview task - receives frames and copies to display buffer
 */
static void preview_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Preview task started");
    
    // Subscribe to watchdog
    esp_task_wdt_add(NULL);
    
    uint32_t frame_count = 0;
    TickType_t last_log_time = xTaskGetTickCount();
    
    while (task_running) {
        // Wait for frame ready notification from controller callback
        if (xSemaphoreTake(frame_ready_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            UBaseType_t sem_count = uxSemaphoreGetCount(frame_ready_sem);
            ESP_LOGE(TAG, "⚠️  FRAME TIMEOUT after frame #%lu (sem=%u, drops=%lu)", 
                     frame_count, sem_count, semaphore_overflow_count);
            continue;
        }
        
        // Check semaphore backlog
        UBaseType_t sem_count = uxSemaphoreGetCount(frame_ready_sem);
        if (sem_count > 5) {
            ESP_LOGW(TAG, "⚠️ Semaphore backlog: %u pending frames", sem_count);
        }
        
        // Measure frame timing
        int64_t frame_time_us = esp_timer_get_time();
        
        frame_count++;
        
        // Calculate FPS
        int64_t frame_interval_us = 0;
        if (last_frame_time_us != 0) {
            frame_interval_us = frame_time_us - last_frame_time_us;
        }
        last_frame_time_us = frame_time_us;
        
        // Log timing for first 3 frames, then every 30th
        if (frame_count <= 3 || frame_count % 30 == 0) {
            if (frame_interval_us > 0) {
                float fps = 1000000.0f / frame_interval_us;
                ESP_LOGI(TAG, "Frame #%lu: interval %lld μs (%.1f FPS)", 
                         frame_count, frame_interval_us, fps);
            }
        }
        
        // Sync cache before accessing buffer
        int64_t cache_start = esp_timer_get_time();
        esp_cache_msync(frame_trans.buffer, frame_trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        int64_t cache_end = esp_timer_get_time();
        
        // Sample pixels for debugging (first 10 frames)
        if (frame_count <= 10) {
            uint16_t *pixels = (uint16_t *)frame_trans.buffer;
            int mid_idx = (CAMERA_VRES / 2) * CAMERA_HRES + (CAMERA_HRES / 2);
            int corner_idx = (CAMERA_VRES - 1) * CAMERA_HRES + (CAMERA_HRES - 1);
            ESP_LOGI(TAG, "Frame #%lu pixels - Start: 0x%04x Mid: 0x%04x End: 0x%04x", 
                     frame_count, pixels[0], pixels[mid_idx], pixels[corner_idx]);
        }
        
        // Start async DMA transfer to display buffer
        int64_t dma_start = esp_timer_get_time();
        
        if (scaled_buffer && dma_handle) {
            esp_err_t dma_ret = camera_dma_transfer(
                scaled_buffer, 
                frame_trans.buffer, 
                CAMERA_HRES * CAMERA_VRES * sizeof(uint16_t)
            );
            
            int64_t dma_end = esp_timer_get_time();
            
            if (dma_ret == ESP_OK) {
                // DMA started - callback will set frame_ready_for_display
                if (frame_count <= 10) {
                    ESP_LOGI(TAG, "Frame #%lu - DMA started (async), time: %lld us", 
                             frame_count, dma_end - dma_start);
                }
            } else {
                // Fallback to CPU memcpy
                ESP_LOGW(TAG, "DMA failed (%s), using CPU memcpy", esp_err_to_name(dma_ret));
                memcpy(scaled_buffer, frame_trans.buffer, 
                       CAMERA_HRES * CAMERA_VRES * sizeof(uint16_t));
                
                dma_end = esp_timer_get_time();
                frame_ready_for_display = true;
                
                // Signal for CPU fallback
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xSemaphoreGiveFromISR(frame_ready_sem, &xHigherPriorityTaskWoken);
                
                if (frame_count <= 10) {
                    ESP_LOGW(TAG, "Frame #%lu - CPU fallback, time: %lld us", 
                             frame_count, dma_end - dma_start);
                }
            }
        }
        
        int64_t total_end = esp_timer_get_time();
        
        // Log detailed timing for first 10 frames
        if (frame_count <= 10) {
            ESP_LOGI(TAG, "Frame #%lu timing - Cache: %lld us, Total: %lld us", 
                     frame_count, cache_end - cache_start, total_end - frame_time_us);
        }
        
        // Yield and reset watchdog
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
        
        // Log status every second
        TickType_t now = xTaskGetTickCount();
        if ((now - last_log_time) >= pdMS_TO_TICKS(1000)) {
            UBaseType_t queue_size = uxSemaphoreGetCount(frame_ready_sem);
            ESP_LOGI(TAG, "📊 %lu fps | Drops: %lu | LVGL fails: %lu | Queue: %u", 
                     frame_count, semaphore_overflow_count, 
                     lvgl_lock_timeout_count, queue_size);
            frame_count = 0;
            last_log_time = now;
        }
    }
    
    ESP_LOGI(TAG, "Preview task ending");
    esp_task_wdt_delete(NULL);
    preview_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t camera_task_start(void)
{
    if (task_running) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }
    
    task_running = true;
    
    BaseType_t ret = xTaskCreate(
        preview_task,
        "camera_preview",
        8192,
        NULL,
        tskIDLE_PRIORITY + 3,
        &preview_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        task_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Task created successfully");
    return ESP_OK;
}

void camera_task_stop(void)
{
    if (!task_running) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping task...");
    
    // Signal task to stop
    task_running = false;
    
    // Wake up task if waiting
    if (frame_ready_sem) {
        xSemaphoreGive(frame_ready_sem);
    }
    
    // Wait for task to finish
    if (preview_task_handle) {
        int timeout_ms = 500;
        while (preview_task_handle != NULL && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout_ms -= 10;
        }
    }
    
    ESP_LOGI(TAG, "Task stopped");
}

bool camera_task_is_running(void)
{
    return task_running;
}

TaskHandle_t camera_task_get_handle(void)
{
    return preview_task_handle;
}
