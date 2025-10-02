// =============================================================================
// Brightness Control Integration Example for ESP-Brookesia Phone App
// =============================================================================
// Add this code to your ESP-Brookesia phone app to integrate brightness control

#include "bsp/esp-bsp.h"

class BrightnessControl {
private:
    uint8_t current_brightness = 100; // Default to full brightness
    
public:
    // Initialize brightness control (call once during app startup)
    esp_err_t init() {
        esp_err_t ret = bsp_display_backlight_on();
        if (ret == ESP_OK) {
            // Load saved brightness from NVS if available
            load_brightness_from_nvs();
            ret = set_brightness(current_brightness);
        }
        return ret;
    }
    
    // Set brightness (0-100%)
    esp_err_t set_brightness(uint8_t brightness) {
        if (brightness > 100) {
            return ESP_ERR_INVALID_ARG;
        }
        
        esp_err_t ret = bsp_display_brightness_set(brightness);
        if (ret == ESP_OK) {
            current_brightness = brightness;
            save_brightness_to_nvs();
        }
        return ret;
    }
    
    // Get current brightness
    uint8_t get_brightness() const {
        return current_brightness;
    }
    
    // Brightness step functions for UI controls
    esp_err_t increase_brightness(uint8_t step = 10) {
        uint8_t new_brightness = current_brightness + step;
        if (new_brightness > 100) new_brightness = 100;
        return set_brightness(new_brightness);
    }
    
    esp_err_t decrease_brightness(uint8_t step = 10) {
        uint8_t new_brightness = (current_brightness > step) ? 
                                 current_brightness - step : 0;
        return set_brightness(new_brightness);
    }

private:
    // Save brightness setting to NVS for persistence
    void save_brightness_to_nvs() {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_set_u8(nvs_handle, "brightness", current_brightness);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }
    }
    
    // Load brightness setting from NVS
    void load_brightness_from_nvs() {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("settings", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            size_t required_size = sizeof(current_brightness);
            nvs_get_u8(nvs_handle, "brightness", &current_brightness);
            nvs_close(nvs_handle);
        }
        // Ensure valid range
        if (current_brightness > 100) current_brightness = 100;
    }
};

// Global brightness control instance
static BrightnessControl brightness_ctrl;

// =============================================================================
// Example LVGL Slider Integration
// =============================================================================

// Slider event callback
void brightness_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    // Set brightness based on slider value
    esp_err_t ret = brightness_ctrl.set_brightness((uint8_t)value);
    if (ret != ESP_OK) {
        ESP_LOGE("BRIGHTNESS", "Failed to set brightness: %s", esp_err_to_name(ret));
    }
}

// Create brightness slider in your settings screen
lv_obj_t* create_brightness_slider(lv_obj_t* parent) {
    // Create slider
    lv_obj_t * slider = lv_slider_create(parent);
    lv_slider_set_range(slider, 10, 100); // Min 10% to avoid completely dark screen
    lv_slider_set_value(slider, brightness_ctrl.get_brightness(), LV_ANIM_OFF);
    lv_obj_set_width(slider, 200);
    
    // Add event callback
    lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Create label
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, "Brightness");
    
    return slider;
}

// =============================================================================
// Example Integration in main.cpp
// =============================================================================

// Add to your main.cpp after display initialization:
/*
extern "C" void app_main(void)
{
    // ... existing initialization code ...
    
    // Initialize display
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    
    // Initialize brightness control
    esp_err_t ret = brightness_ctrl.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize brightness control: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Brightness control initialized successfully");
    }
    
    // ... rest of your app initialization ...
}
*/

// =============================================================================
// Quick Test Function (add to your app for testing)
// =============================================================================

void test_brightness_quick() {
    ESP_LOGI("BRIGHTNESS_TEST", "Testing brightness control...");
    
    // Test different brightness levels
    uint8_t levels[] = {20, 50, 80, 100};
    for (int i = 0; i < 4; i++) {
        ESP_LOGI("BRIGHTNESS_TEST", "Setting brightness to %d%%", levels[i]);
        brightness_ctrl.set_brightness(levels[i]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI("BRIGHTNESS_TEST", "Brightness test complete");
}