/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Calculator App for ESP-Brookesia Phone System
 * Adapted for ESP32-P4 JC4880P433C
 */
#pragma once

#include "lvgl.h"
#include "esp_brookesia.hpp"

namespace phone_apps {

class Calculator: public esp_brookesia::systems::phone::App {
public:
    Calculator();
    ~Calculator();

    // Core app interface methods
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;

    // Calculator logic methods
    bool isStartZero(void);
    bool isStartNum(void);
    bool isStartPercent(void);
    bool isLegalDot(void);
    double calculate(const char *input);

    int formula_len;
    lv_obj_t *keyboard;
    lv_obj_t *history_label;
    lv_obj_t *formula_label;
    lv_obj_t *result_label;
    uint16_t _height;
    uint16_t _width;

private:
    static void keyboard_event_cb(lv_event_t *e);
};

} // namespace phone_apps
