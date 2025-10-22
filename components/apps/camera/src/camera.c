/*
 * Camera Application for ESP32-P4 with ESP-IDF 5.5.1
 * 
 * Architecture:
 * - MIPI CSI Camera Controller (esp_cam_ctlr_csi)
 * - ISP Processor (driver/isp) for image processing
 * - LVGL canvas integration for Brookesia UI framework
 * 
 * Hardware:
 * - ESP32-P4 with MIPI CSI interface
 * - OV02C10 camera sensor (1920x1080 @ 30fps, RAW8, 2-lane)
 * - Shared I2C bus for sensor communication (via BSP)
 * 
 * Data Flow:
 * OV02C10 (RAW8) → MIPI CSI → CSI Controller → ISP Processor → RGB565 → LVGL Canvas
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// BSP and Hardware APIs
#include "bsp/esp-bsp.h"
#include "bsp/camera.h"
#include "esp_lvgl_port.h"

// Camera Controller Driver APIs (NEW in ESP-IDF 5.5.1)
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "driver/isp.h"
#include "driver/isp_demosaic.h"
#include "driver/isp_ccm.h"
#include "driver/isp_awb.h"
#include "driver/isp_color.h"
#include "driver/i2c_master.h"
#include "driver/ppa.h"  // Pixel Processing Accelerator for scaling
#include "esp_async_memcpy.h"  // Hardware DMA for fast PSRAM memcpy

// Camera Sensor API
#include "esp_cam_sensor.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "ov02c10.h"

// LVGL for display
#include "lvgl.h"

// Local includes
#include "camera.h"
#include "camera_internal.h"
#include "camera_dma.h"
#include "camera_ppa.h"
#include "camera_isp.h"
#include "camera_sensor.h"
#include "camera_controller.h"
#include "camera_display.h"

static const char *TAG = "camera";

// Camera configuration (from sdkconfig.defaults)
// PRODUCTION: OV02C10 1-lane 1288x728 - stable 30+ FPS with async DMA
// Frame buffer size: 1288x728x2 = 1,875,968 bytes (~1.8MB)
// With async DMA: ~16ms transfer time, leaving headroom for 30+ FPS
#define CAMERA_HRES 1288
#define CAMERA_VRES 728
#define CAMERA_LANE_BITRATE_MBPS 400
#define CAMERA_DATA_LANES 1

// Testing: 2-lane 1920x1080 achieved 23 FPS with async DMA
// #define CAMERA_HRES 1920
// #define CAMERA_VRES 1080
// #define CAMERA_LANE_BITRATE_MBPS 400
// #define CAMERA_DATA_LANES 2

// Display configuration (480x800 portrait)
// PPA will scale camera output to fit display
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 800

// Frame buffer configuration
#define CAMERA_FRAME_BUFFER_COUNT 1  // Single frame buffer for LVGL integration
#define RGB565_BITS_PER_PIXEL 16

// ISP clock (80MHz is recommended for ESP32-P4)
#define ISP_CLK_HZ (80 * 1000 * 1000)

// ═══════════════════════════════════════════════════════════════════════════
// Global state variables (declared in camera_internal.h)
// ═══════════════════════════════════════════════════════════════════════════

// Initialization flags
bool camera_initialized = false;
bool preview_running = false;

// Hardware handles
esp_cam_ctlr_handle_t cam_handle = NULL;
isp_proc_handle_t isp_proc = NULL;
isp_awb_ctlr_t awb_ctrl = NULL;
i2c_master_bus_handle_t i2c_bus = NULL;
esp_cam_sensor_device_t *cam_sensor = NULL;
ppa_client_handle_t ppa_client = NULL;
async_memcpy_handle_t dma_handle = NULL;

// Synchronization primitives
SemaphoreHandle_t frame_ready_sem = NULL;
SemaphoreHandle_t ppa_done_sem = NULL;

// LVGL display objects
lv_obj_t *preview_parent = NULL;
lv_obj_t *preview_canvas = NULL;
lv_display_t *display = NULL;
lv_timer_t *update_timer = NULL;

// Frame buffers
void *frame_buffer = NULL;       // Camera output buffer (1288x728)
size_t frame_buffer_size = 0;
void *scaled_buffer = NULL;      // Scaled buffer for display (480x800)
size_t scaled_buffer_size = 0;

// Flag to indicate new frame is ready for display
volatile bool frame_ready_for_display = false;

// Camera transaction descriptor
esp_cam_ctlr_trans_t frame_trans = {0};

// Task handle for frame reception
TaskHandle_t preview_task_handle = NULL;

// Timing measurement variables (use TickType_t for ISR-safe timing)
TickType_t last_callback_time = 0;
int64_t last_frame_time_us = 0;

// Diagnostic counters for debugging freeze issue
uint32_t ppa_callback_count = 0;
uint32_t semaphore_overflow_count = 0;
uint32_t lvgl_lock_timeout_count = 0;



/**
 * Initialize ISP processor - NOW IMPLEMENTED IN camera_isp.c
 * This wrapper kept for legacy compatibility during refactoring
 */
