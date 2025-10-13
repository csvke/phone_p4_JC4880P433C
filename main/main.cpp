#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "boost/thread.hpp"
#include "nvs_flash.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"

// Include apps to force linking (for registry-based installation)
#include "Calculator.hpp"
#include "Settings.hpp"
#include "CameraCsi.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;
using namespace phone_apps;

#define LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 4,       \
        .task_stack = 10 * 1024,       \
        .task_affinity = -1,      \
        .task_max_sleep_ms = 500, \
        .timer_period_ms = 5,     \
    }

constexpr bool EXAMPLE_SHOW_MEM_INFO = false;

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

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
        ESP_UTILS_LOGW("SPIFFS mount failed (%s), continuing without SPIFFS", esp_err_to_name(err));
    } else {
        ESP_UTILS_LOGI("SPIFFS mount successfully");
    }

    // Optional: SD card
    bsp_sdcard_mount();

    ESP_ERROR_CHECK(bsp_extra_codec_init());

    /* Configure display */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 80,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,  // Enable SW rotation - ST7701S doesn't support HW swap_xy/mirror
        }
    };
    
    ESP_UTILS_LOGI("Starting display initialization...");
    ESP_UTILS_CHECK_NULL_EXIT(bsp_display_start_with_config(&cfg), "Start display failed");
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        if (timeout_ms < 0) {
            timeout_ms = 0;
        } else if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        ESP_UTILS_CHECK_FALSE_RETURN(bsp_display_lock(timeout_ms), false, "Lock failed");

        return true;
    }, []() {
        bsp_display_unlock();

        return true;
    });

    /* Create a phone object */
    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

    /* Try using a stylesheet that corresponds to the resolution */
    Stylesheet *stylesheet = nullptr;
    if ((BSP_LCD_H_RES == 480) && (BSP_LCD_V_RES == 800)) {
        stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_480_800_DARK);
        ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");
        ESP_UTILS_LOGI("Using 480x800 stylesheet for native display resolution");
    }
    if (stylesheet) {
        ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
        delete stylesheet;
    }

    {
        // When operating on non-GUI tasks, should acquire a lock before operating on LVGL
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");

        /* Init and install apps from registry */
        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_LOGI("Before registry init - Plugin count: %zu", systems::base::App::Registry::getPluginCount());
        systems::base::App::Registry::forEach([](const auto &plugin) {
            ESP_UTILS_LOGI("  - Registered app: %s (type: %s)", plugin.name.c_str(), plugin.type_name.c_str());
        });
        
        // Force linking of our app components by referencing them
        extern void force_camera_app_link();
        extern void force_calculator_app_link();
        extern void force_settings_app_link();
        force_camera_app_link();
        force_calculator_app_link();
        force_settings_app_link();
        
        ESP_UTILS_LOGI("After forced linking - Plugin count: %zu", systems::base::App::Registry::getPluginCount());
        systems::base::App::Registry::forEach([](const auto &plugin) {
            ESP_UTILS_LOGI("  - Registered app: %s (type: %s)", plugin.name.c_str(), plugin.type_name.c_str());
        });
        
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_LOGI("Registry found %d apps", inited_apps.size());
        for (const auto& app_info : inited_apps) {
            ESP_UTILS_LOGI("Registry app: %s", std::get<0>(app_info).c_str());
        }
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );
        }, 1000, phone);
    }

    ESP_UTILS_LOGI("setup done");
}

#pragma GCC diagnostic pop

