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
#include "camera_task.h"

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

// Timing measurement variables (use TickType_t for ISR-safe timing)
TickType_t last_callback_time = 0;
int64_t last_frame_time_us = 0;

// Diagnostic counters for debugging freeze issue
uint32_t ppa_callback_count = 0;
uint32_t semaphore_overflow_count = 0;
uint32_t lvgl_lock_timeout_count = 0;

/**
 * Start camera preview
 */
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
    ESP_RETURN_ON_ERROR(camera_isp_init(), TAG, "ISP processor init failed");
    
    // Initialize LVGL display
    ESP_RETURN_ON_ERROR(camera_display_init(), TAG, "Preview display init failed");
    
    ESP_LOGI(TAG, "Camera controller started, starting preview task and display timer");
    
    // Start preview task (handles frame reception and DMA transfer)
    preview_running = true;
    ESP_RETURN_ON_ERROR(camera_task_start(), TAG, "Failed to start preview task");
    
    // Start display update timer (runs in LVGL thread context)
    ESP_RETURN_ON_ERROR(camera_display_start_timer(), TAG, "Failed to start display timer");
    
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
    
    // Signal and stop preview task
    preview_running = false;
    camera_task_stop();
    
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