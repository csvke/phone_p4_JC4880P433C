#include "CameraCsi.hpp"

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "camera_preview.h"
#include "ui.h"
}

static const char *TAG = "CameraCsi";

LV_IMG_DECLARE(img_app_camera);

// Constructor
CameraCsi::CameraCsi() 
    : esp_brookesia::systems::phone::App(
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
            .navigation_bar_visual_mode = esp_brookesia::systems::phone::NavigationBar::VisualMode::HIDE,
            .flags = {
                .enable_status_icon_common_size = 0,
                .enable_navigation_gesture = 1,
            },
        }
    )
    , start_button(nullptr)
    , camera_initialized(false)
{
}

// Destructor
CameraCsi::~CameraCsi()
{
    // Stop camera preview if running
    if (camera_preview_is_running()) {
        camera_preview_stop();
    }
}

// Camera control button click event callback
static void camera_control_event_cb(lv_event_t *e)
{
    CameraCsi *app = (CameraCsi *)lv_event_get_user_data(e);
    if (!app) return;
    
    lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t *capture_btn = ui_get_capture_btn();
    
    if (btn == capture_btn) {
        if (camera_preview_is_running()) {
            // Stop camera preview
            esp_err_t ret = camera_preview_stop();
            if (ret == ESP_OK) {
                lv_obj_t *btn_label = lv_obj_get_child(capture_btn, 0);
                if (btn_label) {
                    lv_label_set_text(btn_label, LV_SYMBOL_PLAY);
                }
                ESP_LOGI(TAG, "Camera preview stopped");
            } else {
                ESP_LOGE(TAG, "Failed to stop camera: %s", esp_err_to_name(ret));
            }
        } else {
            // Start camera preview
            ESP_LOGI(TAG, "Starting camera preview...");
            
            // Set preview parent to the designated preview area
            lv_obj_t *preview_area = ui_get_preview_area();
            if (preview_area) {
                camera_preview_set_parent(preview_area);
            }
            
            esp_err_t ret = camera_preview_start();
            if (ret == ESP_OK) {
                lv_obj_t *btn_label = lv_obj_get_child(capture_btn, 0);
                if (btn_label) {
                    lv_label_set_text(btn_label, LV_SYMBOL_PAUSE);
                }
                ESP_LOGI(TAG, "Camera preview started successfully");
            } else {
                ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
            }
        }
    }
}

// Handle back action
bool CameraCsi::back(void)
{
    return false; // Let the system handle the back action
}

// Handle init
bool CameraCsi::init(void)
{
    ESP_LOGI(TAG, "� Camera app init called");
    return true;
}

// Main run function
bool CameraCsi::run(void)
{
    ESP_LOGI(TAG, "Creating camera UI");
    
    // Initialize the camera UI on the active screen
    ui_camera_init(lv_scr_act());
    
    // Get UI components
    lv_obj_t *capture_btn = ui_get_capture_btn();
    
    // Add event handlers for buttons
    if (capture_btn) {
        lv_obj_add_event_cb(capture_btn, camera_control_event_cb, LV_EVENT_CLICKED, this);
    }
    
    // Store references for cleanup (update class members)
    start_button = capture_btn;  // Reuse existing member for cleanup
    
    ESP_LOGI(TAG, "Camera UI created successfully");
    return true;
}

// Register the camera app in the ESP-Brookesia registry
ESP_UTILS_REGISTER_PLUGIN(esp_brookesia::systems::base::App, CameraCsi, "Camera");

// Required force link function for ESP-Brookesia app registration
extern "C" void force_camera_app_link() {
    // This function ensures the camera app is linked and registered
}