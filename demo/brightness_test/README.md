# Brightness Control Demo

This folder contains the brightness control test and demo code for the JC4880P443C display.

## Files:

- `brightness_manual_test.c` - Complete brightness test with visible content
- `brightness_test_main.cpp` - Alternative C++ brightness test program
- `BRIGHTNESS_TEST_GUIDE.md` - Comprehensive testing guide
- `BRIGHTNESS_TEST_SUMMARY.md` - Test summary and results
- `CMakeLists.txt` - Build configuration for using as main component

## How to Use This Demo:

### Option 1: Quick Test (Recommended)
Add brightness control functions to your main Brookesia phone app:

```cpp
// Add to your main.cpp
#include "bsp/esp-bsp.h"

// Test brightness control
void test_brightness_control() {
    bsp_display_brightness_set(0);    // Darkest
    vTaskDelay(pdMS_TO_TICKS(2000));
    bsp_display_brightness_set(50);   // Medium  
    vTaskDelay(pdMS_TO_TICKS(2000));
    bsp_display_brightness_set(100);  // Brightest
}
```

### Option 2: Run Complete Demo
To run the complete brightness test demo:

1. **Replace main component:**
   ```bash
   cd /path/to/phone_p4_JC4880P433C
   cp demo/brightness_test/brightness_manual_test.c main/
   cp demo/brightness_test/CMakeLists.txt main/
   mv main/main.cpp main/main.cpp.backup
   ```

2. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

3. **Restore original app:**
   ```bash
   mv main/main.cpp.backup main/main.cpp
   rm main/brightness_manual_test.c
   # Restore original CMakeLists.txt
   idf.py build flash
   ```

## Brightness Control API:

The JC4880P443C BSP provides these functions for brightness control:

```c
#include "bsp/esp-bsp.h"

// Turn on backlight (call once during initialization)
esp_err_t bsp_display_backlight_on(void);

// Set brightness (0-100%)
esp_err_t bsp_display_brightness_set(uint8_t brightness_percent);
```

## Integration with Brookesia Phone App:

To add brightness control to your phone settings:

1. **Create a brightness settings screen**
2. **Add slider control for brightness adjustment**
3. **Call `bsp_display_brightness_set()` when slider changes**
4. **Save brightness setting to NVS for persistence**

## Hardware Details:

- **Backlight GPIO**: GPIO23 (confirmed working)
- **PWM Timer**: LEDC Timer (configurable via menuconfig)
- **PWM Channel**: LEDC Channel (configurable via menuconfig)
- **PWM Frequency**: ~5kHz (configurable via menuconfig)

## Test Results:

✅ **Brightness control confirmed working**
- GPIO23 correctly controls display brightness
- PWM duty cycle range: 0-1023 (0%-100%)
- All LEDC operations successful
- Visible brightness changes confirmed
- Hardware implementation matches schematic

## Notes:

- The warning `GPIO 23 is not usable, maybe conflict with others` is a false positive
- GPIO23 is correctly assigned for LCD PWM per the schematic
- Brightness control works perfectly despite the warning
- This is ready for integration into the main phone application settings