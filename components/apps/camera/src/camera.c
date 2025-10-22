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

// Forward declarations
static esp_err_t init_camera_controller(void);

/**
 * Camera callback: Provide new buffer for next frame
 * 
 * This callback is called by the camera controller to get a buffer for the next frame.
 * We provide our pre-allocated frame buffer which the ISP will write into.
 */
static bool camera_get_new_vb(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    // Dereference to copy the structure by value (matching reference example)
    esp_cam_ctlr_trans_t new_trans = *(esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = new_trans.buffer;
    trans->buflen = new_trans.buflen;

    // Return true to indicate we successfully provided a buffer
    // Returning false would tell the driver we couldn't provide a buffer,
    // which could cause it to stop requesting frames!
    return true;
}

/**
 * Camera callback: Frame reception finished
 * 
 * This callback is fired by the camera controller when a frame is ready.
 * We signal the preview task and return true to release the buffer back to the driver.
 * 
 * Frame skipping: Camera captures at 30 FPS but PPA can only process at 13 FPS.
 * To prevent queue buildup, we skip frames (process every 2nd frame = ~15 FPS target).
 */
static bool camera_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Use system tick count (safe in ISR) - will measure in task
    last_callback_time = xTaskGetTickCountFromISR();
    
    // NO FRAME SKIPPING: With PPA rotation-only (~5-10ms), should handle all 30 FPS
    // Log first 20 frames, then every 30th to monitor performance
    if (callback_count <= 20 || callback_count % 30 == 0) {
        ESP_EARLY_LOGI(TAG, "Frame callback #%lu (processing all frames, PPA rotation-only)", callback_count);
    }
    
    // Signal the preview task that a new frame is ready for processing
    BaseType_t result = pdFAIL;
    if (frame_ready_sem) {
        result = xSemaphoreGiveFromISR(frame_ready_sem, &high_task_woken);
        
        // Track semaphore overflow (semaphore full, frame dropped)
        if (result == pdFAIL) {
            semaphore_overflow_count++;
            if (semaphore_overflow_count % 10 == 0) {
                ESP_EARLY_LOGW(TAG, "⚠️ Frame semaphore FULL! Dropped %lu frames total", semaphore_overflow_count);
            }
        }
    }
    
    // IMPORTANT: Return true to release buffer back to camera controller
    // The ISP outputs to our pre-allocated buffer, so the trans buffer is just metadata
    // and can be reused immediately. Returning false would block the driver!
    return true;
}

/**
 * Initialize CSI camera controller
 */
