#pragma once
// Compatibility shim to allow using 480x800 stylesheet macro name on releases
// that don't yet ship it. When the upstream macro becomes available, this shim
// will be a no-op.

#include "esp_brookesia.hpp"

#ifndef ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET
#define ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET() ESP_BROOKESIA_PHONE_800_480_DARK_STYLESHEET()
#endif
