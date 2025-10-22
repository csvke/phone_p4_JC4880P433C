/**
 * @file camera_display.c
 * @brief Camera display module implementation - LVGL integration
 * 
 * Handles LVGL widget creation, display updates, and timer management for camera preview.
 * Extracted from camera.c as part of Phase 4 refactoring.
 */

#include "camera_display.h"
#include "camera_internal.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "camera_display";

// Module state
static lv_obj_t *preview_parent_local = NULL;
static lv_obj_t *preview_canvas_local = NULL;
static lv_timer_t *update_timer_local = NULL;
static lv_display_t *display_local = NULL;

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
    if (!frame_ready_for_display || !scaled_buffer || !display_local) {
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
    lv_img_set_src(preview_canvas_local, &img_dsc);
    
    if (update_count <= 10) {
        ESP_LOGI(TAG, "Display update #%lu - Image source updated (%dx%d passthrough, LVGL zoom active)", 
                 update_count, CAMERA_HRES, CAMERA_VRES);
    }
}

esp_err_t camera_display_set_parent(lv_obj_t *parent)
{
    preview_parent_local = parent;
    return ESP_OK;
}

esp_err_t camera_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL canvas preview...");
    
    if (!preview_parent_local) {
        ESP_LOGE(TAG, "No preview parent set");
        return ESP_FAIL;
    }
    
    display_local = lv_display_get_default();
    if (!display_local) {
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
    preview_canvas_local = lv_img_create(preview_parent_local);
    if (!preview_canvas_local) {
        ESP_LOGE(TAG, "Failed to create LVGL image widget");
        return ESP_FAIL;
    }
    
    // Set image position and pivot point (for zoom)
    lv_obj_set_pos(preview_canvas_local, img_x, img_y);
    lv_img_set_pivot(preview_canvas_local, 0, 0);  // Zoom from top-left corner
    lv_img_set_zoom(preview_canvas_local, lvgl_zoom);  // Apply zoom factor
    lv_obj_clear_flag(preview_canvas_local, LV_OBJ_FLAG_SCROLLABLE);
    
    ESP_LOGI(TAG, "LVGL image widget created with zoom=%u (%.1f%%), will scale PPA output to fit display", 
             lvgl_zoom, (float)lvgl_zoom / 256 * 100);
    ESP_LOGI(TAG, "Image widget positioned at (%d,%d), final size: %dx%d", 
             img_x, img_y, display_width_final, display_height_final);
    
    // Store widget globally for timer callback access
    preview_canvas = preview_canvas_local;
    display = display_local;
    
    return ESP_OK;
}

esp_err_t camera_display_start_timer(void)
{
    if (update_timer_local) {
        ESP_LOGW(TAG, "Display timer already running");
        return ESP_OK;
    }
    
    // Create LVGL timer to update display (runs in LVGL thread context)
    // This safely handles image source updates without cross-thread LVGL access
    // Set to 30 FPS (33ms) - PPA rotation-only should easily handle this
    bsp_display_lock(0);
    update_timer_local = lv_timer_create(display_update_timer_cb, 33, NULL);  // 30 FPS target
    if (!update_timer_local) {
        ESP_LOGE(TAG, "Failed to create display update timer");
        bsp_display_unlock();
        return ESP_FAIL;
    }
    bsp_display_unlock();
    
    // Store globally for stop access
    update_timer = update_timer_local;
    
    ESP_LOGI(TAG, "Display update timer created (33ms interval, 30 FPS target)");
    return ESP_OK;
}

void camera_display_stop_timer(void)
{
    if (update_timer_local) {
        bsp_display_lock(0);
        lv_timer_delete(update_timer_local);
        update_timer_local = NULL;
        update_timer = NULL;
        bsp_display_unlock();
        ESP_LOGI(TAG, "Display update timer deleted");
    }
}

void camera_display_deinit(void)
{
    // Stop timer first
    camera_display_stop_timer();
    
    // Delete LVGL widgets
    if (preview_canvas_local) {
        bsp_display_lock(0);
        lv_obj_delete(preview_canvas_local);
        preview_canvas_local = NULL;
        preview_canvas = NULL;
        bsp_display_unlock();
        ESP_LOGI(TAG, "LVGL preview widget deleted");
    }
    
    // Clear references
    preview_parent_local = NULL;
    display_local = NULL;
    display = NULL;
}
