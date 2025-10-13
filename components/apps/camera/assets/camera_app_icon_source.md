# Icon Source Metadata

## Icon Information
- **Name:** `camera_app_icon`
- **Generated:** 2025-10-13 20:20:23
- **Tool:** generate_icon.py (ESP-Brookesia Icon Generator)

## Source
- **URL:** https://pictogrammers.com/library/mdi/icon/camera/
- **Provider:** Material Design Icons (pictogrammers.com)
- **License:** Apache License 2.0

## Specifications
- **Size:** 112x112 pixels
- **Format:** ARGB8888 (LVGL9 compatible with alpha channel)
- **Icon Color:** #FFFFFF
- **Background Color:** #808080
- **Corner Radius:** 12px
- **Data Size:** 50176 bytes

## Generated Files
- `camera_app_icon.c` - LVGL9 image data
- `camera_app_icon.h` - Header file
- `camera_app_icon_source.md` - This metadata file

## Usage in Code
```c
#include "camera_app_icon.h"

// Set as image source
lv_image_set_src(img_obj, &camera_app_icon);
```

## Regeneration
To regenerate this icon with the same settings:
```bash
python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ \
    --size 112 \
    --icon-color "#FFFFFF" \\
    --bg-color "#808080" \\
    --radius 12 \\
    --output generated_icons/camera/camera_app_icon.c \
    --name camera_app_icon
```
