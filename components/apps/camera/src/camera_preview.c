/*
 * Camera Preview Application for ESP32-P4 with ESP-IDF 5.5.1
 * 
 * Features:
 * - Uses latest ESP-IDF 5.5.1 APIs
 * - Proper BSP integration with shared I2C bus
 * - V4L2-style video device interface
 * - Real-time video preview display using LVGL
 * - Optimized for JC4880P433C hardware
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

// BSP and Hardware APIs
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"

// Camera and Video APIs
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"
#include "sys/mman.h"

// LVGL for display
#include "lvgl.h"

// Local includes
#include "camera_preview.h"

static const char *TAG = "camera_preview";

// Configuration constants
// OV02C10 sensor supported resolutions: 1288x728 or 1920x1080
// Resolution is set via sdkconfig, we detect it at runtime
#define CAMERA_BUFFER_COUNT 3
#define PREVIEW_UPDATE_RATE_MS 66  // ~15 FPS

// Global state
static bool camera_initialized = false;
static bool preview_running = false;
static bool stream_started = false;  // Track if VIDIOC_STREAMON was called
static TaskHandle_t preview_task_handle = NULL;
static int video_fd = -1;
static lv_obj_t *preview_canvas = NULL;
static lv_obj_t *preview_parent = NULL;  // Parent object for preview canvas
static lv_draw_buf_t *draw_buf = NULL;

// Video configuration (detected at runtime)
static uint32_t camera_width = 0;
static uint32_t camera_height = 0;

// Video buffer management  
static uint8_t *video_buffers[CAMERA_BUFFER_COUNT];
static uint32_t buffer_length = 0;

/**
 * Initialize video subsystem with proper configuration
 */
static esp_err_t init_video_system(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Initializing video system...");
    
    // Initialize BSP I2C (shared bus for GT911 touch and camera)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "BSP I2C init failed");
    
    // Get shared I2C handle from BSP
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (!i2c_handle) {
        ESP_LOGE(TAG, "Failed to get shared I2C handle from BSP");
        return ESP_FAIL;
    }
    
    // Configure video system for MIPI CSI with shared I2C
    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false,  // Use BSP's shared I2C bus
            .i2c_handle = i2c_handle,
            .freq = 400000,  // 400kHz to match BSP configuration
        },
        .reset_pin = -1,   // No dedicated reset pin in this design
        .pwdn_pin = -1,    // No dedicated power down pin
    };
    
    esp_video_init_config_t video_config = {
        .csi = &csi_config,
    };
    
    ESP_RETURN_ON_ERROR(esp_video_init(&video_config), TAG, "Video system init failed");
    
    ESP_LOGI(TAG, "Video system initialized successfully");
    return ret;
}

/**
 * Open and configure video device
 */
static esp_err_t setup_video_device(void)
{
    ESP_LOGI(TAG, "Setting up video device...");
    
    // Open MIPI CSI video device (O_RDWR required for ioctl operations)
    video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (video_fd < 0) {
        ESP_LOGE(TAG, "Failed to open video device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return ESP_FAIL;
    }
    
    // Get the default format configured by sdkconfig
    struct v4l2_format default_format = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    
    if (ioctl(video_fd, VIDIOC_G_FMT, &default_format) != 0) {
        ESP_LOGE(TAG, "Failed to get default video format");
        close(video_fd);
        return ESP_FAIL;
    }
    
    // Store actual camera dimensions for buffer management and display
    camera_width = default_format.fmt.pix.width;
    camera_height = default_format.fmt.pix.height;
    
    ESP_LOGI(TAG, "Camera format: %ux%u, pixelformat: 0x%x", 
             camera_width, camera_height, default_format.fmt.pix.pixelformat);
    
    // The ISP pipeline automatically converts RAW format to RGB565
    // No need to explicitly set format if ISP is enabled
    ESP_LOGI(TAG, "Using ISP-processed format");
    
    // Request video buffers
    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "Failed to request video buffers");
        close(video_fd);
        return ESP_FAIL;
    }
    
    // Map video buffers
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to query buffer %d", i);
            return ESP_FAIL;
        }
        
        buffer_length = buf.length;
        video_buffers[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                          MAP_SHARED, video_fd, buf.m.offset);
        
        if (video_buffers[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "Failed to map buffer %d", i);
            return ESP_FAIL;
        }
        
        // Queue the buffer
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %d", i);
            return ESP_FAIL;
        }
    }
    
    ESP_LOGI(TAG, "Video device configured successfully");
    return ESP_OK;
}

/**
 * Initialize LVGL canvas for preview display
 */
