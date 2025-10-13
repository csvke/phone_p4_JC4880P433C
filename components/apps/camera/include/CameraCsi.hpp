#ifndef CAMERA_CSI_HPP
#define CAMERA_CSI_HPP

#include "esp_brookesia.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "lvgl.h"

LV_IMG_DECLARE(img_app_camera);

class CameraCsi : public ESP_Brookesia_PhoneApp {
public:
    CameraCsi();
    ~CameraCsi();

    bool run(void) override;
    bool back(void) override;
    bool init(void) override;

public:
    // UI references (for cleanup only)
    lv_obj_t *start_button;  // Reference to capture button for cleanup
    
    bool camera_initialized;
};

#endif // CAMERA_CSI_HPP