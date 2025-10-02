# ESP32-P4 JC4880P433C Phone Project

ESP-IDF phone application using ESP-Brookesia framework for the JC4880P433C development board with ESP32-P4.

## Hardware

- **Board**: JC4880P433C development board
- **MCU**: ESP32-P4 RISC-V dual-core processor
- **Display**: 4.3" 480x800 ST7701 MIPI-DSI LCD with GT911 touch
- **Framework**: ESP-Brookesia phone UI framework

## Features

- Complete phone UI framework using ESP-Brookesia
- LVGL-based graphical interface
- Touch screen support
- PWM backlight control
- SPIFFS file system for data storage
- Brightness control demo included

## Dependencies

This project uses the [esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) component for hardware support.

## Getting Started

### Prerequisites

- ESP-IDF v5.5.1 or later
- ESP32-P4 development environment

### Building

1. Clone this repository:
```bash
git clone https://github.com/csvke/phone_p4_JC4880P433C.git
cd phone_p4_JC4880P433C
```

2. Set up ESP-IDF environment:
```bash
source ~/esp/esp-idf/export.sh
```

3. Configure and build:
```bash
idf.py build
```

4. Flash to device:
```bash
idf.py flash monitor
```

## Project Structure

```
phone_p4_JC4880P433C/
├── main/                 # Main application code
├── demo/                 # Demo applications
│   └── brightness_test/  # Brightness control demo
├── spiffs/              # SPIFFS filesystem data
├── CMakeLists.txt       # Project configuration
└── README.md           # This file
```

## Demos

### Brightness Test

Located in `demo/brightness_test/`, this demonstrates:
- PWM backlight control
- LVGL UI integration
- Visible brightness adjustment testing

To build and run the brightness demo:
```bash
cd demo/brightness_test
idf.py build flash monitor
```

## Configuration

The hardware-specific configuration is handled by the BSP component. For timing adjustments or pin modifications, refer to the [BSP repository](https://github.com/csvke/esp32_p4_jc4880p433c_bsp).

## Development

### Adding Applications

ESP-Brookesia supports modular app development. To add new applications:

1. Create your app in `components/apps/your_app/`
2. Implement the ESP-Brookesia app interface
3. Register the app in `main.cpp`

### Custom Components

Add custom components to the `components/` directory or use the ESP Component Manager in `main/idf_component.yml`.

## License

This project is licensed under the Apache License 2.0.

## Related Projects

- [esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) - Board Support Package
- [ESP-Brookesia](https://github.com/espressif/esp-brookesia) - Phone UI Framework
