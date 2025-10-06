# Build Instructions

## Prerequisites
- ESP-IDF v5.5.1 or later
- Git with submodule support

## First Time Setup

1. **Clone the repository with submodules:**
   ```bash
   git clone --recursive https://github.com/csvke/phone_p4_JC4880P433C.git
   cd phone_p4_JC4880P433C
   ```

   Or if you already cloned without `--recursive`:
   ```bash
   git submodule update --init --recursive
   ```

2. **Set up ESP-IDF environment:**
   ```bash
   # Source the ESP-IDF environment (adjust path to your IDF installation)
   . $HOME/esp/esp-idf-versions/esp-idf-v5.5.1/export.sh
   # Or if using standard installation:
   # . $HOME/esp/esp-idf/export.sh
   ```

3. **Build the project:**
   ```bash
   idf.py build
   ```

4. **Flash to device:**
   ```bash
   idf.py -p /dev/cu.usbmodem1101 flash monitor
   # Adjust the port to match your device
   ```

## About esp-brookesia Submodule

This project uses a local fork of esp-brookesia as a git submodule to support:
- Custom 480x800 phone stylesheet
- Calculator app with snapshot support
- Disabled AI framework and speaker system (not needed for this phone project)

The submodule is configured to track the `feat/add_phone_stylesheet_480_800` branch.

## Configuration Notes

- **AI Framework**: Disabled via `CONFIG_ESP_BROOKESIA_ENABLE_AI_FRAMEWORK=n`
- **Speaker System**: Disabled via `CONFIG_ESP_BROOKESIA_SYSTEMS_ENABLE_SPEAKER=n`
- **LVGL Memory**: Uses CLIB malloc (`CONFIG_LV_USE_CLIB_MALLOC=y`) which automatically allocates from PSRAM
- **Snapshot Support**: Enabled (`CONFIG_LV_USE_SNAPSHOT=y`) for app lifecycle management

These settings are in `sdkconfig.defaults` and will be applied automatically during build.

## Clean Build

If you need to do a complete clean build:
```bash
idf.py fullclean
rm -f sdkconfig sdkconfig.old
idf.py build
```

## Troubleshooting

### Compiler not found
Make sure you've sourced the ESP-IDF export script:
```bash
. $HOME/esp/esp-idf/export.sh
```

### Submodule issues
Update submodules:
```bash
git submodule update --init --recursive
```

### Build errors related to AI framework or speaker
These systems are intentionally disabled. If you see related errors, verify:
```bash
grep "ESP_BROOKESIA_ENABLE_AI_FRAMEWORK\|ESP_BROOKESIA_SYSTEMS_ENABLE_SPEAKER" sdkconfig.defaults
```
Should show both set to `=n`.
