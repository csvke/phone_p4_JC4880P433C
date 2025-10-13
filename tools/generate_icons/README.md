# Icon Generator for LVGL9 and ESP-Brookesia

This tool generates LVGL9-compatible icon files from Material Design Icons for use in ESP-Brookesia phone applications.

## Features

- 📥 Downloads icons directly from Material Design Icons (pictogrammers.com)
- 🎨 Converts SVG to ARGB8888 format with alpha channel support
- 🌟 Transparent backgrounds matching official Brookesia style
- 📝 Generates both `.c` and `.h` files ready to use
- ⚡ Optimized for ESP32-P4 and Brookesia framework (112x112px default)
- 🔧 Customizable icon size, background color, and rounded corners

## Installation

Install required Python packages:

```bash
pip install pillow cairosvg requests
```

## Usage

### Interactive Mode (Recommended)

Run the tool in interactive mode for a guided experience:

```bash
python generate_icon.py --interactive
```

or simply:

```bash
python generate_icon.py
```

The interactive mode will guide you through:
1. **Icon Selection**: Enter Material Design Icons URL
2. **Configuration**: Set icon name, size, background color, corner radius
3. **Output Location**: Choose between current directory, app's resources folder, or custom path
4. **Metadata**: Option to save source information for future reference

### Basic Command-Line Usage

Generate an icon from a Material Design Icons URL:

```bash
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/
```

This will create:
- `camera_icon.c` - Icon data in ARGB8888 format (transparent background, 112x112px)
- `camera_icon.h` - Header file with declarations

### Advanced Command-Line Usage

Specify custom styling and output options:

```bash
# Standard transparent icon (Brookesia style)
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ \
    --size 112 \
    --save-metadata

# Icon with blue background and rounded corners
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ \
    --size 112 \
    --bg-color "#2196F3" \
    --radius 12 \
    --save-metadata

# Custom output path for specific app
python generate_icon.py https://pictogrammers.com/library/mdi/icon/phone/ \
    --size 112 \
    --bg-color "#4CAF50" \
    --radius 16 \
    --output ../../components/apps/phone/resources/phone_icon.c \
    --name phone_app_icon \
    --save-metadata

# Smaller icon for UI elements
python generate_icon.py https://pictogrammers.com/library/mdi/icon/message/ \
    --size 64 \
    --name messaging_icon
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `url` | Material Design Icons URL | Required (optional in interactive mode) |
| `-i, --interactive` | Run in interactive mode | False |
| `--size` | Icon size in pixels (WxH) | 112 (Brookesia standard) |
| `--bg-color` | Background color in hex format | None (transparent) |
| `--radius` | Corner radius in pixels | 0 (square) |
| `--output` | Output C file path | `<icon_name>.c` |
| `--name` | Override icon variable name | Extracted from URL |
| `--save-metadata` | Save source metadata file | False |

**Note:** Default icons have transparent backgrounds matching official Brookesia apps. Use `--bg-color` only if you need a colored background.

### Background Color Examples

| Color | Hex Code | Use Case |
|-------|----------|----------|
| 🔵 Blue | `#2196F3` | Primary actions, phone dialer |
| 🟢 Green | `#4CAF50` | Success, contacts, messaging |
| 🔴 Red | `#F44336` | Alerts, delete actions |
| 🟠 Orange | `#FF9800` | Warnings, media |
| 🟣 Purple | `#9C27B0` | Creative apps, gallery |
| ⚫ Grey | `#607D8B` | Settings, utilities |

## Example: Camera App Icon with Styled Background

### Option 1: Interactive Mode (Easiest)

```bash
cd /path/to/phone_p4_JC4880P433C/tools/generate_icons
python generate_icon.py --interactive
```

Then follow the prompts:
- URL: `https://pictogrammers.com/library/mdi/icon/camera/`
- Name: `camera_app_icon`
- Size: `112` (press Enter for default)
- Background: Leave empty for transparent (or enter `#FF5722` for orange)
- Radius: `0` (or `16` for rounded)
- Location: Choose "2" for app's resources folder, then select "camera"
- Metadata: `Y`

### Option 2: Command-Line (Transparent Background)

