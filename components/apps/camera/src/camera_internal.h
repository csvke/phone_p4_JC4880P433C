/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_internal.h
 * @brief Internal definitions shared across camera modules
 * 
 * This header contains:
 * - Shared configuration constants
 * - Global state variables
 * - Common structures
 * - Internal function declarations
 * 
 * DO NOT include this in public API headers!
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_sensor.h"
#include "driver/isp.h"
#include "driver/ppa.h"
#include "driver/i2c_master.h"
#include "esp_async_memcpy.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Configuration Constants
 *============================================================================*/

// Camera resolution (production config: 1288x728 @ 1-lane)
#define CAMERA_HRES                     1288
#define CAMERA_VRES                     728
#define CAMERA_LANE_BITRATE_MBPS        400
#define CAMERA_DATA_LANES               1

// Display configuration
#define DISPLAY_WIDTH                   480
#define DISPLAY_HEIGHT                  800
#define STATUS_BAR_HEIGHT               40

// Frame buffer configuration
#define CAMERA_FRAME_BUFFER_COUNT       1
#define RGB565_BITS_PER_PIXEL          16

// ISP clock configuration
#define ISP_CLK_HZ                     (80 * 1000 * 1000)

// Memory alignment
#define CACHE_LINE_SIZE                 128

/*==============================================================================
 * Global State Variables
 *============================================================================*/

// Initialization flags
extern bool camera_initialized;
extern bool preview_running;

// Hardware handles
extern esp_cam_ctlr_handle_t cam_handle;
extern isp_proc_handle_t isp_proc;
extern isp_awb_ctlr_t awb_ctrl;
extern i2c_master_bus_handle_t i2c_bus;
extern esp_cam_sensor_device_t *cam_sensor;
extern ppa_client_handle_t ppa_client;
extern async_memcpy_handle_t dma_handle;

// Synchronization primitives
extern SemaphoreHandle_t frame_ready_sem;
extern SemaphoreHandle_t ppa_done_sem;

// LVGL display objects
extern lv_obj_t *preview_parent;
extern lv_obj_t *preview_canvas;
extern lv_display_t *display;
extern lv_timer_t *update_timer;

// Frame buffers
extern void *frame_buffer;
extern size_t frame_buffer_size;
extern void *scaled_buffer;
extern size_t scaled_buffer_size;

// Frame transaction descriptor
extern esp_cam_ctlr_trans_t frame_trans;

// Task handle
extern TaskHandle_t preview_task_handle;

// Frame ready flag
extern volatile bool frame_ready_for_display;

// Timing and diagnostics
extern TickType_t last_callback_time;
extern int64_t last_frame_time_us;
extern uint32_t ppa_callback_count;
extern uint32_t semaphore_overflow_count;
extern uint32_t lvgl_lock_timeout_count;

/*==============================================================================
 * Common Structures
 *============================================================================*/

/**
 * @brief Camera configuration structure
 */
typedef struct {
    uint32_t h_res;                 /*!< Horizontal resolution */
    uint32_t v_res;                 /*!< Vertical resolution */
    uint32_t lane_bitrate_mbps;     /*!< Lane bit rate in Mbps */
    uint32_t data_lanes;            /*!< Number of data lanes */
} camera_config_t;

/**
 * @brief Display configuration structure
 */
typedef struct {
    uint32_t width;                 /*!< Display width */
    uint32_t height;                /*!< Display height */
    uint32_t useable_height;        /*!< Useable height (excluding status bar) */
    uint16_t zoom;                  /*!< LVGL zoom factor (256 = 100%) */
} display_config_t;

/*==============================================================================
 * Helper Macros
 *============================================================================*/

#define ALIGN_UP(size, align)       (((size) + (align) - 1) & ~((align) - 1))
#define MIN(a, b)                   ((a) < (b) ? (a) : (b))
#define MAX(a, b)                   ((a) > (b) ? (a) : (b))

#ifdef __cplusplus
}
#endif
