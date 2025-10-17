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
// PRODUCTION: OV02C10 1-lane 1288x728 - stable 30.1 FPS (validated)
// Frame buffer size: 1288x728x2 = 1,875,968 bytes (~1.8MB)
#define CAMERA_HRES 1288
#define CAMERA_VRES 728
#define CAMERA_LANE_BITRATE_MBPS 400
#define CAMERA_DATA_LANES 1

// // Testing: 1920x1080 maxes at 11.1 FPS (blanking limited)
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

// Global state
static bool camera_initialized = false;
static bool preview_running = false;
static SemaphoreHandle_t frame_ready_sem = NULL;

// Camera Controller and ISP handles
static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t isp_proc = NULL;
static isp_awb_ctlr_t awb_ctrl = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_cam_sensor_device_t *cam_sensor = NULL;

// PPA (Pixel Processing Accelerator) for scaling
static ppa_client_handle_t ppa_client = NULL;
static SemaphoreHandle_t ppa_done_sem = NULL;

// LVGL display objects
static lv_obj_t *preview_parent = NULL;
static lv_obj_t *preview_canvas = NULL;
static lv_display_t *display = NULL;

// Frame buffers
static void *frame_buffer = NULL;       // Camera output buffer (1288x728)
static size_t frame_buffer_size = 0;
static void *scaled_buffer = NULL;      // Scaled buffer for display (480x800)
static size_t scaled_buffer_size = 0;

// Camera transaction descriptor
static esp_cam_ctlr_trans_t frame_trans = {0};

// Task handle for frame reception
static TaskHandle_t preview_task_handle = NULL;

// Timing measurement variables (use TickType_t for ISR-safe timing)
static TickType_t last_callback_time = 0;
static int64_t last_frame_time_us = 0;

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
 */
static bool camera_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Use system tick count (safe in ISR) - will measure in task
    last_callback_time = xTaskGetTickCountFromISR();
    
    // Log callbacks to debug why they stop after frame 10
    // Log first 20 frames, then every 10th
    if (callback_count <= 20 || callback_count % 10 == 0) {
        ESP_EARLY_LOGI(TAG, "Frame callback #%lu triggered", callback_count);
    }
    
    // Signal the preview task that a new frame is ready
    if (frame_ready_sem) {
        xSemaphoreGiveFromISR(frame_ready_sem, &high_task_woken);
    }
    
    // IMPORTANT: Return true to release buffer back to camera controller
    // The ISP outputs to our pre-allocated buffer, so the trans buffer is just metadata
    // and can be reused immediately. Returning false would block the driver!
    return true;
}

/**
 * PPA callback: Called when scaling operation completes
 */
static bool ppa_transaction_done_cb(ppa_client_handle_t ppa_client, 
                                   ppa_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_woken = pdFALSE;
    
    // DEBUG: Log callback entry
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Signal that PPA scaling is complete
    if (ppa_done_sem) {
        xSemaphoreGiveFromISR(ppa_done_sem, &high_task_woken);
        // Log every 10th callback to avoid spam
        if (callback_count <= 10 || callback_count % 10 == 0) {
            ESP_EARLY_LOGI(TAG, "PPA callback #%lu fired, semaphore signaled", callback_count);
        }
    } else {
        ESP_EARLY_LOGE(TAG, "PPA callback fired but semaphore is NULL!");
    }
    
    return high_task_woken == pdTRUE;
}

/**
 * Initialize PPA (Pixel Processing Accelerator) for scaling
 */
