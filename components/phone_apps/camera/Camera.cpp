/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Camera.hpp"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_lvgl_port.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "bsp/esp-bsp.h"

static const char *TAG = "Camera";

// V4L2 configuration
// Note: esp_video creates the MIPI-CSI device as /dev/video0 (ESP_VIDEO_MIPI_CSI_DEVICE_NAME)
#define VIDEO_DEVICE "/dev/video0"
#define BUFFER_COUNT 3
#define CANVAS_UPDATE_PERIOD_MS 33  // ~30 FPS

// Software debayering helper (Bayer GRBG -> RGB565)
// OV02C10 outputs RAW10 GRBG Bayer pattern
inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Camera icon (will be created manually or imported from assets)
LV_IMG_DECLARE(img_app_camera);

Camera::Camera(uint16_t hor_res, uint16_t ver_res) :
    esp_brookesia::systems::phone::App(
        // Core app configuration
        {
            .name = "Camera",
            .launcher_icon = esp_brookesia::gui::StyleImage::IMAGE(&img_app_camera),
            .screen_size = esp_brookesia::gui::StyleSize::RECT_PERCENT(100, 100),
            .flags = {
                .enable_default_screen = 1,
                .enable_recycle_resource = 1,
                .enable_resize_visual_area = 1,
            }
        },
        // Phone-specific configuration
        {
            .app_launcher_page_index = 0,
            .status_icon_area_index = 0,
            .status_icon_data = {},
            .status_bar_visual_mode = esp_brookesia::systems::phone::StatusBar::VisualMode::HIDE,
            .navigation_bar_visual_mode = esp_brookesia::systems::phone::NavigationBar::VisualMode::HIDE,
            .flags = {
                .enable_status_icon_common_size = 0,
                .enable_navigation_gesture = 1,
            },
        }
    ),
    _hor_res(hor_res),
    _ver_res(ver_res),
    _video_fd(-1),
    _is_streaming(false),
    _stream_task(nullptr),
    _buffer_size(0),
    _current_buffer_idx(0),
    _canvas(nullptr),
    _update_timer(nullptr)
{
    // Initialize buffer pointers
    for (int i = 0; i < BUFFER_COUNT; i++) {
        _video_buffers[i] = nullptr;
    }
    
    ESP_LOGI(TAG, "Camera app created for %dx%d resolution", _hor_res, _ver_res);
}

Camera::~Camera()
{
    ESP_LOGI(TAG, "Camera app destroyed");
}

bool Camera::init(void)
{
    ESP_LOGI(TAG, "Camera app init() - deferring hardware init to run()");
    // Camera hardware initialization is deferred to run() because:
    // 1. esp_video_init() needs BSP's I2C handle (only available after BSP init)
    // 2. V4L2 device only exists after esp_video_init()
    // 3. Init is called during app installation (early boot), run() is called when user opens app
    return true;
}

