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
#include "driver/i2c_master.h"

// Camera Sensor API
#include "esp_cam_sensor.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "ov02c10.h"

// LVGL for display
#include "lvgl.h"

// Local includes
#include "camera.h"

static const char *TAG = "camera";

// Camera configuration (from sdkconfig.defaults)
// OV02C10: 1288x728 @ 30fps, 1-lane MIPI CSI, 400Mbps/lane (manufacturer config)
#define CAMERA_HRES 1288
#define CAMERA_VRES 728
#define CAMERA_LANE_BITRATE_MBPS 400
#define CAMERA_DATA_LANES 1

// Frame buffer configuration
#define CAMERA_FRAME_BUFFER_COUNT 1  // Single frame buffer for LVGL integration
#define RGB565_BITS_PER_PIXEL 16

// ISP clock (80MHz is recommended for ESP32-P4)
#define ISP_CLK_HZ (80 * 1000 * 1000)

// Global state
static bool camera_initialized = false;
static bool preview_running = false;
static SemaphoreHandle_t frame_ready_sem = NULL;

// Camera Controller and ISP handles
static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t isp_proc = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_cam_sensor_device_t *cam_sensor = NULL;

// LVGL display objects
static lv_obj_t *preview_parent = NULL;
static lv_obj_t *preview_canvas = NULL;
static lv_display_t *display = NULL;

// Frame buffer (used by both Camera Controller and LVGL)
static void *frame_buffer = NULL;
static size_t frame_buffer_size = 0;

// Camera transaction descriptor
static esp_cam_ctlr_trans_t frame_trans = {0};

// Task handle for frame reception
static TaskHandle_t preview_task_handle = NULL;

// Timing measurement variables (use TickType_t for ISR-safe timing)
static TickType_t last_callback_time = 0;
static int64_t last_frame_time_us = 0;

/**
 * Camera callback: Provide new buffer for next frame
 */
