#include "CameraApp.hpp"
#include "camera.h"
#include "esp_log.h"

#define TAG "CameraApp"

// Register the camera app in the ESP-Brookesia registry
ESP_UTILS_REGISTER_PLUGIN(esp_brookesia::systems::base::App, CameraApp, "Camera");

// Force link function to ensure camera app is registered
extern "C" void force_camera_app_link() {
    // This function ensures the camera app is linked and registered
}

CameraApp::CameraApp()
    : ESP_Brookesia_PhoneApp("Camera", &camera_app_icon, true)
    , camera_running(false)
{
    ESP_LOGI(TAG, "CameraApp constructed");
}

CameraApp::~CameraApp()
{
    ESP_LOGI(TAG, "CameraApp destroyed");
    if (camera_running) {
        camera_stop();
        camera_deinit();
    }
}

bool CameraApp::init(void)
{
    ESP_LOGI(TAG, "Initializing Camera app");
    
    // Note: Actual camera initialization happens in run()
    // This keeps the app lightweight until it's actually launched
    
    return true;
}

bool CameraApp::run(void)
{
    ESP_LOGI(TAG, "Running Camera app");
    
    // Set the LVGL parent container for the camera preview (use active screen)
    camera_set_parent(lv_scr_act());
    
    // Start the camera (this will initialize CSI controller, ISP, and display)
    esp_err_t ret = camera_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera: %s", esp_err_to_name(ret));
        return false;
    }
    
    camera_running = true;
    ESP_LOGI(TAG, "Camera started successfully");
    
    return true;
}

bool CameraApp::back(void)
{
    ESP_LOGI(TAG, "Back pressed in Camera app");
    
    if (camera_running) {
        camera_stop();
        camera_running = false;
        ESP_LOGI(TAG, "Camera stopped");
    }
    
    return true;
}

bool CameraApp::close(void)
{
    ESP_LOGI(TAG, "Closing Camera app");
    
    if (camera_running) {
        camera_stop();
        camera_running = false;
    }
    
    // Clean up all camera resources to allow reopening
    camera_deinit();
    
    return true;
}
