# Camera App

A high-performance camera application for ESP32-P4 with MIPI CSI interface, implementing real-time preview at 30 FPS using hardware acceleration.

## Overview

This camera app provides a complete camera solution for the JC4880P443C display module, integrating:
- **MIPI CSI-2** camera interface (1-lane)
- **ESP32-P4 ISP** for color correction and auto functions
- **PPA (Pixel Processing Accelerator)** for scaling and rotation
- **Async DMA** for efficient frame buffer transfers
- **LVGL integration** for UI display

## Architecture

The camera app follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                        CameraApp.cpp                         │
│              (ESP-Brookesia App Wrapper)                     │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ↓
┌─────────────────────────────────────────────────────────────┐
│                         camera.c                             │
│                  (Main Coordinator - 397 lines)              │
│   • Initialization & lifecycle management                    │
│   • Module coordination                                      │
│   • Callback registration                                    │
└──┬────────┬──────────┬──────────┬──────────┬────────────┬───┘
   │        │          │          │          │            │
   ↓        ↓          ↓          ↓          ↓            ↓
┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│ Sensor │ │  ISP   │ │  PPA   │ │  DMA   │ │Display │ │  Task  │
│ Module │ │ Module │ │ Module │ │ Module │ │ Module │ │ Module │
└────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘
```

## Module Structure

### Core Coordinator
- **`camera.c`** (397 lines) - Main coordinator
  - Orchestrates all modules
  - Manages initialization sequence
  - Handles lifecycle and error recovery

### Hardware Modules
- **`camera_sensor.c`** (188 lines) - Camera sensor control
  - SC2336 sensor initialization
  - Resolution: 1920x1080 → 1288x728 (ISP crop)
  - Frame rate: 30 FPS
  - Format: RAW8 (Bayer pattern)

- **`camera_controller.c`** (217 lines) - CSI controller
  - MIPI CSI-2 interface (1-lane, 1200 Mbps)
  - Frame reception and callbacks
  - Buffer management

### Processing Modules
- **`camera_isp.c`** (205 lines) - ISP processor
  - Color correction (CCM)
  - Auto white balance (AWB)
  - Auto exposure (AE)
  - Demosaic and gamma correction
  - Output: RGB888

- **`camera_ppa.c`** (113 lines) - PPA processor
  - Scaling: 1288x728 → 800x480
  - Rotation: 90° clockwise
  - Format conversion
  - Output: RGB565

- **`camera_dma.c`** (108 lines) - DMA transfers
  - Async AXI GDMA
  - PSRAM-to-PSRAM transfers
  - CPU memcpy fallback
  - Transfer size: 768 KB per frame

### Display & Task Modules
- **`camera_display.c`** (207 lines) - LVGL display integration
  - LVGL canvas management
  - Display timer (33ms refresh)
  - Thread-safe buffer updates
  - UI screen creation

- **`camera_task.c`** (223 lines) - Frame processing task
  - FreeRTOS task management
  - Frame reception loop
  - Watchdog handling
  - FPS monitoring and diagnostics

### UI & Integration
- **`CameraApp.cpp`** (92 lines) - ESP-Brookesia wrapper
  - App lifecycle (init, run, back, close)
  - Plugin registration
  - C++ to C bridge

- **`ui/ui.c`** - LVGL UI screens
  - Camera preview screen
  - Control buttons (capture, gallery, settings)

## Data Flow

```
Camera Sensor (SC2336)
    ↓ RAW8 @ 1920x1080
CSI Controller (crop to 1288x728)
    ↓ RAW8 @ 1288x728
ISP Processor
    ↓ RGB888 @ 1288x728
PPA Processor (scale & rotate)
    ↓ RGB565 @ 800x480 (rotated 90°)
Async DMA Transfer
    ↓ 768 KB to scaled_buffer
LVGL Display
    ↓ Canvas update @ 30 FPS
LCD Display (800x480)
```

## Performance Characteristics

- **Frame Rate**: 30 FPS sustained
- **Latency**: ~33ms per frame
- **Memory Usage**:
  - Frame buffer: 2.7 MB (1288x728x3)
  - Scaled buffer: 768 KB (800x480x2)
  - Total PSRAM: ~3.5 MB
- **CPU Usage**: ~25% (task + display timer)
- **DMA Throughput**: 23 MB/s

## API Reference

### Main Camera API (`camera.h`)

```c
// Initialize camera system
esp_err_t camera_init(void);

// Deinitialize and free resources
void camera_deinit(void);

// Start camera preview
esp_err_t camera_start(void);

// Stop camera preview
void camera_stop(void);

// Set LVGL parent container
void camera_set_parent(lv_obj_t *parent);