bool Camera::run(void)
{
    ESP_LOGI(TAG, "Starting camera app...");

    // Initialize camera hardware (only once)
    if (_video_fd < 0) {
        ESP_LOGI(TAG, "Initializing camera hardware...");
        
        // Get BSP's I2C handle (for camera sensor communication)
        i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
        
        // Initialize esp_video with BSP's I2C (following manufacturer's pattern)
        esp_video_init_csi_config_t csi_config = {
            .sccb_config = {
                .init_sccb = false,  // Use BSP's I2C, don't init new one
                .i2c_handle = i2c_bus,
                .freq = 400000,  // 400kHz - standard I2C fast mode for OV02C10 sensor
            },
            .reset_pin = (gpio_num_t)3,  // GPIO3 - Camera reset pin (from hardware schematic)
            .pwdn_pin = (gpio_num_t)-1,  // No power-down pin on this board
        };
        
        esp_video_init_config_t video_config = {
            .csi = &csi_config,
        };
        
        esp_err_t ret = esp_video_init(&video_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        ESP_LOGI(TAG, "esp_video initialized successfully");
        
        // Open V4L2 video device (now exists after esp_video_init)
        _video_fd = open(VIDEO_DEVICE, O_RDONLY);
        if (_video_fd < 0) {
            ESP_LOGE(TAG, "Failed to open %s: %s", VIDEO_DEVICE, strerror(errno));
            return false;
        }
        
        // Query device capabilities
        struct v4l2_capability cap;
        if (ioctl(_video_fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ESP_LOGE(TAG, "Failed to query device capabilities");
            ::close(_video_fd);
            _video_fd = -1;
            return false;
        }
        
        ESP_LOGI(TAG, "Camera device opened successfully");
        ESP_LOGI(TAG, "  Driver: %s", cap.driver);
        ESP_LOGI(TAG, "  Card: %s", cap.card);
        ESP_LOGI(TAG, "  Bus: %s", cap.bus_info);
        
        // Get current format
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        if (ioctl(_video_fd, VIDIOC_G_FMT, &fmt) < 0) {
            ESP_LOGE(TAG, "Failed to get video format");
            ::close(_video_fd);
            _video_fd = -1;
            return false;
        }
        
        ESP_LOGI(TAG, "Camera format: %ux%u, fourcc=0x%08x ('%c%c%c%c')", 
                 fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat,
                 (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
        
        ESP_LOGI(TAG, "ISP automatically converts RAW10 to RGB565 ('RGBP') - direct display!");
    }

    // Get screen and visual area
    lv_obj_t *screen = lv_scr_act();
    const auto &visual_area = getVisualArea();
    int width = lv_area_get_width(&visual_area);
    int height = lv_area_get_height(&visual_area);
    
    ESP_LOGI(TAG, "Visual area: %dx%d", width, height);

    // Create fullscreen container (no scrolling)
    lv_obj_t *cont = lv_obj_create(screen);
    lv_obj_set_size(cont, width, height);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Create canvas for video display
    _canvas = lv_canvas_create(cont);
    lv_obj_center(_canvas);

    // Allocate canvas buffer (RGB565 format, 2 bytes per pixel)
    size_t canvas_buf_size = _hor_res * _ver_res * sizeof(lv_color_t);
    void *canvas_buf = lv_malloc(canvas_buf_size);
    if (canvas_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        return false;
    }

    lv_canvas_set_buffer(_canvas, canvas_buf, _hor_res, _ver_res, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(_canvas, lv_color_black(), LV_OPA_COVER);

    // Create status label
    lv_obj_t *status_label = lv_label_create(cont);
    lv_label_set_text(status_label, "📷 Initializing camera...");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);

    // Request V4L2 buffers (MMAP mode - kernel manages memory)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(_video_fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "Failed to request buffers");
        return false;
    }

    // Map buffers
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(_video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to query buffer %u", i);
            return false;
        }

        _video_buffers[i] = (uint8_t *)mmap(nullptr, buf.length, 
                                             PROT_READ | PROT_WRITE, 
                                             MAP_SHARED, _video_fd, buf.m.offset);
        
        if (_video_buffers[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "Failed to mmap buffer %u", i);
            return false;
        }

        _buffer_size = buf.length;

        // Queue buffer
        if (ioctl(_video_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %u", i);
            return false;
        }
    }

    ESP_LOGI(TAG, "Buffers allocated and queued (%u buffers, %zu bytes each)", 
             req.count, _buffer_size);

    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(_video_fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "Failed to start streaming");
        return false;
    }

    _is_streaming = true;
    lv_label_set_text(status_label, "📷 Camera streaming...");

    // Create video streaming task
    xTaskCreatePinnedToCore(videoStreamTask, "camera_stream", 4096, this, 5, &_stream_task, 0);

    ESP_LOGI(TAG, "Camera app running successfully");
    return true;
}

bool Camera::back(void)
{
    ESP_LOGI(TAG, "Back button pressed, closing camera");
    return true;  // Return true to close the app
}

bool Camera::close(void)
{
    ESP_LOGI(TAG, "Closing camera app...");

    // Stop streaming
    if (_is_streaming && _video_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(_video_fd, VIDIOC_STREAMOFF, &type);
        _is_streaming = false;
    }

    // Delete streaming task
    if (_stream_task != nullptr) {
        vTaskDelete(_stream_task);
        _stream_task = nullptr;
    }

    // Unmap buffers
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (_video_buffers[i] != nullptr && _video_buffers[i] != MAP_FAILED) {
            munmap(_video_buffers[i], _buffer_size);
            _video_buffers[i] = nullptr;
        }
    }

    // Close video device
    if (_video_fd >= 0) {
        ::close(_video_fd);  // Use global scope to avoid conflict with member function
        _video_fd = -1;
    }

    ESP_LOGI(TAG, "Camera app closed successfully");
    
    // Brookesia will automatically clean up LVGL resources (canvas, labels, etc.)
    return true;
}

void Camera::videoStreamTask(void *app)
{
    Camera *self = (Camera *)app;
    struct v4l2_buffer buf;
    
    ESP_LOGI(TAG, "Video streaming task started");

    while (self->_is_streaming) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        // Dequeue buffer (get frame from camera)
        if (ioctl(self->_video_fd, VIDIOC_DQBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to dequeue buffer");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Check if frame is valid
        if (buf.flags & V4L2_BUF_FLAG_DONE) {
            // Update canvas with new frame
            uint8_t *frame_data = self->_video_buffers[buf.index];
            
            lv_draw_buf_t *canvas_buf = lv_canvas_get_draw_buf(self->_canvas);
            if (canvas_buf != nullptr && canvas_buf->data != nullptr) {
                // CRITICAL DISCOVERY: ISP is already converting RAW10 to RGB565!
                // Buffer size: 1875328 bytes = 1288×728×2 (RGB565)
                // We just need to downscale and copy the RGB565 data directly!
                
                uint16_t *frame_rgb565 = (uint16_t *)frame_data;
                uint16_t *canvas_rgb565 = (uint16_t *)canvas_buf->data;
                
                // Camera: 1288x728, RGB565 (2 bytes per pixel)
                int cam_width = 1288;
                int cam_height = 728;
                
                // Debug: log first few RGB565 pixels (only once)
                static int frame_count = 0;
                if (frame_count++ == 0) {
                    ESP_LOGI(TAG, "ISP output is RGB565! Buffer=%zu bytes, first 4 pixels:", buf.bytesused);
                    for (int i = 0; i < 4; i++) {
                        ESP_LOGI(TAG, "  pixel[%d] = 0x%04x", i, frame_rgb565[i]);
                    }
                }
                
                int out_width = self->_hor_res;
                int out_height = self->_ver_res;
                
                // Calculate proper aspect ratio scaling
                // Camera: 1288x728 = 1.77:1 aspect ratio
                // Display: 480x696 = 0.69:1 aspect ratio (portrait)
                // We need to crop the camera feed to fit without stretching
                
                float cam_aspect = (float)cam_width / cam_height;  // 1.77
                float display_aspect = (float)out_width / out_height;  // 0.69
                
                int src_w, src_h, src_x_offset, src_y_offset;
                
                if (cam_aspect > display_aspect) {
                    // Camera is wider - crop sides (fit height)
                    src_h = cam_height;
                    src_w = (int)(cam_height * display_aspect);
                    src_x_offset = (cam_width - src_w) / 2;  // Center horizontally
                    src_y_offset = 0;
                } else {
                    // Camera is taller - crop top/bottom (fit width)
                    src_w = cam_width;
                    src_h = (int)(cam_width / display_aspect);
                    src_x_offset = 0;
                    src_y_offset = (cam_height - src_h) / 2;  // Center vertically
                }
                
                // Scale from cropped region to canvas
                float x_ratio = (float)src_w / out_width;
                float y_ratio = (float)src_h / out_height;
                
                for (int y = 0; y < out_height; y++) {
                    for (int x = 0; x < out_width; x++) {
                        int src_x = src_x_offset + (int)(x * x_ratio);
                        int src_y = src_y_offset + (int)(y * y_ratio);
                        
                        // Direct copy of RGB565 pixel from ISP output
                        canvas_rgb565[y * out_width + x] = frame_rgb565[src_y * cam_width + src_x];
                    }
                }
                
                // Invalidate canvas to trigger redraw
                lv_obj_invalidate(self->_canvas);
            }
        }

        // Requeue buffer
        if (ioctl(self->_video_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to requeue buffer");
        }

        // Yield to prevent task starvation (~30 FPS)
        vTaskDelay(pdMS_TO_TICKS(CANVAS_UPDATE_PERIOD_MS));
    }

    ESP_LOGI(TAG, "Video streaming task stopped");
    vTaskDelete(nullptr);
}
