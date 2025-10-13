#!/usr/bin/env python3
"""
Icon Generator for LVGL9 and ESP-Brookesia

This script downloads Material Design Icons (MDI) from pictogrammers.com
and converts them to LVGL9-compatible C code with ARGB8888 format.
Optimized for ESP-Brookesia phone apps with 112x112 pixel default size.

Default appearance:
- Icon graphics: White (#FFFFFF)
- Background: Grey (#808080)
- Corner radius: 12px (rounded)

Usage:
    Interactive mode (recommended):
        python generate_icon.py --interactive
    
    Command-line mode:
        python generate_icon.py <icon_url> [OPTIONS]

Example:
    python generate_icon.py --interactive
    python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/
    python generate_icon.py https://pictogrammers.com/library/mdi/icon/camera/ --icon-color "#FFFFFF" --bg-color "#2196F3" --radius 12
    python generate_icon.py https://pictogrammers.com/library/mdi/icon/phone/ --bg-color none --radius 0
"""

import argparse
import requests
import re
import sys
import os
from pathlib import Path
from io import BytesIO
from datetime import datetime
from PIL import Image, ImageDraw
import cairosvg

def extract_icon_name(url):
    """Extract icon name from pictogrammers URL"""
    match = re.search(r'/icon/([^/]+)/?$', url)
    if match:
        return match.group(1).replace('-', '_')
    return None

def get_svg_from_mdi(url):
    """Download SVG from Material Design Icons website"""
    # Extract icon name from URL
    icon_name = extract_icon_name(url)
    if not icon_name:
        print(f"Error: Could not extract icon name from URL: {url}")
        return None, None
    
    print(f"Downloading icon: {icon_name}")
    
    # Material Design Icons CDN URL format
    # https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/{icon-name}.svg
    svg_url = f"https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/{icon_name.replace('_', '-')}.svg"
    
    try:
        response = requests.get(svg_url, timeout=10)
        response.raise_for_status()
        print(f"Successfully downloaded SVG from: {svg_url}")
        return response.content, icon_name
    except requests.exceptions.RequestException as e:
        print(f"Error downloading SVG: {e}")
        print(f"Tried URL: {svg_url}")
        return None, None

def hex_to_rgb(hex_color):
    """Convert hex color to RGB tuple"""
    hex_color = hex_color.lstrip('#')
    return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))

def colorize_icon(image, icon_color):
    """
    Colorize the icon graphics while preserving transparency
    
    Args:
        image: PIL Image in RGBA mode
        icon_color: Target color as hex string (e.g., "#FFFFFF" for white)
    
    Returns:
        Colorized image with preserved alpha channel
    """
    # Convert to RGBA if not already
    if image.mode != 'RGBA':
        image = image.convert('RGBA')
    
    # Get the target color
    target_rgb = hex_to_rgb(icon_color)
    
    # Split into RGB and Alpha channels
    r, g, b, a = image.split()
    
    # Create new colored layers based on alpha intensity
    # The icon is typically white/light in SVG, we'll use the alpha as intensity
    r_new = Image.new('L', image.size)
    g_new = Image.new('L', image.size)
    b_new = Image.new('L', image.size)
    
    # For each pixel, set color based on alpha (icon visibility)
    pixels_a = a.load()
    pixels_r = r_new.load()
    pixels_g = g_new.load()
    pixels_b = b_new.load()
    
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            alpha_val = pixels_a[x, y]
            # Apply target color with alpha intensity
            pixels_r[x, y] = target_rgb[0]
            pixels_g[x, y] = target_rgb[1]
            pixels_b[x, y] = target_rgb[2]
    
    # Merge back with original alpha
    return Image.merge('RGBA', (r_new, g_new, b_new, a))

