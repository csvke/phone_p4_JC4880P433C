#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "stylesheet_compat.h"
#include "esp_brookesia.hpp"

// Suppress missing field initializer warnings for ESP-Brookesia structures
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_LOGI(TAG, "SPIFFS mount successfully");

    // Optional: SD card
    bsp_sdcard_mount();

    ESP_ERROR_CHECK(bsp_extra_codec_init());

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
    
    ESP_LOGI(TAG, "Starting display initialization...");

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Display initialization failed");
        abort();
    }
    ESP_LOGI(TAG, "Display started successfully");
    
    bsp_display_backlight_on();

    bsp_display_lock(0);

    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone(disp);
    assert(phone != nullptr && "Failed to create phone");

    // Activate the 480x800 stylesheet from the Brookesia branch
    phone->activate_stylesheet("phone_480_800");

    // Begin phone UI
    if (!phone->begin()) {
        ESP_LOGE(TAG, "Failed to begin phone");
        abort();
    }

    // Minimal phone without extra example apps; add apps later as dependencies are added

    ESP_LOGI(TAG, "setup done");
    bsp_display_unlock();
}

#pragma GCC diagnostic pop
