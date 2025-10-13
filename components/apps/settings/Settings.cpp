/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "Settings.hpp"
#include "esp_lib_utils.h"
#include <cinttypes>

static const char *TAG = "AppSettings";

#define NVS_STORAGE_NAMESPACE           "storage"
#define NVS_KEY_DISPLAY_BRIGHTNESS      "brightness"

#define SCREEN_BRIGHTNESS_MIN           (20)
#define SCREEN_BRIGHTNESS_MAX           (100)

LV_IMG_DECLARE(img_app_settings);

AppSettings::AppSettings():
    ESP_Brookesia_PhoneApp("Settings", &img_app_settings, false),
    _is_ui_resumed(false),
    _is_ui_del(true),
    _screen_index(UI_MAIN_SETTING_INDEX),
    _screen_list({nullptr})
{
    ESP_LOGI(TAG, "AppSettings constructed");
}

AppSettings::~AppSettings()
{
    ESP_LOGI(TAG, "AppSettings destroyed");
}

bool AppSettings::run(void)
{
    ESP_LOGI(TAG, "Running Settings app");
    _is_ui_del = false;

    // Create simple UI
    extra_ui_init();

    return true;
}

bool AppSettings::back(void)
{
    ESP_LOGI(TAG, "Back pressed");
    _is_ui_resumed = false;

    if (_screen_index != UI_MAIN_SETTING_INDEX) {
        // Go back to main settings screen
        _screen_index = UI_MAIN_SETTING_INDEX;
        lv_scr_load(_screen_list[UI_MAIN_SETTING_INDEX]);
        return true;
    } else {
        // On main screen, close the app
        notifyCoreClosed();
        return true;
    }
}

bool AppSettings::close(void)
{
    ESP_LOGI(TAG, "Closing Settings app");
    _is_ui_del = true;

    // Clean up all screens
    for (int i = 0; i < UI_MAX_INDEX; i++) {
        if (_screen_list[i]) {
            lv_obj_del(_screen_list[i]);
            _screen_list[i] = nullptr;
        }
    }

    return true;
}

bool AppSettings::init(void)
{
    ESP_LOGI(TAG, "Initializing Settings app");

    // Initialize NVS if needed
    esp_err_t err = nvs_flash_init_partition(NVS_DEFAULT_PART_NAME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }

    return true;
}

bool AppSettings::pause(void)
{
    _is_ui_resumed = false;
    return true;
}

bool AppSettings::resume(void)
{
    _is_ui_resumed = true;
    return true;
}

void AppSettings::extra_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing simple UI");

    // Create main settings screen
    _screen_list[UI_MAIN_SETTING_INDEX] = lv_obj_create(nullptr);
    lv_obj_set_style_pad_all(_screen_list[UI_MAIN_SETTING_INDEX], 0, 0);

    // Create list for menu items
    lv_obj_t *list = lv_list_create(_screen_list[UI_MAIN_SETTING_INDEX]);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 0);

    // Add menu items
    lv_obj_t *btn;

    // WiFi settings
    btn = lv_list_add_btn(list, nullptr, "WiFi Settings");
    lv_obj_add_event_cb(btn, onMainMenuClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)UI_WIFI_SCAN_INDEX);

    // Bluetooth settings
    btn = lv_list_add_btn(list, nullptr, "Bluetooth");
    lv_obj_add_event_cb(btn, onMainMenuClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)UI_BLUETOOTH_SETTING_INDEX);

    // Volume settings
    btn = lv_list_add_btn(list, nullptr, "Volume");
    lv_obj_add_event_cb(btn, onMainMenuClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)UI_VOLUME_SETTING_INDEX);

    // Brightness settings
    btn = lv_list_add_btn(list, nullptr, "Brightness");
    lv_obj_add_event_cb(btn, onMainMenuClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)UI_BRIGHTNESS_SETTING_INDEX);

    // About
    btn = lv_list_add_btn(list, nullptr, "About");
    lv_obj_add_event_cb(btn, onMainMenuClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)UI_ABOUT_SETTING_INDEX);

    // Create placeholder screens for each section
    for (int i = UI_WIFI_SCAN_INDEX; i < UI_MAX_INDEX; i++) {
        _screen_list[i] = lv_obj_create(nullptr);
        lv_obj_set_style_pad_all(_screen_list[i], 20, 0);
        
        lv_obj_t *label = lv_label_create(_screen_list[i]);
        switch (i) {
            case UI_WIFI_SCAN_INDEX:
                lv_label_set_text(label, "WiFi Settings\n\nComing soon...");
                break;
            case UI_BLUETOOTH_SETTING_INDEX:
                lv_label_set_text(label, "Bluetooth Settings\n\nComing soon...");
                break;
            case UI_VOLUME_SETTING_INDEX:
                lv_label_set_text(label, "Volume Settings\n\nComing soon...");
                break;
            case UI_BRIGHTNESS_SETTING_INDEX:
                lv_label_set_text(label, "Brightness\n\nComing soon...");
                break;
            case UI_ABOUT_SETTING_INDEX:
                update_system_info(label);
                break;
            default:
                lv_label_set_text(label, "Settings");
                break;
        }
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }

    // Load main screen
    lv_scr_load(_screen_list[UI_MAIN_SETTING_INDEX]);

    ESP_LOGI(TAG, "UI initialized");
}

void AppSettings::update_system_info(lv_obj_t *label)
{
    if (!label) {
        return;
    }

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char *idf_version = esp_get_idf_version();

    char info[512];
    snprintf(info, sizeof(info),
        "System Information\n\n"
        "Chip: %s\n"
        "Cores: %d\n"
        "Features: %s%s%s%s\n"
        "Silicon Rev: %d\n"
        "ESP-IDF: %s\n\n"
        "Free Heap: %" PRIu32 " bytes\n"
        "Min Free Heap: %" PRIu32 " bytes",
        CONFIG_IDF_TARGET,
        chip_info.cores,
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
        (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
        (chip_info.features & CHIP_FEATURE_IEEE802154) ? "802.15.4 " : "",
        chip_info.revision,
        idf_version,
        esp_get_free_heap_size(),
        esp_get_minimum_free_heap_size()
    );

    lv_label_set_text(label, info);
}

void AppSettings::onMainMenuClickedEventCallback(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    AppSettings *self = (AppSettings *)lv_event_get_user_data(e);
    SettingScreenIndex_t index = (SettingScreenIndex_t)(intptr_t)lv_obj_get_user_data(btn);

    ESP_LOGI(TAG, "Main menu clicked: %d", index);
    self->_screen_index = index;

    if (index < UI_MAX_INDEX && self->_screen_list[index]) {
        lv_scr_load(self->_screen_list[index]);
    }
}

// Debug: Check if this code is being executed
static bool __attribute__((used)) settings_registration_debug = []() {
    ESP_LOGI("AppSettings", "Settings app registration code is being executed");
    return true;
}();

// Register the settings app in the ESP-Brookesia registry  
ESP_UTILS_REGISTER_PLUGIN(esp_brookesia::systems::base::App, AppSettings, "Settings");

// Force linking function
extern "C" void force_settings_app_link() {
    // This function ensures the registrar static variable is not optimized away
}