static esp_err_t init_preview_display(void)
{
    ESP_LOGI(TAG, "Initializing preview display...");
    
    if (camera_width == 0 || camera_height == 0) {
        ESP_LOGE(TAG, "Camera dimensions not set");
        return ESP_FAIL;
    }
    
    // Get the LVGL display
    lv_display_t *display = lv_display_get_default();
    if (!display) {
        ESP_LOGE(TAG, "No LVGL display found");
        return ESP_FAIL;
    }
    
    // Create canvas for camera preview
    lv_obj_t *parent = preview_parent ? preview_parent : lv_screen_active();
    preview_canvas = lv_canvas_create(parent);
    if (!preview_canvas) {
        ESP_LOGE(TAG, "Failed to create preview canvas");
        return ESP_FAIL;
    }
    
    // Allocate draw buffer for RGB565 format using actual camera dimensions
    ESP_LOGI(TAG, "Creating draw buffer: %lux%lu RGB565", camera_width, camera_height);
    draw_buf = lv_draw_buf_create(camera_width, camera_height, LV_COLOR_FORMAT_RGB565, 0);
    if (!draw_buf) {
        ESP_LOGE(TAG, "Failed to create draw buffer");
        return ESP_FAIL;
    }
    
    // Set canvas buffer
    lv_canvas_set_draw_buf(preview_canvas, draw_buf);
    
    // Scale canvas to fit within parent/display
    if (preview_parent) {
        // Fit within the designated preview area
        lv_obj_set_size(preview_canvas, lv_pct(100), lv_pct(100));
        lv_obj_center(preview_canvas);
    } else {
        // Scale to fit display (480x800)
        int32_t display_width = lv_display_get_horizontal_resolution(display);
        int32_t display_height = lv_display_get_vertical_resolution(display);
        
        // Calculate scaling to fit while maintaining aspect ratio
        float scale_x = (float)display_width / camera_width;
        float scale_y = (float)display_height / camera_height;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        
        lv_obj_set_size(preview_canvas, (int32_t)(camera_width * scale), (int32_t)(camera_height * scale));
        lv_obj_center(preview_canvas);
    }
    
    ESP_LOGI(TAG, "Preview display initialized successfully");
    return ESP_OK;
}

/**
 * Preview task that captures and displays video frames
 */
static void preview_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Preview task started");
    
    // Start video streaming only if not already started
    if (!stream_started) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(video_fd, VIDIOC_STREAMON, &type) != 0) {
            ESP_LOGE(TAG, "Failed to start video stream: %d", errno);
            preview_running = false;
            vTaskDelete(NULL);
            return;
        }
        stream_started = true;
        ESP_LOGI(TAG, "Video stream started");
    }
    
    while (preview_running) {
        // Dequeue frame buffer with timeout
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        
        int ret = ioctl(video_fd, VIDIOC_DQBUF, &buf);
        if (ret == 0) {
            // Copy frame data to LVGL canvas
            if (draw_buf && video_buffers[buf.index]) {
                uint32_t copy_size = buf.bytesused;
                if (copy_size > draw_buf->data_size) {
                    copy_size = draw_buf->data_size;
                }
                
                // Copy RGB565 data directly to draw buffer
                memcpy(draw_buf->data, video_buffers[buf.index], copy_size);
                
                // Update canvas (this will trigger display refresh)
                lv_obj_invalidate(preview_canvas);
            }
            
            // Re-queue the buffer
            if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(TAG, "Failed to re-queue buffer %d", buf.index);
            }
        } else {
            // DQBUF failed - likely no frame available yet
            if (errno != EAGAIN) {
                ESP_LOGE(TAG, "DQBUF error: %d", errno);
            }
            // Small delay to avoid spinning
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Feed the watchdog
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Stop video streaming
    if (stream_started) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(video_fd, VIDIOC_STREAMOFF, &type);
        stream_started = false;
        ESP_LOGI(TAG, "Video stream stopped");
    }
    
    ESP_LOGI(TAG, "Preview task ended");
    vTaskDelete(NULL);
}

/**
 * Start camera preview
 */
esp_err_t camera_preview_start(void)
{
    esp_err_t ret = ESP_OK;
    
    if (preview_running) {
        ESP_LOGW(TAG, "Camera preview already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting camera preview...");
    
    // Initialize video system if not done
    if (!camera_initialized) {
        ESP_RETURN_ON_ERROR(init_video_system(), TAG, "Video system init failed");
        ESP_RETURN_ON_ERROR(setup_video_device(), TAG, "Video device setup failed");
        ESP_RETURN_ON_ERROR(init_preview_display(), TAG, "Preview display init failed");
        camera_initialized = true;
    }
    
    // Start preview task
    preview_running = true;
    BaseType_t task_ret = xTaskCreate(preview_task, "camera_preview", 8192, NULL, 
                                     tskIDLE_PRIORITY + 2, &preview_task_handle);
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create preview task");
        preview_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Camera preview started successfully");
    return ret;
}

/**
 * Stop camera preview
 */
esp_err_t camera_preview_stop(void)
{
    if (!preview_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping camera preview...");
    
    // Signal task to stop
    preview_running = false;
    
    // Wait for task to finish
    if (preview_task_handle) {
        // Task will delete itself
        preview_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Camera preview stopped");
    return ESP_OK;
}

/**
 * Get camera preview status
 */
bool camera_preview_is_running(void)
{
    return preview_running;
}

/**
 * Cleanup resources
 */
void camera_preview_deinit(void)
{
    ESP_LOGI(TAG, "Cleaning up camera preview resources...");
    
    // Stop preview first
    camera_preview_stop();
    
    // Unmap video buffers
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (video_buffers[i] != NULL && video_buffers[i] != MAP_FAILED) {
            munmap(video_buffers[i], buffer_length);
            video_buffers[i] = NULL;
        }
    }
    
    // Close video device
    if (video_fd >= 0) {
        close(video_fd);
        video_fd = -1;
    }
    
    // Clean up LVGL objects
    if (draw_buf) {
        lv_draw_buf_destroy(draw_buf);
        draw_buf = NULL;
    }
    
    if (preview_canvas) {
        lv_obj_delete(preview_canvas);
        preview_canvas = NULL;
    }
    
    // Deinitialize video system
    esp_video_deinit();
    
    camera_initialized = false;
    preview_parent = NULL;  // Reset parent reference
    
    ESP_LOGI(TAG, "Camera preview cleanup completed");
}

/**
 * Set the parent object for camera preview canvas
 */
void camera_preview_set_parent(lv_obj_t *parent)
{
    preview_parent = parent;
    ESP_LOGI(TAG, "Preview parent set to %p", parent);
}