# Icon Source Metadata

## Icon Information
- **Name:** `phone_app_icon`
- **Generated:** 2025-10-13 18:54:46
- **Tool:** generate_icon.py (ESP-Brookesia Icon Generator)

## Source
- **URL:** https://pictogrammers.com/library/mdi/icon/phone/
- **Provider:** Material Design Icons (pictogrammers.com)
- **License:** Apache License 2.0

## Specifications
- **Size:** 64x64 pixels
- **Format:** RGB565 (LVGL9 compatible)
- **Background Color:** #2196F3
- **Corner Radius:** 12px
- **Data Size:** 8192 bytes

## Generated Files
- `phone_app_icon.c` - LVGL9 image data
- `phone_app_icon.h` - Header file
- `phone_app_icon_source.md` - This metadata file

## Usage in Code
```c
#include "phone_app_icon.h"

// Set as image source
lv_image_set_src(img_obj, &phone_app_icon);
```

## Regeneration
To regenerate this icon with the same settings:
```bash
python generate_icon.py https://pictogrammers.com/library/mdi/icon/phone/ \
    --size 64 \
    --bg-color "#2196F3" \\
    --radius 12 \\
    --output phone_app_icon.c \
    --name phone_app_icon
```
