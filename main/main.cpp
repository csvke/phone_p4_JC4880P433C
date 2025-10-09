#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_brookesia.hpp"
#include "systems/phone/stylesheets/480_800/dark/stylesheet.hpp"
#include "Calculator.hpp"
#include "Settings.hpp"

// Suppress missing field initializer warnings for ESP-Brookesia structures
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    // Suppress harmless I2C pull-up warning (external pull-ups are present on hardware)
    esp_log_level_set("i2c.master", ESP_LOG_ERROR);
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Try to mount SPIFFS, but don't fail if it's not available
    err = bsp_spiffs_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s), continuing without SPIFFS", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SPIFFS mount successfully");
    }

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
            .sw_rotate = true,  // Enable SW rotation - ST7701S doesn't support HW swap_xy/mirror
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

    // Create phone system with explicit display
    esp_brookesia::systems::phone::Phone *phone = new esp_brookesia::systems::phone::Phone(disp);
    assert(phone != nullptr && "Failed to create phone");

    // Get and explicitly set the GT911 touch device (already initialized by BSP)
    lv_indev_t *touch_indev = lv_indev_get_next(nullptr);
    if (touch_indev != nullptr && lv_indev_get_type(touch_indev) == LV_INDEV_TYPE_POINTER) {
        phone->setTouchDevice(touch_indev);
        ESP_LOGI(TAG, "GT911 touch device explicitly set (@0x%p)", touch_indev);
    } else {
        ESP_LOGW(TAG, "Touch device not found, will use auto-detection");
    }

    // Use 480x800 stylesheet - perfect match for JC4880P443C display
    ESP_LOGI(TAG, "Display resolution: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    esp_brookesia::systems::phone::Stylesheet stylesheet = ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET();
    
    // Configure and activate the stylesheet
    assert(phone->addStylesheet(&stylesheet) && "Add 480x800 stylesheet failed");
    assert(phone->activateStylesheet(&stylesheet) && "Activate 480x800 stylesheet failed");
    ESP_LOGI(TAG, "480x800 stylesheet activated for native display resolution");

    // Begin the phone interface
    assert(phone->begin() && "Phone begin failed");

    // Install Calculator app - keep shared_ptr alive to prevent deallocation
    static std::shared_ptr<phone_apps::Calculator> calculator = std::make_shared<phone_apps::Calculator>();
    assert(phone->getManager().installApp(calculator.get()) && "Install Calculator app failed");
    ESP_LOGI(TAG, "Calculator app installed successfully");

    // Install Settings app (provides WiFi management)
    static std::shared_ptr<AppSettings> settings = std::make_shared<AppSettings>();
    assert(phone->getManager().installApp(settings.get()) && "Install Settings app failed");
    ESP_LOGI(TAG, "Settings app installed successfully");

    ESP_LOGI(TAG, "setup done");
    bsp_display_unlock();
}

#pragma GCC diagnostic pop