static esp_err_t init_ppa(void)
{
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
    
    // Configure Demosaic module for RAW10 → RGB conversion (Bayer demosaicing)
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
    
    // Configure Color Correction Matrix (CCM) to fix purple tint
    // Identity matrix baseline with adjustments to reduce blue/purple cast
    esp_isp_ccm_config_t ccm_config = {
        .matrix = {
            {1.0,  0.0,  0.0},   // Red channel: pass through
            {0.0,  1.0,  0.0},   // Green channel: pass through
            {0.0,  0.0,  0.75}   // Blue channel: reduce by 15% to counteract purple tint
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
    
    // Configure Automatic White Balance (AWB)
    // Sample after CCM since we're applying CCM corrections
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
    
    // Configure Color Adjustments (brightness, saturation, contrast, hue)
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
    int32_t display_width = lv_display_get_horizontal_resolution(display);
    int32_t display_height = lv_display_get_vertical_resolution(display);
    
    // Use actual display constants for calculations (not Brookesia-adjusted values)
    // DISPLAY_WIDTH = 480, DISPLAY_HEIGHT = 800 (full display including status bar)
    const int STATUS_BAR_HEIGHT = 40;
    const int USEABLE_HEIGHT = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT; // 800 - 40 = 760
    
    // Maintain aspect ratio within useable area
    // Camera: 1288×728 → After 90° rotation: 728×1288
    // Calculate scale to fit within 480×760
    float scale_x_ratio = (float)DISPLAY_WIDTH / CAMERA_VRES;    // 480/728 = 0.659
    float scale_y_ratio = (float)USEABLE_HEIGHT / CAMERA_HRES;   // 760/1288 = 0.590
    float scale = (scale_x_ratio < scale_y_ratio) ? scale_x_ratio : scale_y_ratio;  // 0.590
    
    int canvas_width = (int)(CAMERA_VRES * scale);   // 728 * 0.590 = 429
    int canvas_height = (int)(CAMERA_HRES * scale);  // 1288 * 0.590 = 760
    
    // Center canvas horizontally in the useable area
    int canvas_x = (DISPLAY_WIDTH - canvas_width) / 2;  // (480 - 429) / 2 = 26
    int canvas_y = 0;  // Align to top of useable area
    
    ESP_LOGI(TAG, "Display query: %dx%d, Display constants: %dx%d (useable: %dx%d), Camera: %dx%d, Canvas: %dx%d at (%d,%d)", 
             display_width, display_height, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, USEABLE_HEIGHT,
             CAMERA_HRES, CAMERA_VRES, canvas_width, canvas_height, canvas_x, canvas_y);
    
    // Create LVGL canvas for scaled camera preview
    preview_canvas = lv_canvas_create(preview_parent);
    if (!preview_canvas) {
        ESP_LOGE(TAG, "Failed to create LVGL canvas");
        return ESP_FAIL;
    }
    
    // Set canvas size and position
    lv_obj_set_size(preview_canvas, canvas_width, canvas_height);
    lv_obj_set_pos(preview_canvas, canvas_x, canvas_y);  // Centered horizontally
    // Disable scrolling
    lv_obj_clear_flag(preview_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    ESP_LOGI(TAG, "LVGL canvas created at %dx%d, positioned at (%d,%d), no scrolling",
             canvas_width, canvas_height, canvas_x, canvas_y);
    
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
            ESP_LOGE(TAG, "⚠️  FRAME TIMEOUT after frame #%lu - no callback received in 1 second!", frame_count);
            ESP_LOGE(TAG, "    This means camera callbacks stopped firing!");
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
            
            // Scale frame using PPA to fill display (1288x728 → 480x760, no borders)
            int64_t ppa_start = esp_timer_get_time();
            
            if (ppa_client && scaled_buffer) {
                // Maintain aspect ratio to avoid PPA constraints
                // Camera: 1288×728 (landscape) → After 90° CW rotation: 728×1288 (portrait)
                // Target: Fit within 480×760 (useable display area)
                
                const int STATUS_BAR_HEIGHT = 40;
                const int USEABLE_HEIGHT = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT; // 760
                
                // After 90° rotation: 728×1288 → Need to fit in 480×760
                // Calculate scale to fit both dimensions (maintain aspect ratio)
                float scale_x_ratio = (float)DISPLAY_WIDTH / CAMERA_VRES;    // 480/728 = 0.659
                float scale_y_ratio = (float)USEABLE_HEIGHT / CAMERA_HRES;   // 760/1288 = 0.590
                
                // Use the SMALLER scale to ensure both dimensions fit
                float scale = (scale_x_ratio < scale_y_ratio) ? scale_x_ratio : scale_y_ratio;  // 0.590
                
                // Calculate actual output dimensions (will have borders on one axis)
                int output_width = (int)(CAMERA_VRES * scale);   // 728 * 0.590 = 429
                int output_height = (int)(CAMERA_HRES * scale);  // 1288 * 0.590 = 760

                // PPA configuration with uniform scaling
                ppa_srm_oper_config_t srm_config = {
                    .in = {
                        .buffer = frame_trans.buffer,
                        .pic_w = CAMERA_HRES,  // 1288 (landscape width)
                        .pic_h = CAMERA_VRES,  // 728 (landscape height)
                        .block_w = CAMERA_HRES,
                        .block_h = CAMERA_VRES,
                        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                    },
                    .out = {
                        .buffer = scaled_buffer,
                        .buffer_size = scaled_buffer_size,
                        .pic_w = output_width,      // 429 (maintains aspect ratio)
                        .pic_h = output_height,     // 760 (fits height exactly)
                        .block_offset_x = 0,
                        .block_offset_y = 0,
                        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                    },
                    .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,  // 90° clockwise
                    .scale_x = scale,  // 0.590 (uniform scaling)
                    .scale_y = scale,  // 0.590 (uniform scaling)
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
                        esp_cache_msync(scaled_buffer, scaled_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                        
                        int64_t ppa_end = esp_timer_get_time();
                        
                        // Update LVGL canvas with scaled frame
                        if (preview_canvas) {
                            lv_lock();
                            lv_canvas_set_buffer(preview_canvas, scaled_buffer, 
                                               output_width, output_height,  // 429×760 (aspect ratio preserved)
                                               LV_COLOR_FORMAT_RGB565);
                            lv_obj_invalidate(preview_canvas);
                            lv_unlock();
                        }
                        
                        // Log PPA timing for first 10 frames
                        if (frame_count <= 10) {
                            int64_t ppa_time_us = ppa_end - ppa_start;
                            ESP_LOGI(TAG, "Frame #%lu - PPA: %lld us, scaled to %dx%d (scale=%.3f)", 
                                    frame_count, ppa_time_us, output_width, output_height, scale);
                        }
                    } else {
                        ESP_LOGW(TAG, "PPA timeout waiting for completion");
                    }
                } else {
                    ESP_LOGW(TAG, "PPA operation failed: %s", esp_err_to_name(ppa_ret));
                }
            }
            
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
            
            // Log frame rate every second
            TickType_t now = xTaskGetTickCount();
            if ((now - last_log_time) >= pdMS_TO_TICKS(1000)) {
                ESP_LOGI(TAG, "Frame rate: %lu fps", frame_count);
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
    
    // Allocate scaled buffer for display
    if (!scaled_buffer) {
        size_t raw_scaled_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * RGB565_BITS_PER_PIXEL / 8;
        scaled_buffer_size = (raw_scaled_size + 127) & ~127;  // Align to 128 bytes
        ESP_LOGI(TAG, "Allocating scaled buffer: %zu bytes (raw: %zu)", scaled_buffer_size, raw_scaled_size);
        
        scaled_buffer = heap_caps_aligned_calloc(128, 1, scaled_buffer_size, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled_buffer) {
            ESP_LOGE(TAG, "Failed to allocate scaled buffer");
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGI(TAG, "Scaled buffer allocated in PSRAM");
        memset(scaled_buffer, 0xFF, scaled_buffer_size);
        esp_cache_msync(scaled_buffer, scaled_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    
    // Initialize PPA if not done
    if (!ppa_client) {
        ESP_RETURN_ON_ERROR(init_ppa(), TAG, "PPA initialization failed");
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