/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Camera app for ESP32-P4 with MIPI-CSI camera
 * 
 * Features:
 * - Real-time video preview using esp_video V4L2 interface
 * - LVGL canvas-based display
 * - Automatic resource management (Brookesia best practices)
 * - Proper error handling and cleanup
 */
class Camera : public ESP_Brookesia_PhoneApp {
public:
    /**
     * @brief Constructor
     * @param hor_res Horizontal resolution (screen width)
     * @param ver_res Vertical resolution (screen height)
     */
    Camera(uint16_t hor_res, uint16_t ver_res);
    
    /**
     * @brief Destructor
     */
    ~Camera();

    /**
     * @brief Initialize the app (override from base class)
     * @return true on success, false on failure
     */
    bool init(void) override;

    /**
     * @brief Run the camera app (required by Brookesia)
     * Creates UI and starts video stream
     * @return true on success, false on failure
     */
    bool run(void) override;

    /**
     * @brief Handle back button press (required by Brookesia)
     * @return true to close app, false to stay open
     */
    bool back(void) override;

    /**
     * @brief Close the app (override for cleanup)
     * @return true on success
     */
    bool close(void) override;

private:
    /**
     * @brief Camera initialization task (runs in separate FreeRTOS task)
     * @param app Pointer to Camera instance
     */
    static void cameraInitTask(void *app);

    /**
     * @brief Video streaming task (runs in separate FreeRTOS task)
     * @param app Pointer to Camera instance
     */
    static void videoStreamTask(void *app);

    /**
     * @brief LVGL timer callback for updating canvas
     * @param timer LVGL timer handle
     */
    static void canvasUpdateTimerCb(lv_timer_t *timer);

    // Screen resolution
    uint16_t _hor_res;
    uint16_t _ver_res;

    // Camera state
    int _video_fd;              // V4L2 video device file descriptor
    bool _is_streaming;         // Is video stream active
    TaskHandle_t _stream_task;  // FreeRTOS task handle for video streaming

    // Video buffers (V4L2 MMAP mode)
    uint8_t *_video_buffers[3]; // Circular buffer (3 buffers for smooth streaming)
    size_t _buffer_size;
    uint8_t _current_buffer_idx;

    // LVGL UI elements (automatically cleaned up by Brookesia)
    lv_obj_t *_canvas;          // Canvas for video display
    lv_timer_t *_update_timer;  // Timer for canvas refresh
};
