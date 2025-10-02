# JC4880P443C Brightness Control Testing Guide

## Overview
This guide helps you test the brightness control functionality on your JC4880P443C display board.

## Prerequisites
1. ESP32-P4 board with JC4880P443C display connected
2. ESP-IDF environment set up
3. Project built successfully (compilation errors fixed)

## Testing Methods

### Method 1: Quick Function Test (Recommended)
Use the existing BSP functions to test brightness control:

```c
#include "bsp/esp-bsp.h"

// Test in your main application
void test_brightness_control(void) {
    // Initialize display first
    lv_display_t *disp = bsp_display_start();
    bsp_display_backlight_on();
    
    // Test different brightness levels
    bsp_display_brightness_set(0);    // 0% - darkest
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    bsp_display_brightness_set(50);   // 50% - medium
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    bsp_display_brightness_set(100);  // 100% - brightest
    vTaskDelay(pdMS_TO_TICKS(2000));
}
```

### Method 2: Comprehensive Test Program
1. Replace your `main/main.cpp` with `brightness_test_main.cpp` (provided above)
2. Build and flash: `idf.py build flash monitor`
3. Observe the display brightness cycling through different levels

### Method 3: Manual Hardware Test
1. Use `brightness_manual_test.c` to test PWM directly
2. This bypasses the BSP layer to test the hardware directly

## Configuration Check

### 1. Verify GPIO Configuration
Check your `sdkconfig` file for these settings:
```
CONFIG_BSP_JC4880P443C_LCD_BL_GPIO=??        # Backlight GPIO pin
CONFIG_BSP_JC4880P443C_BACKLIGHT_TIMER=??    # LEDC timer
CONFIG_BSP_JC4880P443C_BACKLIGHT_CHANNEL=??  # LEDC channel
CONFIG_BSP_JC4880P443C_BACKLIGHT_PWM_FREQ=?? # PWM frequency
```

### 2. Check menuconfig
Run `idf.py menuconfig` and navigate to:
- Component config → Board Support Package (BSP) → JC4880P443C Configuration
- Verify backlight GPIO pin and PWM settings

## Expected Behavior

### ✅ Working Brightness Control:
- Display brightness visibly changes between levels
- 0% should be very dim (may appear off but screen content still visible)
- 100% should be brightest
- No error messages in serial monitor
- Serial output shows "SUCCESS" for brightness operations

### ❌ Not Working - Possible Issues:

#### 1. No Brightness Change
**Symptoms:** Display stays at same brightness regardless of setting
**Causes:**
- Wrong GPIO pin configured
- Hardware issue with backlight circuit
- PWM not reaching the backlight driver

**Debug Steps:**
```bash
# Check GPIO pin with oscilloscope/multimeter
# Pin should show PWM signal with changing duty cycle

# Check if GPIO is being driven
idf.py monitor
# Look for PWM configuration messages
```

#### 2. Error Messages
**Common Errors:**
- `ESP_ERR_INVALID_ARG`: Invalid brightness percentage (>100)
- `ESP_FAIL`: Hardware configuration failed
- GPIO/LEDC errors: Pin or timer configuration issues

#### 3. Display Goes Completely Black
**Symptoms:** Display turns off at low brightness
**Cause:** Backlight circuit may not support very low PWM duties
**Solution:** Limit minimum brightness to ~10%

## Hardware Verification

### Check Backlight Circuit:
1. **Measure GPIO pin with multimeter/oscilloscope**
   - Should show 3.3V PWM signal
   - Duty cycle should change with brightness setting
   - Frequency should match configuration (usually 1-10kHz)

2. **Check Backlight Power Supply**
   - Verify backlight circuit has proper power
   - Check for any enable pins that need to be set

3. **Visual Inspection**
   - Ensure all connections are secure
   - Check for any damaged components

## Troubleshooting Commands

```bash
# Monitor with detailed logging
idf.py monitor

# Check configuration
idf.py menuconfig

# Clean build if having issues
idf.py fullclean
idf.py build flash monitor

# Check GPIO state (if available)
# In monitor, you can check registers to see if GPIO is configured correctly
```

## Testing Script for Serial Monitor

You can also test interactively by adding commands to your main app:

```c
// Add to your main loop
char input[10];
if (fgets(input, sizeof(input), stdin)) {
    int brightness = atoi(input);
    if (brightness >= 0 && brightness <= 100) {
        ESP_LOGI(TAG, "Setting brightness to %d%%", brightness);
        esp_err_t ret = bsp_display_brightness_set(brightness);
        ESP_LOGI(TAG, "Result: %s", esp_err_to_name(ret));
    }
}
```

## Expected Serial Output (Working System)

```
I (1234) brightness_test: === Quick Brightness Test ===
I (1235) brightness_test: Setting to 0% (minimum)
I (1236) brightness_test: Result: SUCCESS
I (3240) brightness_test: Setting to 100% (maximum)
I (3241) brightness_test: Result: SUCCESS
I (5245) brightness_test: Setting to 50% (medium)
I (5246) brightness_test: Result: SUCCESS
I (5247) brightness_test: === Quick test complete ===
```

## Notes
- The JC4880P443C uses PWM for brightness control via LEDC peripheral
- Brightness control is handled by the `bsp_display_brightness_set()` function
- Valid range is 0-100 (percentage)
- Changes should be immediately visible on the display
- Some displays may have a minimum brightness threshold below which they appear off