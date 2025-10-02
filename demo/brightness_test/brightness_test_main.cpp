#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "brightness_test";

// Brightness test function that cycles through different brightness levels
void brightness_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting brightness test...");
    
    // Test different brightness levels from 0% to 100% in steps
    uint8_t brightness_levels[] = {0, 10, 25, 50, 75, 90, 100};
    size_t num_levels = sizeof(brightness_levels) / sizeof(brightness_levels[0]);
    
    while (1) {
        for (size_t i = 0; i < num_levels; i++) {
            ESP_LOGI(TAG, "Setting brightness to %d%%", brightness_levels[i]);
            
            esp_err_t ret = bsp_display_brightness_set(brightness_levels[i]);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✓ Brightness set successfully to %d%%", brightness_levels[i]);
            } else {
                ESP_LOGE(TAG, "✗ Failed to set brightness to %d%%. Error: %s", 
                         brightness_levels[i], esp_err_to_name(ret));
            }
            
            // Hold each brightness level for 3 seconds so you can observe the change
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        
        ESP_LOGI(TAG, "Brightness test cycle complete. Repeating...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Quick brightness test function for immediate verification
void quick_brightness_test(void)
{
    ESP_LOGI(TAG, "=== Quick Brightness Test ===");
    
    ESP_LOGI(TAG, "Testing brightness extremes...");
    
    // Test minimum brightness (0%)
    ESP_LOGI(TAG, "Setting to 0%% (minimum)");
    esp_err_t ret = bsp_display_brightness_set(0);
    ESP_LOGI(TAG, "Result: %s", (ret == ESP_OK) ? "SUCCESS" : esp_err_to_name(ret));
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test maximum brightness (100%)
    ESP_LOGI(TAG, "Setting to 100%% (maximum)");
    ret = bsp_display_brightness_set(100);
    ESP_LOGI(TAG, "Result: %s", (ret == ESP_OK) ? "SUCCESS" : esp_err_to_name(ret));
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test medium brightness (50%)
    ESP_LOGI(TAG, "Setting to 50%% (medium)");
    ret = bsp_display_brightness_set(50);
    ESP_LOGI(TAG, "Result: %s", (ret == ESP_OK) ? "SUCCESS" : esp_err_to_name(ret));
    
    ESP_LOGI(TAG, "=== Quick test complete ===");
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Initializing display for brightness testing...");

    // Initialize display
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 80,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Display initialization failed");
        abort();
    }
    ESP_LOGI(TAG, "Display started successfully");
    
    // Turn on backlight initially
    esp_err_t ret = bsp_display_backlight_on();
    ESP_LOGI(TAG, "Backlight initialization: %s", (ret == ESP_OK) ? "SUCCESS" : esp_err_to_name(ret));
    
    // Run quick test first
    quick_brightness_test();
    
    // Create a task for continuous brightness testing
    xTaskCreate(brightness_test_task, "brightness_test", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Brightness test setup complete. Monitor serial output and observe display brightness changes.");
    ESP_LOGI(TAG, "The display should cycle through different brightness levels every 3 seconds.");
    ESP_LOGI(TAG, "0%% = darkest (may appear off), 100%% = brightest");
}