static esp_err_t init_isp_processor(void)
{
    return camera_isp_init();
}

/**
 * Initialize LVGL canvas for camera preview
 */


/**
 * Preview task that receives frames and updates display
 * 
 * This follows the esp-idf example pattern: continuously call esp_cam_ctlr_receive()
 * in a blocking loop. The receive() call blocks until a frame is ready, then returns.
 */
static void preview_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Preview task started");
    
    // Subscribe to watchdog to prevent triggering (camera task keeps CPU1 busy)
    esp_task_wdt_add(NULL);  // Add current task to watchdog
    
    uint32_t frame_count = 0;
    TickType_t last_log_time = xTaskGetTickCount();
    
    // Use global frame_trans which is set up by callbacks (ISP requires this!)
    
    while (preview_running) {
        // *** ISP-INTEGRATED PATTERN: Pure callback-driven (no manual receive() needed) ***
        // The ISP callbacks handle buffer management automatically
        // We just wait for frame_ready notification from callback
        
        // Wait for callback notification that frame is ready
        if (xSemaphoreTake(frame_ready_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            // Get semaphore count to diagnose the issue
            UBaseType_t sem_count = uxSemaphoreGetCount(frame_ready_sem);
            ESP_LOGE(TAG, "⚠️  FRAME TIMEOUT after frame #%lu - no callback received in 1 second!", frame_count);
            ESP_LOGE(TAG, "    Semaphore count: %u, Dropped frames: %lu", sem_count, semaphore_overflow_count);
            ESP_LOGE(TAG, "    Camera callbacks should be firing - check if they stopped!");
            continue;
        }
        
        // Check semaphore backlog (how many pending frames)
        UBaseType_t sem_count = uxSemaphoreGetCount(frame_ready_sem);
        if (sem_count > 5) {
            ESP_LOGW(TAG, "⚠️ Semaphore backlog: %u pending frames queued", sem_count);
        }
        
        // Frame is ready in frame_trans.buffer (set by callbacks)
        esp_err_t ret = ESP_OK;
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive frame: %s", esp_err_to_name(ret));
            continue;
        }
        
        // Measure frame timing (NOW safe to use esp_timer_get_time in task context)
        int64_t frame_time_us = esp_timer_get_time();
        int64_t processing_start = frame_time_us;
        
        // Frame received successfully!
        frame_count++;
            
            // Calculate frame interval (time between consecutive frames)
            int64_t frame_interval_us = 0;
            if (last_frame_time_us != 0) {
                frame_interval_us = frame_time_us - last_frame_time_us;
            }
            last_frame_time_us = frame_time_us;
            
            // Log detailed timing analysis for first 10 frames
            // OPTIMIZATION: Disable verbose logging during frame processing
            // Logging takes ~200ms per frame and blocks the preview task!
            // Only log basic info for first 3 frames, then every 30th frame
            if (frame_count <= 3 || frame_count % 30 == 0) {
                if (frame_interval_us > 0) {
                    float fps = 1000000.0f / frame_interval_us;
                    ESP_LOGI(TAG, "Frame #%lu: interval %lld μs (%.1f FPS)", 
                             frame_count, frame_interval_us, fps);
                }
            }
            
            // Sync cache before accessing the frame buffer
            int64_t cache_sync_start = esp_timer_get_time();
            esp_cache_msync(frame_trans.buffer, frame_trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            int64_t cache_sync_end = esp_timer_get_time();
            
            // Sample pixels from different parts of the frame (first 10 frames only for debugging)
            if (frame_count <= 10) {
                uint16_t *pixels = (uint16_t *)frame_trans.buffer;
                int mid_idx = (CAMERA_VRES / 2) * CAMERA_HRES + (CAMERA_HRES / 2);
                int corner_idx = (CAMERA_VRES - 1) * CAMERA_HRES + (CAMERA_HRES - 1);
                ESP_LOGI(TAG, "Frame #%lu - Start: 0x%04x Mid: 0x%04x End: 0x%04x", 
                        frame_count, pixels[0], pixels[mid_idx], pixels[corner_idx]);
            }
            
            // TESTING: NO ROTATION - Camera physically mounted for portrait
            // Camera buffer is 1288×728 in memory, but if mounted rotated, 
            // the visual scene is portrait. Keep buffer dimensions as-is.
            // PPA: Just pass through (no rotation, no scaling)
            
            int64_t ppa_start = esp_timer_get_time();
            
            // ⚠️ TESTING: Skip memcpy to measure ISP timing only ⚠️
            #define SKIP_MEMCPY_TEST 0
            
            #if SKIP_MEMCPY_TEST
            // Skip memcpy completely - just measure ISP output timing
            int64_t ppa_end = esp_timer_get_time();
            
            // Signal frame ready WITHOUT copying
            frame_ready_for_display = false;  // Don't display (buffer not copied)
            
            if (frame_count <= 10) {
                ESP_LOGI(TAG, "Frame #%lu - ISP output ready (SKIPPED memcpy test)", 
                        frame_count);
                ESP_LOGI(TAG, "                ISP-only time: %lld us", 
                        ppa_end - ppa_start);
            }
            #else
            // ASYNC PATH: Use AXI GDMA for fast PSRAM-to-PSRAM memcpy (non-blocking)
            if (scaled_buffer && dma_handle) {
                // Start DMA transfer (non-blocking, returns immediately ~1-2us)
                // Callback will signal frame_ready_for_display when DMA completes
                esp_err_t dma_ret = camera_dma_transfer(scaled_buffer, 
                                                        frame_trans.buffer, 
                                                        CAMERA_HRES * CAMERA_VRES * sizeof(uint16_t));
                
                int64_t ppa_end = esp_timer_get_time();
                
                if (dma_ret == ESP_OK) {
                    // DMA started successfully - it will complete in background
                    // Callback will set frame_ready_for_display and signal semaphore
                    
                    if (frame_count <= 10) {
                        ESP_LOGI(TAG, "Frame #%lu - DMA started (async, %dx%d)", 
                                frame_count, CAMERA_HRES, CAMERA_VRES);
                        ESP_LOGI(TAG, "                DMA start time: %lld us (non-blocking)", 
                                ppa_end - ppa_start);
                    }
                } else {
                    // Fallback to CPU memcpy if DMA fails to start
                    ESP_LOGW(TAG, "DMA start failed (%s), falling back to CPU memcpy", esp_err_to_name(dma_ret));
                    memcpy(scaled_buffer, frame_trans.buffer, 
                           CAMERA_HRES * CAMERA_VRES * sizeof(uint16_t));
                    
                    ppa_end = esp_timer_get_time();
                    frame_ready_for_display = true;
                    
                    // Signal semaphore for CPU fallback path
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    xSemaphoreGiveFromISR(frame_ready_sem, &xHigherPriorityTaskWoken);
                    
                    if (frame_count <= 10) {
                        ESP_LOGW(TAG, "Frame #%lu - CPU fallback copy (DMA failed)", frame_count);
                        ESP_LOGI(TAG, "                CPU time: %lld us", ppa_end - ppa_start);
                    }
                }
            }
            #endif // SKIP_MEMCPY_TEST
            
            #if 0  // DISABLED: ORIGINAL PPA PATH (not used for testing)
            if (ppa_client && scaled_buffer) {
                // Keep actual buffer dimensions (1288×728 as sensor outputs)
                const int output_width = CAMERA_HRES;   // 1288 (actual buffer width)
                const int output_height = CAMERA_VRES;  // 728 (actual buffer height)

                // PPA configuration: PASSTHROUGH - No rotation, no scaling
                ppa_srm_oper_config_t srm_config = {
                    .in = {
                        .buffer = frame_trans.buffer,
                        .pic_w = CAMERA_HRES,  // 1288 (actual sensor width)
                        .pic_h = CAMERA_VRES,  // 728 (actual sensor height)
                        .block_w = CAMERA_HRES,
                        .block_h = CAMERA_VRES,
                        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                    },
                    .out = {
                        .buffer = scaled_buffer,
                        .buffer_size = scaled_buffer_size,
                        .pic_w = output_width,      // 1288 (no change)
                        .pic_h = output_height,     // 728 (no change)
                        .block_offset_x = 0,
                        .block_offset_y = 0,
                        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                    },
                    .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,  // NO rotation
                    .scale_x = 1.0f,  // No scaling (passthrough)
                    .scale_y = 1.0f,  // No scaling (passthrough)
                    .mirror_x = false,
                    .mirror_y = false,
                    .rgb_swap = false,
                    .byte_swap = false,
                    .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
                    .mode = PPA_TRANS_MODE_NON_BLOCKING,  // Non-blocking to avoid deadlock
                    .user_data = NULL,
                };
                
                // DEBUG: Log before PPA call
                if (frame_count <= 10) {
                    ESP_LOGI(TAG, "Frame #%lu - Starting PPA operation (non-blocking)...", frame_count);
                }
                
                // Start PPA scaling operation (non-blocking - returns immediately)
                esp_err_t ppa_ret = ppa_do_scale_rotate_mirror(ppa_client, &srm_config);
                
                // DEBUG: Log PPA return
                if (frame_count <= 10) {
                    ESP_LOGI(TAG, "Frame #%lu - PPA call returned: %s", 
                            frame_count, esp_err_to_name(ppa_ret));
                }
                
                if (ppa_ret == ESP_OK) {
                    // DEBUG: Log before semaphore wait
                    if (frame_count <= 10) {
                        ESP_LOGI(TAG, "Frame #%lu - Waiting for PPA semaphore (100ms timeout)...", frame_count);
                    }
                    
                    // Wait for PPA callback to signal completion
                    BaseType_t sem_result = xSemaphoreTake(ppa_done_sem, pdMS_TO_TICKS(100));
                    
                    // DEBUG: Log semaphore result
                    if (frame_count <= 10) {
                        ESP_LOGI(TAG, "Frame #%lu - Semaphore result: %s", 
                                frame_count, sem_result == pdTRUE ? "SUCCESS" : "TIMEOUT");
                    }
                    
                    if (sem_result == pdTRUE) {
                        // PPA operation complete, sync cache
                        int64_t cache_start = esp_timer_get_time();
                        esp_cache_msync(scaled_buffer, scaled_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                        int64_t cache_end = esp_timer_get_time();
                        
                        int64_t ppa_end = esp_timer_get_time();
                        
                        // Signal that a new frame is ready for display
                        // The LVGL timer callback will handle the actual framebuffer update
                        // This avoids accessing LVGL objects from the camera task
                        frame_ready_for_display = true;
                        
                        if (frame_count <= 10) {
                            ESP_LOGI(TAG, "Frame #%lu - Passthrough complete (%dx%d, no rotation)", 
                                    frame_count, CAMERA_HRES, CAMERA_VRES);
                            ESP_LOGI(TAG, "                PPA time: %lld us (passthrough), Cache: %lld us", 
                                    ppa_end - ppa_start, cache_end - cache_start);
                        }
                        
                        // Log PPA timing for first 10 frames
                        if (frame_count <= 10) {
                            int64_t ppa_time_us = ppa_end - ppa_start;
                            ESP_LOGI(TAG, "Frame #%lu - PPA passthrough: %lld us (no rotation), output: %dx%d", 
                                    frame_count, ppa_time_us, output_width, output_height);
                        }
                    } else {
                        ESP_LOGW(TAG, "PPA timeout waiting for completion");
                    }
                } else {
                    ESP_LOGW(TAG, "PPA operation failed: %s", esp_err_to_name(ppa_ret));
                }
            }
            #endif  // Disabled PPA path
            
            int64_t processing_end = esp_timer_get_time();
            
            // Log detailed timing for first 10 frames
            if (frame_count <= 10) {
                int64_t cache_sync_time_us = cache_sync_end - cache_sync_start;
                int64_t total_processing_us = processing_end - processing_start;
                
                ESP_LOGI(TAG, "Frame #%lu timing - Cache sync: %lld us, Total: %lld us", 
                        frame_count, cache_sync_time_us, total_processing_us);
            }
            
            // Yield to prevent watchdog timeout (allows idle task to run)
            vTaskDelay(pdMS_TO_TICKS(1));
            
            // Reset watchdog for this task
            esp_task_wdt_reset();
            
            // Log comprehensive frame rate info every second
            TickType_t now = xTaskGetTickCount();
            if ((now - last_log_time) >= pdMS_TO_TICKS(1000)) {
                UBaseType_t sem_count = uxSemaphoreGetCount(frame_ready_sem);
                ESP_LOGI(TAG, "📊 Status: %lu fps | Callbacks: cam=%lu ppa=%lu | Dropped: %lu | LVGL fails: %lu | Queue: %u", 
                        frame_count, frame_count, ppa_callback_count, 
                        semaphore_overflow_count, lvgl_lock_timeout_count, sem_count);
                frame_count = 0;
                last_log_time = now;
            }
    }
    
    ESP_LOGI(TAG, "Preview task ended");
    
    // Unsubscribe from watchdog before exiting
    esp_task_wdt_delete(NULL);
    
    preview_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * Start camera preview
 */
esp_err_t camera_start(void)
{
    if (preview_running) {
        ESP_LOGW(TAG, "Camera already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting camera...");
    
    // Initialize hardware if not done
    if (!camera_initialized) {
        ESP_LOGI(TAG, "Initializing camera hardware (I2C bus and control pins)...");
        ESP_RETURN_ON_ERROR(bsp_camera_init(NULL, &i2c_bus), TAG, "Camera hardware init failed");
        ESP_LOGI(TAG, "Camera hardware initialized, I2C bus ready for sensor");
        camera_initialized = true;
    }
    
    // Allocate frame buffer
    if (!frame_buffer) {
        // Calculate size and align to cache line size (128 bytes for ESP32-P4)
        size_t raw_size = CAMERA_HRES * CAMERA_VRES * RGB565_BITS_PER_PIXEL / 8;
        frame_buffer_size = (raw_size + 127) & ~127;  // Align to 128 bytes
        ESP_LOGI(TAG, "Allocating frame buffer: %zu bytes (raw: %zu)", frame_buffer_size, raw_size);
        
        // Allocate in PSRAM (tested and validated at 30.1 FPS)
        // Note: Internal DMA RAM (~221KB) is insufficient for frame buffers (need 1.8MB+)
        // PSRAM @ 200MHz provides excellent performance (cache sync ~8µs)
        frame_buffer = heap_caps_aligned_calloc(128, 1, frame_buffer_size, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer");
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGI(TAG, "Frame buffer allocated in PSRAM");
        
        // Initialize to white (like the example) - will be overwritten by camera data
        memset(frame_buffer, 0xFF, frame_buffer_size);
        
        // Sync cache (required for ESP32-P4)
        esp_cache_msync((void *)frame_buffer, frame_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        
        frame_trans.buffer = frame_buffer;
        frame_trans.buflen = frame_buffer_size;
    }
    
    // Allocate rotated buffer for PPA output (rotation only, no scaling)
    // After 90° rotation: 1288x728 → 728x1288
    // LVGL will handle scaling this to fit 480x800 display
    if (!scaled_buffer) {
        // Camera outputs CAMERA_HRES×CAMERA_VRES, keep as-is (no rotation)
        const uint32_t buffer_width = CAMERA_HRES;
        const uint32_t buffer_height = CAMERA_VRES;
        size_t raw_buffer_size = buffer_width * buffer_height * RGB565_BITS_PER_PIXEL / 8;
        scaled_buffer_size = (raw_buffer_size + 127) & ~127;  // Align to 128 bytes
        ESP_LOGI(TAG, "Allocating passthrough buffer (no PPA rotation): %zu bytes (%lux%lu)", 
                 scaled_buffer_size, buffer_width, buffer_height);
        
        scaled_buffer = heap_caps_aligned_calloc(128, 1, scaled_buffer_size, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled_buffer) {
            ESP_LOGE(TAG, "Failed to allocate passthrough buffer");
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGI(TAG, "Passthrough buffer allocated in PSRAM (testing 2-lane bandwidth, LVGL will scale)");
        memset(scaled_buffer, 0xFF, scaled_buffer_size);
        esp_cache_msync(scaled_buffer, scaled_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    
    // Initialize PPA if not done (using camera_ppa module)
    if (!ppa_client) {
        ESP_RETURN_ON_ERROR(camera_ppa_init(), TAG, "PPA initialization failed");
    }
    
    // Initialize DMA for fast PSRAM memcpy (using camera_dma module)
    if (!dma_handle) {
        ESP_RETURN_ON_ERROR(camera_dma_init(), TAG, "DMA initialization failed");
    }
    
    // Create semaphore for frame ready notifications
    // Use counting semaphore (not binary) to handle multiple frame callbacks
    // that arrive faster than we can process them (camera @ 30fps, PPA takes ~47ms)
    if (!frame_ready_sem) {
        frame_ready_sem = xSemaphoreCreateCounting(10, 0);  // Max 10 pending frames, start at 0
        if (!frame_ready_sem) {
            ESP_LOGE(TAG, "Failed to create frame ready semaphore");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Initialize camera sensor if not done
    if (!cam_sensor) {
        ESP_RETURN_ON_ERROR(camera_sensor_init(), TAG, "Camera sensor init failed");
    }
    
    // Start sensor streaming
    ESP_RETURN_ON_ERROR(camera_sensor_start_stream(), TAG, "Failed to start sensor streaming");
    
    // Initialize camera controller
    ESP_RETURN_ON_ERROR(camera_controller_init(), TAG, "Camera controller init failed");
    
    // Start camera controller
    ESP_RETURN_ON_ERROR(camera_controller_start(), TAG, "Camera controller start failed");
    
    // Initialize ISP processor (MANDATORY - ESP32-P4 requires ISP in pipeline)
    ESP_RETURN_ON_ERROR(init_isp_processor(), TAG, "ISP processor init failed");
    
    // Initialize LVGL display
    ESP_RETURN_ON_ERROR(camera_display_init(), TAG, "Preview display init failed");
    
    ESP_LOGI(TAG, "Camera controller started, creating preview task");
    
    // Start preview task - it will handle receive() in blocking loop (ESP-IDF example pattern)
    preview_running = true;
    BaseType_t task_ret = xTaskCreate(
        preview_task,
        "camera_preview",
        8192,
        NULL,
        tskIDLE_PRIORITY + 3,
        &preview_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create preview task");
        preview_running = false;
        camera_controller_stop();
        return ESP_FAIL;
    }
    
    // Start display update timer (runs in LVGL thread context)
    esp_err_t timer_ret = camera_display_start_timer();
    if (timer_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start display update timer");
        preview_running = false;
        camera_controller_stop();
        vTaskDelete(preview_task_handle);
        return timer_ret;
    }
    
    // Note: Task will call esp_cam_ctlr_receive() in its loop (like the example)
    ESP_LOGI(TAG, "Camera started successfully");
    return ESP_OK;
}

/**
 * Stop camera preview
 */
esp_err_t camera_stop(void)
{
    if (!preview_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping camera...");
    
    // Stop sensor stream
    if (cam_sensor) {
        camera_sensor_stop_stream();
    }
    
    // Stop camera controller
    if (cam_handle) {
        camera_controller_stop();
    }
    
    // Stop display update timer
    camera_display_stop_timer();
    
    // Signal task to stop
    preview_running = false;
    
    // Wake up task
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
    
    ESP_LOGI(TAG, "Camera stopped");
    return ESP_OK;
}

/**
 * Check if camera is running
 */
bool camera_is_running(void)
{
    return preview_running;
}

/**
 * Set parent object for camera preview canvas
 */
void camera_set_parent(lv_obj_t *parent)
{
    preview_parent = parent;
    camera_display_set_parent(parent);
    ESP_LOGI(TAG, "Preview parent set to %p", parent);
}

/**
 * Cleanup resources
 */
void camera_deinit(void)
{
    ESP_LOGI(TAG, "Cleaning up camera resources...");
    
    camera_stop();
    
    // Disable and delete AWB controller
    if (awb_ctrl) {
        esp_isp_awb_controller_disable(awb_ctrl);
        esp_isp_del_awb_controller(awb_ctrl);
        awb_ctrl = NULL;
    }
    
    // Disable and delete ISP processor (includes Demosaic, CCM, color modules)
    if (isp_proc) {
        esp_isp_demosaic_disable(isp_proc);
        esp_isp_ccm_disable(isp_proc);
        esp_isp_color_disable(isp_proc);
        esp_isp_disable(isp_proc);
        esp_isp_del_processor(isp_proc);
        isp_proc = NULL;
    }
    
    // Disable and delete camera controller
    if (cam_handle) {
        esp_cam_ctlr_disable(cam_handle);
        esp_cam_ctlr_del(cam_handle);
        cam_handle = NULL;
    }
    
    // Unregister PPA client
    if (ppa_client) {
        ppa_unregister_client(ppa_client);
        ppa_client = NULL;
    }
    
    // Delete PPA semaphore
    if (ppa_done_sem) {
        vSemaphoreDelete(ppa_done_sem);
        ppa_done_sem = NULL;
    }
    
    // Clean up display resources
    camera_display_deinit();
    
    // Free frame buffers
    if (frame_buffer) {
        heap_caps_free(frame_buffer);
        frame_buffer = NULL;
    }
    
    if (scaled_buffer) {
        heap_caps_free(scaled_buffer);
        scaled_buffer = NULL;
    }
    
    // Delete semaphore
    if (frame_ready_sem) {
        vSemaphoreDelete(frame_ready_sem);
        frame_ready_sem = NULL;
    }
    
    // CRITICAL: Clear sensor pointer so it gets re-initialized on next launch
    // Without this, second launch skips sensor init and tries to stream on cleaned-up sensor
    cam_sensor = NULL;
    
    camera_initialized = false;
    preview_parent = NULL;
    
    ESP_LOGI(TAG, "Camera cleanup completed");
}