static bool camera_get_new_vb(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    // Dereference to copy the structure by value (matching reference example)
    esp_cam_ctlr_trans_t new_trans = *(esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = new_trans.buffer;
    trans->buflen = new_trans.buflen;

    return false;
}

/**
 * Camera callback: Frame reception finished
 */
static bool camera_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Use system tick count (safe in ISR) - will measure in task
    last_callback_time = xTaskGetTickCountFromISR();
    
    // Log first few callbacks for debugging
    if (callback_count <= 10) {
        ESP_EARLY_LOGI(TAG, "Frame callback #%lu triggered", callback_count);
    }
    
    // Signal the preview task that a new frame is ready
    if (frame_ready_sem) {
        xSemaphoreGiveFromISR(frame_ready_sem, &high_task_woken);
    }
    
    return high_task_woken == pdTRUE;
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
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = CAMERA_DATA_LANES,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    
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
 * Initialize ISP processor
 */
static esp_err_t init_isp_processor(void)
{
    ESP_LOGI(TAG, "Initializing ISP processor...");
    
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
    
    ESP_RETURN_ON_ERROR(
        esp_isp_new_processor(&isp_config, &isp_proc),
        TAG, "Failed to create ISP processor"
    );
    
    ESP_RETURN_ON_ERROR(
        esp_isp_enable(isp_proc),
        TAG, "Failed to enable ISP processor"
    );
    
    ESP_LOGI(TAG, "ISP processor initialized");
    return ESP_OK;
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
    
    int32_t display_width = lv_display_get_horizontal_resolution(display);
    int32_t display_height = lv_display_get_vertical_resolution(display);
    
    ESP_LOGI(TAG, "Display: %dx%d, Camera: %dx%d", 
             display_width, display_height, CAMERA_HRES, CAMERA_VRES);
    
    // Create LVGL canvas for camera preview
    preview_canvas = lv_canvas_create(preview_parent);
    if (!preview_canvas) {
        ESP_LOGE(TAG, "Failed to create LVGL canvas");
        return ESP_FAIL;
    }
    
    // Center and fit the canvas on screen
    lv_obj_set_size(preview_canvas, CAMERA_HRES, CAMERA_VRES);
    lv_obj_center(preview_canvas);
    
    ESP_LOGI(TAG, "LVGL canvas created successfully");
    
    return ESP_OK;
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
    
    uint32_t frame_count = 0;
    TickType_t last_log_time = xTaskGetTickCount();
    
    // Use global frame_trans which is set up by callbacks (ISP requires this!)
    
    while (preview_running) {
        // *** ISP-INTEGRATED PATTERN: Pure callback-driven (no manual receive() needed) ***
        // The ISP callbacks handle buffer management automatically
        // We just wait for frame_ready notification from callback
        
        // Wait for callback notification that frame is ready
        if (xSemaphoreTake(frame_ready_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Frame timeout - no callback received in 1 second");
            continue;
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
            
            // Log first few frames with timing
            if (frame_count <= 10) {
                uint16_t *pixels = (uint16_t *)frame_trans.buffer;
                ESP_LOGI(TAG, "Frame #%lu received, first pixels: 0x%04x 0x%04x 0x%04x", 
                        frame_count, pixels[0], pixels[1], pixels[2]);
                
                // Log frame interval for first 10 frames
                if (frame_interval_us > 0) {
                    float fps = 1000000.0f / frame_interval_us;
                    ESP_LOGI(TAG, "Frame #%lu interval: %lld us (%.1f fps)", 
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
            
            // Update LVGL canvas with new frame
            if (preview_canvas) {
                // Acquire LVGL lock for thread-safe access (critical!)
                // This prevents race conditions with LVGL's render task
                lv_lock();
                
                // Update canvas buffer to point to the new frame
                lv_canvas_set_buffer(preview_canvas, frame_trans.buffer, 
                                   CAMERA_HRES, CAMERA_VRES, 
                                   LV_COLOR_FORMAT_RGB565);
                
                // Invalidate to trigger LVGL refresh (non-blocking)
                lv_obj_invalidate(preview_canvas);
                
                // Release LVGL lock immediately
                lv_unlock();
            }
            
            int64_t processing_end = esp_timer_get_time();
            
            // Log detailed timing for first 10 frames
            if (frame_count <= 10) {
                int64_t cache_sync_time_us = cache_sync_end - cache_sync_start;
                int64_t total_processing_us = processing_end - processing_start;
                
                ESP_LOGI(TAG, "Frame #%lu timing - Cache sync: %lld us, Canvas update: %lld us", 
                        frame_count, cache_sync_time_us, total_processing_us);
            }
            
            // Yield to prevent watchdog timeout (allows idle task to run)
            vTaskDelay(pdMS_TO_TICKS(1));
            
            // Log frame rate every second
            TickType_t now = xTaskGetTickCount();
            if ((now - last_log_time) >= pdMS_TO_TICKS(1000)) {
                ESP_LOGI(TAG, "Frame rate: %lu fps", frame_count);
                frame_count = 0;
                last_log_time = now;
            }
    }
    
    ESP_LOGI(TAG, "Preview task ended");
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
        
        // Allocate with 128-byte alignment for cache line alignment
        frame_buffer = heap_caps_aligned_calloc(128, 1, frame_buffer_size, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer");
            return ESP_ERR_NO_MEM;
        }
        
        // Initialize to white (like the example) - will be overwritten by camera data
        memset(frame_buffer, 0xFF, frame_buffer_size);
        
        // Sync cache for SPIRAM buffer (required for ESP32-P4)
        esp_cache_msync((void *)frame_buffer, frame_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        
        frame_trans.buffer = frame_buffer;
        frame_trans.buflen = frame_buffer_size;
    }
    
    // Create semaphore
    if (!frame_ready_sem) {
        frame_ready_sem = xSemaphoreCreateBinary();
        if (!frame_ready_sem) {
            ESP_LOGE(TAG, "Failed to create semaphore");
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
        esp_err_t ret = sccb_new_i2c_io(i2c_bus, &i2c_config, &sccb_io_handle);
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
        
        // Configure sensor for 1920x1080 @ 30fps
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
        
        // Find 1920x1080 2-lane format (prefer 2-lane over 1-lane for better bandwidth)
        const esp_cam_sensor_format_t *target_format = NULL;
        for (uint32_t i = 0; i < formats.count; i++) {
            if (formats.format_array[i].width == CAMERA_HRES &&
                formats.format_array[i].height == CAMERA_VRES) {
                // Check if this is the 2-lane format (contains "2lane" in name)
                if (strstr(formats.format_array[i].name, "2lane")) {
                    target_format = &formats.format_array[i];
                    ESP_LOGI(TAG, "Selected 2-lane format: %s (%dx%d @ %d fps)",
                            target_format->name,
                            target_format->width, target_format->height, target_format->fps);
                    break;
                } else if (!target_format) {
                    // Fallback to first matching resolution if no 2-lane found
                    target_format = &formats.format_array[i];
                }
            }
        }
        
        if (target_format && !strstr(target_format->name, "2lane")) {
            ESP_LOGW(TAG, "Using 1-lane format as fallback: %s (%dx%d @ %d fps)",
                    target_format->name,
                    target_format->width, target_format->height, target_format->fps);
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
        
        // Start sensor streaming
        int stream_on = 1;
        ret = cam_sensor->ops->priv_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start sensor streaming: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Sensor stream started - OV02C10 configured and streaming");
    }
    
    // Initialize camera controller
    ESP_RETURN_ON_ERROR(init_camera_controller(), TAG, "Camera controller init failed");
    
    // Initialize ISP processor
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
    
    // Stop camera controller
    if (cam_handle) {
        esp_cam_ctlr_stop(cam_handle);
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
    
    // Disable and delete ISP processor
    if (isp_proc) {
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
    
    // Clean up display reference
    display = NULL;
    preview_canvas = NULL;
    
    // Free frame buffer
    if (frame_buffer) {
        heap_caps_free(frame_buffer);
        frame_buffer = NULL;
    }
    
    // Delete semaphore
    if (frame_ready_sem) {
        vSemaphoreDelete(frame_ready_sem);
        frame_ready_sem = NULL;
    }
    
    camera_initialized = false;
    preview_parent = NULL;
    
    ESP_LOGI(TAG, "Camera cleanup completed");
}