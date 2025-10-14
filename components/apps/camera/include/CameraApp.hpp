#ifndef CAMERA_APP_HPP
#define CAMERA_APP_HPP

#include "esp_brookesia.hpp"
#include "camera_app_icon.h"

/**
 * @brief Camera application wrapper for ESP-Brookesia
 * 
 * This is a minimal C++ wrapper that bridges the pure C camera implementation
 * with the ESP-Brookesia app registry system. The actual camera logic is
 * implemented in camera.c using the Camera Controller Driver API.
 */
class CameraApp : public ESP_Brookesia_PhoneApp {
public:
    CameraApp();
    ~CameraApp();

    bool run(void) override;
    bool back(void) override;
    bool init(void) override;
    bool close(void) override;

private:
    bool camera_running;
};

#endif // CAMERA_APP_HPP