def svg_to_png(svg_data, size=112, icon_color="#FFFFFF", bg_color="#808080", radius=12):
    """
    Convert SVG to PNG with icon colorization, background, and rounded corners
    
    Args:
        svg_data: SVG content as bytes
        size: Output size in pixels (default 112 to match Brookesia standard)
        icon_color: Icon graphics color as hex string (default: "#FFFFFF" white)
        bg_color: Background color as hex string (default: "#808080" grey) or None for transparent
        radius: Corner radius in pixels (default: 12 for rounded corners)
    """
    try:
        # First convert SVG to PNG with transparent background
        png_data = cairosvg.svg2png(
            bytestring=svg_data,
            output_width=size,
            output_height=size,
            background_color='transparent'
        )
        image = Image.open(BytesIO(png_data))
        
        # Ensure RGBA mode for alpha channel support
        if image.mode != 'RGBA':
            image = image.convert('RGBA')
        
        # Colorize the icon if color is specified
        if icon_color:
            image = colorize_icon(image, icon_color)
        
        # If no background color and no radius, return colorized icon
        if not bg_color and radius == 0:
            return image
        
        # Create new image with background
        if bg_color:
            bg_rgb = hex_to_rgb(bg_color)
        else:
            bg_rgb = (255, 255, 255)
        
        # Create base image with background color and transparency
        final_image = Image.new('RGBA', (size, size), bg_rgb + (255,))
        
        # Apply rounded corners if requested
        if radius > 0:
            # Create rounded rectangle mask
            mask = Image.new('L', (size, size), 0)
            mask_draw = ImageDraw.Draw(mask)
            mask_draw.rounded_rectangle(
                [(0, 0), (size, size)],
                radius=radius,
                fill=255
            )
            
            # Apply mask to alpha channel
            r, g, b, a = final_image.split()
            a = Image.composite(a, Image.new('L', (size, size), 0), mask)
            final_image = Image.merge('RGBA', (r, g, b, a))
        
        # Composite the icon on top (center it)
        # Calculate padding to center icon (leave 10% margin)
        margin = int(size * 0.1)
        icon_size = size - (margin * 2)
        
        # Resize icon to fit with margin
        icon_resized = image.resize((icon_size, icon_size), Image.Resampling.LANCZOS)
        
        # Paste icon in center
        final_image.paste(icon_resized, (margin, margin), icon_resized.split()[3] if icon_resized.mode == 'RGBA' else None)
        
        return final_image
        
    except Exception as e:
        print(f"Error converting SVG to PNG: {e}")
        return None

def save_source_metadata(output_file, icon_name, url, size, icon_color, bg_color, radius):
    """Save metadata about icon source and generation parameters"""
    metadata_file = output_file.replace('.c', '_source.md')
    
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    metadata_content = f"""# Icon Source Metadata

## Icon Information
- **Name:** `{icon_name}`
- **Generated:** {timestamp}
- **Tool:** generate_icon.py (ESP-Brookesia Icon Generator)

## Source
- **URL:** {url}
- **Provider:** Material Design Icons (pictogrammers.com)
- **License:** Apache License 2.0

## Specifications
- **Size:** {size}x{size} pixels
- **Format:** ARGB8888 (LVGL9 compatible with alpha channel)
- **Icon Color:** {icon_color if icon_color else 'Original'}
- **Background Color:** {bg_color if bg_color else 'Transparent'}
- **Corner Radius:** {radius}px
- **Data Size:** {size * size * 4} bytes

## Generated Files
- `{Path(output_file).name}` - LVGL9 image data
- `{Path(output_file).stem}.h` - Header file
- `{Path(metadata_file).name}` - This metadata file

## Usage in Code
```c
#include "{Path(output_file).stem}.h"

// Set as image source
lv_image_set_src(img_obj, &{icon_name});
```

## Regeneration
To regenerate this icon with the same settings:
```bash
python generate_icon.py {url} \\
    --size {size} \\
    {f'--icon-color "{icon_color}" \\' if icon_color else ''}\\
    {f'--bg-color "{bg_color}" \\' if bg_color else ''}\\
    {f'--radius {radius} \\' if radius > 0 else ''}\\
    --output {output_file} \\
    --name {icon_name}
```
"""
    
    with open(metadata_file, 'w') as f:
        f.write(metadata_content)
    
    print(f"Generated metadata file: {metadata_file}")