static esp_err_t init_camera_controller(void)
{
    ESP_LOGI(TAG, "Initializing CSI camera controller...");
    
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = CAMERA_HRES,
        .v_res = CAMERA_VRES,
        .lane_bit_rate_mbps = CAMERA_LANE_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,  // OV02C10 outputs RAW10
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,  // ISP converts RAW10 → RGB565
        .data_lane_num = CAMERA_DATA_LANES,
        .byte_swap_en = false,
        .queue_items = 2,  // INCREASED: Test if buffer queue depth affects frame rate
    };
    
    // ═══════════════════════════════════════════════════════════════════════════
    // 🔍 DETAILED CSI/ISP TIMING DIAGNOSTICS
    // ═══════════════════════════════════════════════════════════════════════════
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           CSI CONTROLLER CONFIGURATION                       ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  Resolution:        %d × %d pixels", CAMERA_HRES, CAMERA_VRES);
    ESP_LOGI(TAG, "  Frame size:        %d bytes (RGB565)", CAMERA_HRES * CAMERA_VRES * 2);
    ESP_LOGI(TAG, "  Data lanes:        %d", CAMERA_DATA_LANES);
    ESP_LOGI(TAG, "  Lane bit rate:     %d Mbps per lane", CAMERA_LANE_BITRATE_MBPS);
    ESP_LOGI(TAG, "  Total bandwidth:   %d Mbps (%d lanes × %d Mbps)", 
             CAMERA_LANE_BITRATE_MBPS * CAMERA_DATA_LANES, 
             CAMERA_DATA_LANES, 
             CAMERA_LANE_BITRATE_MBPS);
    ESP_LOGI(TAG, "  Input format:      RAW10 (10 bits per pixel)");
    ESP_LOGI(TAG, "  Output format:     RGB565 (16 bits per pixel, ISP Demosaic)");
    ESP_LOGI(TAG, "");
    
    // Calculate expected frame delivery time
    uint32_t raw10_frame_bits = CAMERA_HRES * CAMERA_VRES * 10;  // 10 bits per pixel
    uint32_t total_bandwidth_bps = (uint64_t)CAMERA_LANE_BITRATE_MBPS * CAMERA_DATA_LANES * 1000000ULL;
    uint32_t expected_frame_time_us = ((uint64_t)raw10_frame_bits * 1000000ULL) / total_bandwidth_bps;
    float expected_fps = 1000000.0f / expected_frame_time_us;
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           THEORETICAL PERFORMANCE                            ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  RAW10 frame size:  %d bits", raw10_frame_bits);
    ESP_LOGI(TAG, "  Expected transfer: %d μs per frame", expected_frame_time_us);
    ESP_LOGI(TAG, "  Expected FPS:      %.1f FPS (MIPI bandwidth only)", expected_fps);
    ESP_LOGI(TAG, "  Sensor configured: ~27 FPS (HTS=2280, VTS=1164, 81.67MHz)");
    ESP_LOGI(TAG, "  ⚠️  ACTUAL FPS:    ~8.3 FPS (120ms intervals) ← MYSTERY!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  🎯 Goal: Find where the 120ms frame interval comes from");
    ESP_LOGI(TAG, "");
    
    ESP_RETURN_ON_ERROR(
        esp_cam_new_csi_ctlr(&csi_config, &cam_handle),
        TAG, "Failed to create CSI controller"
    );
    
    // Register callbacks
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = camera_get_new_vb,
        .on_trans_finished = camera_trans_finished,
    };
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_register_event_callbacks(cam_handle, &cbs, &frame_trans),
        TAG, "Failed to register camera callbacks"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_enable(cam_handle),
        TAG, "Failed to enable camera controller"
    );
    
    ESP_LOGI(TAG, "CSI camera controller initialized");
    
    return ESP_OK;
}

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
static esp_err_t init_preview_display(void)
{
    ESP_LOGI(TAG, "Initializing LVGL canvas preview...");
    
    if (!preview_parent) {
        ESP_LOGE(TAG, "No preview parent set");
        return ESP_FAIL;
    }
    
    display = lv_display_get_default();
    if (!display) {
        ESP_LOGE(TAG, "No LVGL display found");
        return ESP_FAIL;
    }
    
    // Get display size (Brookesia may return useable area, not full display)
    // Use display constants from camera_internal.h
    // DISPLAY_WIDTH = 480, DISPLAY_HEIGHT = 800 (full display including status bar)
    // STATUS_BAR_HEIGHT = 40 (defined in camera_internal.h)
    const int useable_height = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT; // 800 - 40 = 760
    
    // Camera outputs CAMERA_HRES×CAMERA_VRES (no rotation, landscape)
    // We need to scale this to fit within 480x760 useable area
    // Calculate zoom factor (LVGL uses 256 = 100%)
    const int PPA_OUTPUT_WIDTH = CAMERA_HRES;   // No rotation (testing bandwidth)
    const int PPA_OUTPUT_HEIGHT = CAMERA_VRES;
    
    float scale_x = (float)DISPLAY_WIDTH / PPA_OUTPUT_WIDTH;
    float scale_y = (float)useable_height / PPA_OUTPUT_HEIGHT;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;  // Use smaller to fit both
    
    // Convert to LVGL zoom (256 = 100%)
    uint16_t lvgl_zoom = (uint16_t)(scale * 256);
    
    // Calculate final displayed size
    int display_width_final = (int)(PPA_OUTPUT_WIDTH * scale);
    int display_height_final = (int)(PPA_OUTPUT_HEIGHT * scale);
    
    // Center horizontally and vertically
    int img_x = (DISPLAY_WIDTH - display_width_final) / 2;
    int img_y = STATUS_BAR_HEIGHT + (useable_height - display_height_final) / 2;
    
    ESP_LOGI(TAG, "Display: %dx%d (useable: %dx%d), PPA output: %dx%d (no rotation), LVGL zoom: %u (scale: %.3f)", 
             DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, useable_height,
             PPA_OUTPUT_WIDTH, PPA_OUTPUT_HEIGHT, lvgl_zoom, scale);
    ESP_LOGI(TAG, "Final display size: %dx%d at (%d,%d)", 
             display_width_final, display_height_final, img_x, img_y);
    
    // Create LVGL image widget for camera preview (hardware-accelerated scaling)
    // Using lv_img instead of lv_canvas allows LVGL to use hardware 2D-DMA for scaling
    preview_canvas = lv_img_create(preview_parent);
    if (!preview_canvas) {
        ESP_LOGE(TAG, "Failed to create LVGL image widget");
        return ESP_FAIL;
    }
    
    // Set image position and pivot point (for zoom)
    lv_obj_set_pos(preview_canvas, img_x, img_y);
    lv_img_set_pivot(preview_canvas, 0, 0);  // Zoom from top-left corner
    lv_img_set_zoom(preview_canvas, lvgl_zoom);  // Apply zoom factor
    lv_obj_clear_flag(preview_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    ESP_LOGI(TAG, "LVGL image widget created with zoom=%u (%.1f%%), will scale PPA output to fit display", 
             lvgl_zoom, (float)lvgl_zoom / 256 * 100);
    ESP_LOGI(TAG, "Image widget positioned at (%d,%d), final size: %dx%d", 
             img_x, img_y, display_width_final, display_height_final);
    
    return ESP_OK;
}

/**
 * LVGL timer callback: Updates display framebuffer from LVGL thread
 * 
 * This runs in the LVGL thread context (safe to access LVGL objects)
 * The camera task signals when a new frame is ready via frame_ready_for_display flag
 * 
 * Uses lv_img widget with direct buffer source for hardware-accelerated scaling
 */
static void display_update_timer_cb(lv_timer_t *timer)
{
    // Check if camera is still running
    if (!preview_running) {
        return;
    }
    
    // Check if new frame is available
    if (!frame_ready_for_display || !scaled_buffer || !display) {
        return;
    }
    
    // Clear flag immediately to avoid re-processing same frame
    frame_ready_for_display = false;
    
    static uint32_t update_count = 0;
    update_count++;
    
    // NO cache sync needed here! DMA writes directly to PSRAM, LVGL reads from PSRAM
    // Cache sync is only needed when CPU writes to PSRAM (not for DMA writes)
    
    // Create image descriptor for direct buffer access
    // Camera outputs CAMERA_HRES×CAMERA_VRES (no rotation - testing bandwidth)
    static lv_image_dsc_t img_dsc;
    img_dsc.header.w = CAMERA_HRES;
    img_dsc.header.h = CAMERA_VRES;
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    img_dsc.header.stride = CAMERA_HRES * 2;  // Bytes per row
    img_dsc.data_size = CAMERA_HRES * CAMERA_VRES * 2;
    img_dsc.data = (const uint8_t *)scaled_buffer;
    
    // Update image source - LVGL will handle scaling via zoom property
    lv_img_set_src(preview_canvas, &img_dsc);
    
    if (update_count <= 10) {
        ESP_LOGI(TAG, "Display update #%lu - Image source updated (%dx%d passthrough, LVGL zoom active)", 
                 update_count, CAMERA_HRES, CAMERA_VRES);
    }
}

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
                uint16_t *pixels = (uint16_t *)frame_trans.buffer;
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
    esp_err_t ret;
    
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
        ESP_LOGI(TAG, "Initializing OV02C10 camera sensor...");
        
        // Create SCCB I2C handle for sensor communication
        esp_sccb_io_handle_t sccb_io_handle = NULL;
        sccb_i2c_config_t i2c_config = {
            .scl_speed_hz = 100000,  // 100kHz for SCCB
            .device_address = OV02C10_SCCB_ADDR,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        ret = sccb_new_i2c_io(i2c_bus, &i2c_config, &sccb_io_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SCCB I2C handle: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Configure and detect OV02C10 sensor
        esp_cam_sensor_config_t sensor_config = {
            .sccb_handle = sccb_io_handle,
            .reset_pin = -1,  // Reset handled by BSP
            .pwdn_pin = -1,   // Power down handled by BSP
            .xclk_pin = -1,   // XCLK not used (sensor has internal clock)
            .xclk_freq_hz = 0,
            .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
        };
        
        cam_sensor = ov02c10_detect(&sensor_config);
        if (!cam_sensor) {
            ESP_LOGE(TAG, "Failed to detect OV02C10 sensor");
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "OV02C10 sensor detected successfully");
        
        // Configure sensor for target resolution
        esp_cam_sensor_format_array_t formats;
        cam_sensor->ops->query_support_formats(cam_sensor, &formats);
        
        // Log all available formats
        ESP_LOGI(TAG, "Sensor supports %d formats:", formats.count);
        for (uint32_t i = 0; i < formats.count; i++) {
            ESP_LOGI(TAG, "  [%d] %s: %dx%d @ %d fps", 
                    i, 
                    formats.format_array[i].name,
                    formats.format_array[i].width, 
                    formats.format_array[i].height, 
                    formats.format_array[i].fps);
        }
        
        // Find matching format (resolution and lane count)
        const esp_cam_sensor_format_t *target_format = NULL;
        const char *lane_keyword = (CAMERA_DATA_LANES == 2) ? "2lane" : "1lane";
        
        for (uint32_t i = 0; i < formats.count; i++) {
            if (formats.format_array[i].width == CAMERA_HRES &&
                formats.format_array[i].height == CAMERA_VRES) {
                // Check if lane count matches
                if (strstr(formats.format_array[i].name, lane_keyword)) {
                    target_format = &formats.format_array[i];
                    ESP_LOGI(TAG, "Selected format: %s (%dx%d @ %d fps)",
                            target_format->name,
                            target_format->width, target_format->height, target_format->fps);
                    break;
                } else if (!target_format) {
                    // Fallback to first matching resolution if no lane-specific format found
                    target_format = &formats.format_array[i];
                }
            }
        }
        
        if (target_format && !strstr(target_format->name, lane_keyword)) {
            ESP_LOGW(TAG, "Could not find %s format, using: %s", lane_keyword, target_format->name);
        }
        
        if (!target_format) {
            ESP_LOGE(TAG, "Sensor does not support %dx%d resolution", CAMERA_HRES, CAMERA_VRES);
            return ESP_ERR_NOT_SUPPORTED;
        }
        
        // Set the format
        ret = cam_sensor->ops->set_format(cam_sensor, target_format);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set sensor format: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Sensor format set successfully");
        
        // CRITICAL: Stop sensor streaming if it's already running (first boot scenario)
        // On first boot, sensor hardware may be in unknown state (powered on by bootloader
        // or residual state from previous power cycle). Explicitly stop to ensure clean state.
        int stream_off = 0;
        ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_off);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Sensor stream stopped (ensuring clean state on first init)");
        } else {
            ESP_LOGW(TAG, "Sensor stream stop returned: %s (may already be stopped)", esp_err_to_name(ret));
        }
    }
    
    // Start sensor streaming (MUST be outside the init block to support restart)
    // This ensures the stream is started on every camera_start(), not just first init
    int stream_on = 1;
    ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor streaming: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Sensor stream started");
    
    // Initialize camera controller
    ESP_RETURN_ON_ERROR(init_camera_controller(), TAG, "Camera controller init failed");
    
    // Initialize ISP processor (MANDATORY - ESP32-P4 requires ISP in pipeline)
    ESP_RETURN_ON_ERROR(init_isp_processor(), TAG, "ISP processor init failed");
    
    // Initialize LVGL display
    ESP_RETURN_ON_ERROR(init_preview_display(), TAG, "Preview display init failed");
    
    // Start camera controller streaming (MUST be before starting task)
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_start(cam_handle),
        TAG, "Failed to start camera controller"
    );
    
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
        esp_cam_ctlr_stop(cam_handle);
        return ESP_FAIL;
    }
    
    // Create LVGL timer to update display (runs in LVGL thread context)
    // This safely handles image source updates without cross-thread LVGL access
    // Set to 30 FPS (33ms) - PPA rotation-only should easily handle this
    bsp_display_lock(0);
    update_timer = lv_timer_create(display_update_timer_cb, 33, NULL);  // 30 FPS target
    if (!update_timer) {
        ESP_LOGE(TAG, "Failed to create display update timer");
        bsp_display_unlock();
        preview_running = false;
        esp_cam_ctlr_stop(cam_handle);
        vTaskDelete(preview_task_handle);
        return ESP_FAIL;
    }
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Display update timer created (33ms interval, 30 FPS target)");
    
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
    
    // Stop sensor streaming FIRST (critical for clean restart)
    if (cam_sensor) {
        int stream_off = 0;
        esp_err_t ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_off);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Sensor stream stopped");
        } else {
            ESP_LOGW(TAG, "Failed to stop sensor stream: %s", esp_err_to_name(ret));
        }
    }
    
    // Stop camera controller
    if (cam_handle) {
        esp_cam_ctlr_stop(cam_handle);
    }
    
    // Delete LVGL update timer
    if (update_timer) {
        bsp_display_lock(0);
        lv_timer_delete(update_timer);
        update_timer = NULL;
        bsp_display_unlock();
        ESP_LOGI(TAG, "Display update timer deleted");
    }
    
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
    
    // Clean up display reference
    display = NULL;
    preview_canvas = NULL;
    
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