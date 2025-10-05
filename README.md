# ESP32-P4 Brookesia Phone UI for JC4880P433C

A production-ready phone interface using ESP-Brookesia framework on the JC4880P433C development board with ESP32-P4.

## 🎯 Features

- ✨ **Complete Phone UI**: App launcher, navigation bar, status bar, and recents screen
- 📱 **480x800 Native Display**: Optimized dark theme stylesheet
- 👆 **Touch Support**: GT911 capacitive touch controller with explicit device configuration
- 🎨 **LVGL 9.2.2**: Hardware-accelerated graphics with software rotation
- 💾 **SPIFFS Storage**: Auto-formatting support for user data (4MB partition)
- 🔧 **Production Ready**: Clean boot with zero errors/warnings
- 🎮 **Gesture Navigation**: Swipe gestures for home, back, and app switching

## 🔧 Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **Board** | JC4880P433C Development Board |
| **MCU** | ESP32-P4 RISC-V Dual-Core @ 360MHz |
| **PSRAM** | 32MB PSRAM @ 200MHz |
| **Flash** | 16MB Flash (QIO mode) |
| **Display** | 4.3" 480x800 ST7701S MIPI-DSI LCD |
| **Touch** | GT911 I2C Capacitive Touch |
| **Interface** | USB-Serial/JTAG |

## 📦 Software Stack