def image_to_lvgl_c(image, icon_name, output_file):
    """Convert PIL Image to LVGL9 C code format with ARGB8888"""
    
    # Ensure image is RGBA for alpha channel support
    if image.mode != 'RGBA':
        image = image.convert('RGBA')
    
    width, height = image.size
    
    # Convert to ARGB8888 format (4 bytes per pixel: R, G, B, A)
    pixels = []
    for y in range(height):
        for x in range(width):
            r, g, b, a = image.getpixel((x, y))
            pixels.append((r, g, b, a))
    
    # Generate C code
    c_code = f"""/*
 * LVGL9 Icon: {icon_name}
 * Generated by generate_icon.py
 * Size: {width}x{height} pixels
 * Format: ARGB8888
 * Source: Material Design Icons (https://pictogrammers.com)
 */

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_{icon_name.upper()}
#define LV_ATTRIBUTE_IMAGE_{icon_name.upper()}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_{icon_name.upper()} uint8_t {icon_name}_map[] = {{
"""
    
    # Write pixel data as 4 bytes per pixel (R, G, B, A)
    bytes_per_line = 28  # 7 pixels per line (7 * 4 = 28 bytes)
    for i, (r, g, b, a) in enumerate(pixels):
        if i % 7 == 0:
            c_code += "  "
        
        # Write as four bytes: Red, Green, Blue, Alpha
        c_code += f"0x{r:02x}, 0x{g:02x}, 0x{b:02x}, 0x{a:02x}, "
        
        if (i + 1) % 7 == 0:
            c_code += "\n"
    
    c_code += """
};

const lv_image_dsc_t """ + icon_name + """ = {
  .header.cf = LV_COLOR_FORMAT_ARGB8888,
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.w = """ + str(width) + """,
  .header.h = """ + str(height) + """,
  .data_size = """ + str(width * height * 4) + """,
  .data = """ + icon_name + """_map,
};
"""
    
    # Write to file
    with open(output_file, 'w') as f:
        f.write(c_code)
    
    print(f"Generated LVGL9 C code: {output_file}")
    print(f"Icon variable name: {icon_name}")
    print(f"Image dimensions: {width}x{height}")
    print(f"Format: ARGB8888 (with alpha channel)")
    print(f"Data size: {len(pixels) * 4} bytes")
    
    # Generate header file
    header_file = output_file.replace('.c', '.h')
    header_guard = f"_{icon_name.upper()}_H_"
    
    header_code = f"""/*
 * LVGL9 Icon Header: {icon_name}
 * Generated by generate_icon.py
 */

#ifndef {header_guard}
#define {header_guard}

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {{
#endif

extern const lv_image_dsc_t {icon_name};

#ifdef __cplusplus
}}
#endif

#endif /* {header_guard} */
"""
    
    with open(header_file, 'w') as f:
        f.write(header_code)
    
    print(f"Generated header file: {header_file}")

def scan_apps_directory():
    """Scan for available app directories in components/apps/"""
    script_dir = Path(__file__).parent
    apps_dir = script_dir.parent.parent / 'components' / 'apps'
    
    if not apps_dir.exists():
        return []
    
    apps = []
    for app_dir in apps_dir.iterdir():
        if app_dir.is_dir() and not app_dir.name.startswith('.'):
            resources_dir = app_dir / 'resources'
            apps.append({
                'name': app_dir.name,
                'path': app_dir,
                'resources': resources_dir,
                'has_resources': resources_dir.exists()
            })
    
    return sorted(apps, key=lambda x: x['name'])