// Get camera status
bool camera_is_running(void);
```

### Module APIs

Each module provides a focused API:

- **Sensor**: `camera_sensor_init()`, `camera_sensor_deinit()`
- **Controller**: `camera_controller_init()`, `camera_controller_start()`
- **ISP**: `camera_isp_init()`, `camera_isp_deinit()`
- **PPA**: `camera_ppa_init()`, `camera_ppa_process()`
- **DMA**: `camera_dma_init()`, `camera_dma_transfer()`
- **Display**: `camera_display_init()`, `camera_display_update_frame()`
- **Task**: `camera_task_start()`, `camera_task_stop()`

## Build Configuration

### CMakeLists.txt

```cmake
idf_component_register(
    SRCS 
        "src/camera.c"
        "src/camera_dma.c"
        "src/camera_ppa.c"
        "src/camera_isp.c"
        "src/camera_sensor.c"
        "src/camera_controller.c"
        "src/camera_display.c"
        "src/camera_task.c"
        "src/CameraApp.cpp"
        "ui/ui.c"
        "assets/camera_app_icon.c"
    INCLUDE_DIRS 
        "include" 
        "ui" 
        "assets"
    PRIV_REQUIRES 
        brookesia_core 
        lvgl 
        freertos 
        esp_cam_sensor 
        esp_driver_cam 
        esp_driver_isp 
        esp_driver_ppa 
        esp32_p4_jc4880p433c_bsp 
        driver 
        esp_timer 
        heap 
        esp_lvgl_port
)
```

### Dependencies

- **esp-idf**: v5.5.1
- **esp-brookesia**: Phone UI framework
- **LVGL**: v8.3.x
- **esp_cam_sensor**: Camera sensor drivers
- **esp32_p4_jc4880p433c_bsp**: Board support package

## Hardware Configuration

### Camera Sensor (SC2336)
- **Interface**: MIPI CSI-2 (1-lane)
- **Resolution**: 1920x1080 native
- **Output**: RAW8 Bayer pattern
- **Frame Rate**: 30 FPS
- **I2C Address**: 0x30

### Display (ST7701S)
- **Interface**: RGB parallel
- **Resolution**: 800x480
- **Orientation**: Portrait (90° rotation)
- **Color Format**: RGB565

### Memory
- **PSRAM**: 16 MB (frame buffers)
- **Flash**: 6 MB available for app

## Usage Example

### Basic Usage

```cpp
#include "CameraApp.hpp"

// The camera app is auto-registered with ESP-Brookesia
// It appears in the phone's app menu as "Camera"

// When user taps the Camera icon:
// 1. CameraApp::run() is called
// 2. camera_start() initializes and starts preview
// 3. Preview runs at 30 FPS until user exits

// When user presses back:
// 1. CameraApp::back() is called
// 2. camera_stop() halts preview
// 3. Resources are released
```

### Manual Control

```c
#include "camera.h"

void my_camera_function(void) {
    // Initialize camera
    ESP_ERROR_CHECK(camera_init());
    
    // Set LVGL parent container
    camera_set_parent(lv_scr_act());
    
    // Start preview
    ESP_ERROR_CHECK(camera_start());
    
    // ... camera runs ...
    
    // Stop preview
    camera_stop();
    
    // Clean up
    camera_deinit();
}
```

## Diagnostics

The camera app provides detailed runtime diagnostics:

```
I (12345) camera: 📊 Status: 30 fps | Callbacks: cam=30 ppa=30 | Sem: cam=0 ppa=0
I (12345) camera: Frame time: 33ms (min=32ms, max=35ms)
I (12345) camera_dma: DMA transfer: 768000 bytes in 33ms (23.27 MB/s)
```

### Status Indicators
- **fps**: Current frame rate
- **Callbacks**: Callback counts per second (camera, PPA)
- **Sem**: Semaphore backlog (should be 0-1 for healthy operation)
- **Frame time**: Per-frame processing time

## Troubleshooting

### Low Frame Rate (< 30 FPS)
- Check semaphore backlog in status logs
- Verify DMA transfer speed
- Check CPU usage with `vTaskGetRunTimeStats()`
- Increase task priority if needed

### No Preview Display
- Verify `camera_set_parent()` called before `camera_start()`
- Check LVGL display timer is running
- Verify PSRAM is properly initialized

### Sensor Initialization Failure
- Check I2C bus connectivity
- Verify sensor I2C address (0x30)
- Check power supply voltage (3.3V)
- Review sensor probe logs

### Memory Issues
- Monitor PSRAM usage: 3.5 MB required minimum
- Check for memory leaks with heap tracing
- Verify frame buffers allocated in PSRAM

## Development

### Adding New Features

1. **New hardware module**: Create separate `.c` file with focused API
2. **Update coordinator**: Modify `camera.c` to integrate module
3. **Update CMakeLists.txt**: Add new source file
4. **Document API**: Update this README

### Code Style
- Use C99 for camera modules
- Use C++ only for Brookesia integration
- Follow ESP-IDF coding conventions
- Keep modules under 250 lines when possible

### Testing
1. Build: `idf.py build`
2. Flash: `idf.py flash`
3. Monitor: `idf.py monitor`
4. Check logs for FPS and diagnostics

## Refactoring History

This camera app has undergone 5 phases of refactoring:

- **Phase 1**: Created modular header interfaces (7 headers)
- **Phase 2**: Extracted peripheral modules (DMA, PPA, ISP)
- **Phase 3**: Extracted hardware modules (Sensor, Controller)
- **Phase 4**: Implemented display module (LVGL integration)
- **Phase 5**: Extracted task management module

**Result**: Main coordinator reduced from 736 to 397 lines (46% reduction)

## License

This component is part of the ESP32-P4 JC4880P443C phone project.

## Related Documentation

- [ESP32-P4 Camera Driver API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/camera_driver.html)
- [ESP32-P4 ISP Driver API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/isp.html)
- [ESP-Brookesia Documentation](https://github.com/espressif/esp-brookesia)
- [LVGL Documentation](https://docs.lvgl.io/)