- **ESP-IDF**: v5.5.1
- **Brookesia**: v0.5.0 (customized with 480x800 stylesheet from [feat/add_phone_stylesheet_480_800](https://github.com/espressif/esp-brookesia/tree/feat/add_phone_stylesheet_480_800))
- **LVGL**: v9.2.2
- **BSP**: [esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp)

## 🚀 Quick Start

### Prerequisites

- [ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/get-started/index.html)
- ESP32-P4 development environment configured
- JC4880P433C board with USB connection

### Build and Flash

1. **Clone the repository**:
```bash
git clone --recursive https://github.com/csvke/phone_p4_JC4880P433C.git
cd phone_p4_JC4880P433C
```

2. **Set up ESP-IDF environment**:
```bash
source ~/esp/esp-idf/export.sh  # Adjust path to your ESP-IDF installation
```

3. **Build the project**:
```bash
idf.py build
```

4. **Flash to device**:
```bash
idf.py flash monitor
```

### Local BSP Development (Optional)

By default, the BSP is automatically downloaded from GitHub via the component manager. If you want to modify the BSP or develop locally:

1. **Clone the BSP repository** alongside your project:
```bash
cd /path/to/your/workspace
git clone https://github.com/csvke/esp32_p4_jc4880p433c_bsp.git
```

2. **Update CMakeLists.txt** to use local BSP:
```cmake
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/components
    ${CMAKE_CURRENT_LIST_DIR}/../esp32_p4_jc4880p433c_bsp  # Add this line
)
```

3. **Comment out BSP in idf_component.yml**:
```yaml
# csvke/esp32_p4_jc4880p433c_bsp:
#   git: "https://github.com/csvke/esp32_p4_jc4880p433c_bsp.git"
```

4. **Rebuild the project**:
```bash
idf.py fullclean
idf.py build
```

**When to use local BSP:**
- 🔧 Developing or debugging BSP features
- 🎨 Customizing display timing or touch parameters
- 📝 Contributing to the BSP repository
- 🧪 Testing BSP changes before pushing

**Note:** Remember to switch back to the component manager version before committing your changes unless you're specifically modifying the BSP.

---

**Expected boot output**:
```
I (1172) app_init: Project name:     phone_p4_JC4880P433C
I (1177) app_init: App version:      ee54e9e-dirty
I (1626) main: Display started successfully
I (1630) main: GT911 touch device explicitly set (@0x4ff41de8)
I (1640) main: 480x800 stylesheet activated for native display resolution
I (1646) BS:Core: Library version: 0.6.0
I (1681) main: setup done
```

## 📁 Project Structure

```
phone_p4_JC4880P433C/
├── components/
│   └── espressif__esp-brookesia/  # Brookesia UI framework (customized)
├── main/
│   ├── main.cpp                   # Application entry point
│   ├── idf_component.yml          # Component dependencies
│   └── stylesheet_compat.h        # Stylesheet compatibility layer
├── CMakeLists.txt                 # Project build configuration
├── partitions.csv                 # Flash partition table
├── sdkconfig.defaults             # Default project configuration
└── README.md                      # This file
```

## 🎨 Key Implementations

### Display Configuration
- **Resolution**: 480x800 portrait orientation
- **Software Rotation**: Enabled (ST7701S doesn't support hardware rotation)
- **Color Format**: RGB565
- **PSRAM Buffers**: 480 × 80 pixels (double buffering disabled for memory efficiency)
- **Backlight**: PWM-controlled via GPIO

### Touch Configuration
- **Controller**: GT911 on I2C (SCL=GPIO8, SDA=GPIO7)
- **Explicit Device Setting**: Touch device passed directly to Brookesia to avoid auto-detection
- **External Pull-ups**: Hardware pull-ups present (I2C warnings suppressed)

### Storage Partitions
| Partition | Type | Size | Purpose |
|-----------|------|------|---------|
| nvs | data | 24KB | Non-volatile storage |
| phy_init | data | 4KB | PHY initialization |
| factory | app | 7MB | Main application |
| storage | spiffs | 4MB | User data (auto-format enabled) |
| anim_emotion | data | 2MB | Emotion animations |
| anim_boot | data | 1MB | Boot animations |
| anim_icon | data | 1.5MB | Icon animations |

## 🔧 Configuration

### Custom Brookesia Version

This project uses a **customized ESP-Brookesia v0.5.0** with 480x800 stylesheet support:

**Base**: Official ESP-Brookesia v0.5.0 from [ESP Component Registry](https://components.espressif.com/components/espressif/esp-brookesia)

**Custom Addition**: 480x800 phone stylesheet manually incorporated from Espressif's feature branch:
- Source: [feat/add_phone_stylesheet_480_800](https://github.com/espressif/esp-brookesia/tree/feat/add_phone_stylesheet_480_800)
- Files added: `systems/phone/stylesheets/480_800/` directory with dark theme
- Reason: Official v0.5.0 only includes 320x240 and 800x480 stylesheets

**Why Custom?**
- Native 480x800 portrait display requires specific stylesheet
- Feature branch not yet merged or released in official version
- Allows optimal UI layout for 4.3" 480x800 portrait screen

**Version Identifier**: `0.5.0+480x800` (in `idf_component.yml`)

### Key Changes from Default Brookesia
1. **Moved Brookesia to `components/`**: Allows customization without managed component conflicts
2. **Version Macros**: Added compile definitions for BROOKESIA_CORE_VER (0.5.0)
3. **480x800 Stylesheet**: Manually added from feature branch for native display support
4. **Software Rotation**: Enabled `.sw_rotate = true` for ST7701S compatibility
5. **BSP from GitHub**: Uses [esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) from component manager with SPIFFS auto-format
6. **Namespace Types**: Using `esp_brookesia::systems::phone::Phone` (no deprecation warnings)

### Customizing Display Settings
Edit `main/main.cpp` to adjust display configuration:
```cpp
bsp_display_cfg_t cfg = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = BSP_LCD_H_RES * 80,  // Adjust buffer size
    .double_buffer = false,              // Enable for smoother rendering
    .flags = {
        .buff_dma = false,
        .buff_spiram = true,
        .sw_rotate = true,               // Required for ST7701S
    }
};
```

## 📱 Development

### Adding Phone Apps
Brookesia supports modular phone app development:

```cpp
// Create your custom app
class MyApp : public esp_brookesia::systems::phone::App {
public:
    MyApp() : App("MyApp") {}
    bool run() override {
        // Your app logic
        return true;
    }
};

// In main.cpp, register your app
MyApp *myApp = new MyApp();
phone->installApp(myApp);
```

### Available UI Components
- **App Launcher**: Home screen with app icons
- **Navigation Bar**: Bottom navigation (home, back, recents)
- **Status Bar**: Top bar with battery, WiFi, time
- **Recents Screen**: App switcher with snapshots
- **Gesture Support**: Swipe gestures for navigation

## 🐛 Troubleshooting

### Build Issues
- **Brookesia hash mismatch**: Brookesia is in `components/` folder (not `managed_components/`)
- **BSP not downloaded**: Run `idf.py reconfigure` to fetch BSP from GitHub
- **Component manager errors**: Ensure you have internet connection for first build
- **LVGL version error**: Project requires LVGL 9.2.2 (automatically managed)

### Runtime Issues
- **Display not working**: Check MIPI-DSI connections and power (LDO ch3: 2500mV)
- **Touch not responding**: Verify GT911 I2C connection (SCL=GPIO8, SDA=GPIO7)
- **SPIFFS mount failed**: First boot auto-formats SPIFFS (takes ~1 second)

## 📚 References

- [ESP-Brookesia Documentation](https://github.com/espressif/esp-brookesia)
- [ESP32-P4 Technical Reference](https://www.espressif.com/en/products/socs/esp32-p4)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/)
- [LVGL Documentation](https://docs.lvgl.io/9.2/)
- [BSP Repository](https://github.com/csvke/esp32_p4_jc4880p433c_bsp)

## 📝 License

This project is licensed under the Apache License 2.0 - see the LICENSE file for details.

## 🔗 Related Projects

- [esp32_p4_jc4880p433c_bsp](https://github.com/csvke/esp32_p4_jc4880p433c_bsp) - Board Support Package with I2C, display, and touch initialization
- [ESP-Brookesia](https://github.com/espressif/esp-brookesia) - Espressif's phone UI framework for embedded systems

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## 👤 Author

**csvke**
- GitHub: [@csvke](https://github.com/csvke)

---

**Status**: ✅ Production Ready | **Last Updated**: October 6, 2025