def interactive_mode():
    """Interactive mode for generating icons"""
    print("\n" + "="*60)
    print("  LVGL9 Icon Generator - Interactive Mode")
    print("  ESP-Brookesia Phone App Icon Generator")
    print("="*60 + "\n")
    
    # Step 1: Get icon URL
    print("Step 1: Icon Source")
    print("-" * 60)
    print("Enter the Material Design Icons URL")
    print("Example: https://pictogrammers.com/library/mdi/icon/camera/\n")
    
    url = input("Icon URL: ").strip()
    if not url:
        print("Error: URL is required")
        return 1
    
    # Step 2: Get icon name
    print("\n" + "="*60)
    print("Step 2: Icon Configuration")
    print("-" * 60)
    
    suggested_name = extract_icon_name(url)
    if suggested_name:
        name_input = input(f"Icon variable name [{suggested_name}_icon]: ").strip()
        icon_name = name_input if name_input else f"{suggested_name}_icon"
    else:
        icon_name = input("Icon variable name: ").strip()
        if not icon_name:
            print("Error: Icon name is required")
            return 1
    
    # Step 3: Icon size
    size_input = input("Icon size in pixels [112]: ").strip()
    size = int(size_input) if size_input else 112
    
    # Step 4: Icon graphics color
    print("\nIcon graphics color (leave empty for white):")
    print("  Common colors: #FFFFFF (white), #000000 (black), #2196F3 (blue)")
    print("  #4CAF50 (green), #F44336 (red), #FF9800 (orange)")
    icon_color_input = input("Icon color (hex) [#FFFFFF]: ").strip()
    icon_color = icon_color_input if icon_color_input else "#FFFFFF"
    
    # Step 5: Background color
    print("\nBackground color (leave empty for grey):")
    print("  Common colors: #808080 (grey), #2196F3 (blue), #4CAF50 (green)")
    print("  #F44336 (red), #FF9800 (orange), #9C27B0 (purple), #607D8B (blue-grey)")
    print("  Or enter 'none' for transparent background")
    bg_color_input = input("Background color (hex) [#808080]: ").strip()
    if bg_color_input.lower() == 'none':
        bg_color = None
    else:
        bg_color = bg_color_input if bg_color_input else "#808080"
    
    # Step 6: Corner radius
    radius_input = input("Corner radius in pixels [12]: ").strip()
    radius = int(radius_input) if radius_input else 12
    
    # Step 7: Output location
    print("\n" + "="*60)
    print("Step 3: Output Location")
    print("-" * 60)
    print("Choose output location:")
    print("  1. Generated icons directory (./generated_icons/<icon_name>/)")
    print("  2. Specific app's resources folder")
    print("  3. Custom path")
    
    location_choice = input("\nChoice [1]: ").strip() or "1"
    
    if location_choice == "1":
        # Create generated_icons/<icon_name>/ directory
        output_dir = Path("generated_icons") / icon_name
        output_dir.mkdir(parents=True, exist_ok=True)
        output_file = str(output_dir / f"{icon_name}.c")
    elif location_choice == "2":
        # Scan for available apps
        apps = scan_apps_directory()
        if not apps:
            print("No apps found in components/apps/")
            output_file = f"{icon_name}.c"
        else:
            print("\nAvailable apps:")
            for i, app in enumerate(apps, 1):
                status = "✓" if app['has_resources'] else "✗"
                print(f"  {i}. {app['name']} {status}")
            
            app_choice = input(f"\nSelect app [1]: ").strip() or "1"
            try:
                app_idx = int(app_choice) - 1
                if 0 <= app_idx < len(apps):
                    selected_app = apps[app_idx]
                    
                    # Create resources directory if it doesn't exist
                    if not selected_app['has_resources']:
                        selected_app['resources'].mkdir(parents=True, exist_ok=True)
                        print(f"Created: {selected_app['resources']}")
                    
                    output_file = str(selected_app['resources'] / f"{icon_name}.c")
                else:
                    print("Invalid choice, using current directory")
                    output_file = f"{icon_name}.c"
            except ValueError:
                print("Invalid choice, using current directory")
                output_file = f"{icon_name}.c"
    else:
        custom_path = input("Enter custom output path: ").strip()
        output_file = custom_path if custom_path else f"{icon_name}.c"
    
    # Step 8: Generate metadata file
    print("\n" + "="*60)
    print("Step 4: Metadata")
    print("-" * 60)
    save_metadata = input("Save source metadata file? [Y/n]: ").strip().lower() or 'y'
    
    # Summary
    print("\n" + "="*60)
    print("Summary")
    print("-" * 60)
    print(f"  Icon URL:        {url}")
    print(f"  Icon Name:       {icon_name}")
    print(f"  Size:            {size}x{size} px")
    print(f"  Icon Color:      {icon_color}")
    print(f"  Background:      {bg_color if bg_color else 'Transparent'}")
    print(f"  Corner Radius:   {radius}px")
    print(f"  Output:          {output_file}")
    print(f"  Save Metadata:   {'Yes' if save_metadata == 'y' else 'No'}")
    print("="*60 + "\n")
    
    confirm = input("Generate icon? [Y/n]: ").strip().lower() or 'y'
    if confirm != 'y':
        print("Cancelled.")
        return 0
    
    # Generate icon
    print("\nGenerating icon...")
    return generate_icon(url, icon_name, size, icon_color, bg_color, radius, output_file, save_metadata == 'y')