```bash
cd /path/to/phone_p4_JC4880P433C/tools/generate_icons
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ \
    --size 112 \
    --output ../../components/apps/camera/resources/camera_icon.c \
    --name camera_app_icon \
    --save-metadata
```

### Option 3: Command-Line (Colored Background)

```bash
cd /path/to/phone_p4_JC4880P433C/tools/generate_icons
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ \
    --size 112 \
    --bg-color "#FF5722" \
    --radius 16 \
    --output ../../components/apps/camera/resources/camera_icon.c \
    --name camera_app_icon \
    --save-metadata
```

This creates:
- `camera_icon.c` - Icon data in ARGB8888 format with transparent or colored background
- `camera_icon.h` - Header file
- `camera_icon_source.md` - Metadata with source URL and regeneration command

2. **Add to your app's CMakeLists.txt:**

```cmake
idf_component_register(
    SRCS "src/CameraCsi.cpp"
         "src/camera_preview.c"
         "resources/camera_icon.c"  # Add this line
    INCLUDE_DIRS "include" "resources"
    ...
)
```

3. **Use in your Brookesia app:**

```cpp
#include "camera_icon.h"

// In your app class constructor or init:
lv_obj_t *img = lv_image_create(parent);
lv_image_set_src(img, &camera_app_icon);
```

## Popular Icons for Phone Apps

Here are some common Material Design Icons useful for phone apps:

| Icon | URL | Use Case |
|------|-----|----------|
| 📷 Camera | https://pictogrammers.com/library/mdi/icon/camera/ | Camera app |
| 📞 Phone | https://pictogrammers.com/library/mdi/icon/phone/ | Dialer app |
| 💬 Message | https://pictogrammers.com/library/mdi/icon/message/ | SMS/Messaging |
| 👤 Account | https://pictogrammers.com/library/mdi/icon/account/ | Contacts/Profile |
| ⚙️ Settings | https://pictogrammers.com/library/mdi/icon/cog/ | Settings app |
| 📁 Folder | https://pictogrammers.com/library/mdi/icon/folder/ | File manager |
| 🔍 Search | https://pictogrammers.com/library/mdi/icon/magnify/ | Search function |
| 🏠 Home | https://pictogrammers.com/library/mdi/icon/home/ | Home screen |
| 🎵 Music | https://pictogrammers.com/library/mdi/icon/music/ | Music player |
| 🌐 Web | https://pictogrammers.com/library/mdi/icon/web/ | Browser |

## Technical Details

### Output Format

- **Color Format:** ARGB8888 (32-bit per pixel with alpha channel)
- **Default Size:** 112x112 pixels (matching Brookesia standard)
- **Transparency:** Full alpha channel support for transparent backgrounds
- **LVGL Version:** 9.x compatible
- **Memory Alignment:** Aligned for optimal performance

### Generated Files

**C File Structure:**
```c
const uint8_t icon_name_map[] = { /* pixel data: R, G, B, A */ };

const lv_image_dsc_t icon_name = {
    .header.cf = LV_COLOR_FORMAT_ARGB8888,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 112,
    .header.h = 112,
    .data_size = 50176,  /* 112 * 112 * 4 bytes */
        .stride = 128,  // width * 2 bytes
    },
    .data = icon_name_map,
};
```

**Header File:**
```c
extern const lv_image_dsc_t icon_name;
```

## Troubleshooting

### Icon appears distorted
- Ensure the icon size matches your app's requirements
- Verify CMakeLists.txt includes the `.c` file in SRCS
- Check that `lv_image_set_src()` uses the correct variable name

### Import error: No module named 'cairosvg'
```bash
pip install cairosvg pillow requests
```

### macOS: cairo library not found
```bash
brew install cairo
```

### Linux: cairo library not found
```bash
sudo apt-get install libcairo2-dev
```

## License

This tool is part of the ESP32-P4 JC4880P433C phone project.
Material Design Icons are licensed under Apache License 2.0.

## References

- [Material Design Icons](https://pictogrammers.com/library/mdi/)
- [LVGL Documentation](https://docs.lvgl.io/master/overview/image.html)
- [ESP-Brookesia Framework](https://github.com/espressif/esp-brookesia)
