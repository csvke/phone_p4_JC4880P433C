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
            .status_bar_visual_mode = esp_brookesia::systems::phone::StatusBar::VisualMode::SHOW_FIXED,
            .navigation_bar_visual_mode = esp_brookesia::systems::phone::NavigationBar::VisualMode::SHOW_FIXED,
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
        
        ESP_LOGI(TAG, "Current camera format: %ux%u, fourcc=0x%08x ('%c%c%c%c')", 
                 fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat,
                 (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
                 (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
        
        // Request RGB565 format (will use ISP to debayer if camera outputs raw Bayer)
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        // Keep resolution, just change pixel format
        
        if (ioctl(_video_fd, VIDIOC_S_FMT, &fmt) < 0) {
            ESP_LOGW(TAG, "Failed to set RGB565 format, will use current format");
        } else {
            ESP_LOGI(TAG, "Set camera format to: %ux%u, RGB565", 
                     fmt.fmt.pix.width, fmt.fmt.pix.height);
        }
    }

    // Get screen and visual area
    lv_obj_t *screen = lv_scr_act();
    const auto &visual_area = getVisualArea();
    int width = lv_area_get_width(&visual_area);
    int height = lv_area_get_height(&visual_area);
    
    ESP_LOGI(TAG, "Visual area: %dx%d", width, height);

    // Create fullscreen container
    lv_obj_t *cont = lv_obj_create(screen);
    lv_obj_set_size(cont, width, height);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);

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
            
            // Copy frame to canvas buffer (assuming RGB565 format)
            lv_draw_buf_t *canvas_buf = lv_canvas_get_draw_buf(self->_canvas);
            if (canvas_buf != nullptr && canvas_buf->data != nullptr) {
                memcpy(canvas_buf->data, frame_data, 
                       self->_hor_res * self->_ver_res * sizeof(lv_color_t));
                
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