def generate_icon(url, icon_name, size, icon_color, bg_color, radius, output_file, save_metadata):
    """Generate icon with given parameters"""
    
    # Download SVG
    svg_data, downloaded_name = get_svg_from_mdi(url)
    if not svg_data or not downloaded_name:
        print("Failed to download icon")
        return 1
    
    # Use provided name or fallback to downloaded name
    final_icon_name = icon_name if icon_name else downloaded_name
    
    # Set output path
    output_path = Path(output_file)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    # Convert SVG to PNG with icon color, background and radius
    print(f"Converting to {size}x{size} PNG...")
    print(f"  Icon color: {icon_color}")
    print(f"  Background: {bg_color if bg_color else 'transparent'}")
    print(f"  Radius: {radius}px")
    image = svg_to_png(svg_data, size, icon_color, bg_color, radius)
    if not image:
        print("Failed to convert SVG to PNG")
        return 1
    
    # Generate LVGL C code
    print("Generating LVGL9 C code...")
    image_to_lvgl_c(image, final_icon_name, output_file)
    
    # Save metadata if requested
    if save_metadata:
        save_source_metadata(output_file, final_icon_name, url, size, icon_color, bg_color, radius)
    
    print("\n✅ Icon generation complete!")
    print(f"\nGenerated files:")
    print(f"  • {output_file}")
    print(f"  • {output_file.replace('.c', '.h')}")
    if save_metadata:
        print(f"  • {output_file.replace('.c', '_source.md')}")
    print(f"\nTo use in your Brookesia app:")
    print(f"1. Add to CMakeLists.txt SRCS: \"{Path(output_file).name}\"")
    print(f"2. Include the header: #include \"{Path(output_file).stem}.h\"")
    print(f"3. Use in LVGL: lv_image_set_src(img, &{final_icon_name});")
    
    return 0

def main():
    parser = argparse.ArgumentParser(
        description='Generate LVGL9 icon from Material Design Icons URL',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Interactive mode (recommended):
    %(prog)s --interactive
  
  Command-line mode with defaults (white icon, grey background, 12px radius):
    %(prog)s https://pictogrammers.com/library/mdi/icon/camera/
  
  Custom colors and size:
    %(prog)s https://pictogrammers.com/library/mdi/icon/phone/ --icon-color "#000000" --bg-color "#2196F3" --size 128
  
  Transparent background, no radius:
    %(prog)s https://pictogrammers.com/library/mdi/icon/settings/ --bg-color none --radius 0
  
  Full customization:
    %(prog)s https://pictogrammers.com/library/mdi/icon/home/ --icon-color "#FFFFFF" --bg-color "#4CAF50" --radius 16 --save-metadata
        """
    )
    
    parser.add_argument('url', nargs='?', help='Material Design Icons URL (e.g., https://pictogrammers.com/library/mdi/icon/camera/)')
    parser.add_argument('-i', '--interactive', action='store_true', help='Run in interactive mode')
    parser.add_argument('--size', type=int, default=112, help='Icon size in pixels (default: 112 matching Brookesia standard)')
    parser.add_argument('--icon-color', type=str, default='#FFFFFF', help='Icon graphics color in hex format (default: "#FFFFFF" white)')
    parser.add_argument('--bg-color', type=str, default='#808080', help='Background color in hex format (default: "#808080" grey, use "none" for transparent)')
    parser.add_argument('--radius', type=int, default=12, help='Corner radius in pixels (default: 12 for rounded corners)')
    parser.add_argument('--output', type=str, help='Output C file path (default: generated_icons/<icon_name>/<icon_name>.c)')
    parser.add_argument('--name', type=str, help='Override icon variable name (default: extracted from URL)')
    parser.add_argument('--save-metadata', action='store_true', help='Save source metadata file')
    
    args = parser.parse_args()
    
    # Run interactive mode if requested or if no URL provided
    if args.interactive or not args.url:
        return interactive_mode()
    
    # Command-line mode
    icon_name = args.name
    output_file = args.output if args.output else None
    
    # Extract icon name from URL
    extracted_name = extract_icon_name(args.url)
    if not extracted_name:
        print("Error: Could not extract icon name from URL. Please specify --output or --name")
        return 1
    
    # Set icon name
    if not icon_name:
        icon_name = f"{extracted_name}_icon"
    
    # If no output file specified, use generated_icons/<icon_name>/<icon_name>.c
    if not output_file:
        output_dir = Path("generated_icons") / icon_name
        output_dir.mkdir(parents=True, exist_ok=True)
        output_file = str(output_dir / f"{icon_name}.c")
    
    # Handle "none" for transparent background
    bg_color = None if args.bg_color.lower() == 'none' else args.bg_color
    
    return generate_icon(
        args.url,
        icon_name,
        args.size,
        args.icon_color,
        bg_color,
        args.radius,
        output_file,
        args.save_metadata
    )

if __name__ == '__main__':
    sys.exit(main())
