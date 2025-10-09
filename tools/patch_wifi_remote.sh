#!/bin/bash
################################################################################
# ESP WiFi Remote Component Patch Script
# 
# Purpose: Fixes Kconfig slave target selection bug in esp_wifi_remote v1.1.4
#          for ESP-IDF 5.5.1 (and other patch versions)
#
# Issue: https://github.com/espressif/esp-wifi-remote/issues/XXX
#        (Update with actual issue number after submission)
#
# Root Cause: $ESP_IDF_VERSION variable expansion doesn't work in Kconfig orsource
#
# Usage: Run this script after:
#        - idf.py update-dependencies (reinstalls components)
#        - rm -rf managed_components (manual component removal)
#        - git clean -fdx (full clean)
#
# This script will be OBSOLETE once esp_wifi_remote > 1.1.4 is released with fix.
################################################################################

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WIFI_REMOTE_DIR="$PROJECT_ROOT/managed_components/espressif__esp_wifi_remote"
KCONFIG_FILE="$WIFI_REMOTE_DIR/Kconfig"

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}   ESP WiFi Remote Component Patcher${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo

# Check if component directory exists
if [ ! -d "$WIFI_REMOTE_DIR" ]; then
    echo -e "${RED}✗ Error: WiFi Remote component not found at:${NC}"
    echo -e "  $WIFI_REMOTE_DIR"
    echo
    echo -e "${YELLOW}Run 'idf.py update-dependencies' first to install the component.${NC}"
    exit 1
fi

# Check component version from dependencies.lock (more reliable)
LOCK_FILE="$PROJECT_ROOT/dependencies.lock"
if [ -f "$LOCK_FILE" ]; then
    # Parse the version from the esp_wifi_remote section (it's at the end after dependencies)
    COMPONENT_VERSION=$(sed -n '/espressif\/esp_wifi_remote:/,/^  [a-z]/p' "$LOCK_FILE" | grep "^    version:" | awk '{print $2}')
else
    # Fallback: check if idf_v5.5 directory exists (indicates v1.1.4+)
    if [ -d "$WIFI_REMOTE_DIR/idf_v5.5" ]; then
        COMPONENT_VERSION="1.1.4 (or compatible)"
    else
        COMPONENT_VERSION="unknown"
    fi
fi

echo -e "${BLUE}Component version:${NC} $COMPONENT_VERSION"
echo

# Check if patch is actually needed by examining Kconfig
if grep -q 'orsource "./idf_v\$ESP_IDF_VERSION/Kconfig.slave_select.in"' "$KCONFIG_FILE" 2>/dev/null; then
    echo -e "${BLUE}✓ Patch needed:${NC} Kconfig contains variable expansion bug"
elif grep -q 'orsource "./idf_v5.5.1/Kconfig.slave_select.in"' "$KCONFIG_FILE" 2>/dev/null; then
    echo -e "${GREEN}✓ Already patched:${NC} Hardcoded paths detected"
    echo -e "  Symlink and backup will be verified..."
else
    echo -e "${YELLOW}⚠ Warning: Cannot determine if patch is needed${NC}"
    echo -e "  Kconfig format may have changed (upstream fix applied?)"
    read -p "Continue with patching anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${BLUE}Exiting. If upstream fixed the bug, this script is no longer needed.${NC}"
        exit 0
    fi
fi

# Warn about version compatibility (informational only)
if [[ ! "$COMPONENT_VERSION" =~ ^1\.1\. ]] && [ "$COMPONENT_VERSION" != "unknown" ] && [[ ! "$COMPONENT_VERSION" =~ "compatible" ]]; then
    echo -e "${YELLOW}⚠ Note: This patch was designed for v1.1.4, but found $COMPONENT_VERSION${NC}"
    echo -e "  The patch may not be needed if upstream has fixed the bug."
fi
echo

# Check if Kconfig file exists
if [ ! -f "$KCONFIG_FILE" ]; then
    echo -e "${RED}✗ Error: Kconfig file not found at:${NC}"
    echo -e "  $KCONFIG_FILE"
    exit 1
fi

echo

# Step 1: Create symlink for IDF 5.5.1 → 5.5
echo -e "${BLUE}[1/3]${NC} Creating IDF version symlink..."

if [ -L "$WIFI_REMOTE_DIR/idf_v5.5.1" ]; then
    echo -e "${GREEN}  ✓ Symlink already exists${NC}"
elif [ -e "$WIFI_REMOTE_DIR/idf_v5.5.1" ]; then
    echo -e "${RED}  ✗ idf_v5.5.1 exists but is not a symlink${NC}"
    exit 1
else
    cd "$WIFI_REMOTE_DIR"
    ln -sfn idf_v5.5 idf_v5.5.1
    echo -e "${GREEN}  ✓ Created: idf_v5.5.1 → idf_v5.5${NC}"
fi

# Step 2: Backup original Kconfig
echo -e "${BLUE}[2/3]${NC} Backing up original Kconfig..."

if [ ! -f "$KCONFIG_FILE.original" ]; then
    cp "$KCONFIG_FILE" "$KCONFIG_FILE.original"
    echo -e "${GREEN}  ✓ Backup created: Kconfig.original${NC}"
else
    echo -e "${GREEN}  ✓ Backup already exists${NC}"
fi

# Step 3: Apply patch to Kconfig
echo -e "${BLUE}[3/3]${NC} Patching Kconfig file..."

# Check if already patched
if grep -q "# PATCH: \$ESP_IDF_VERSION expansion not working" "$KCONFIG_FILE"; then
    echo -e "${GREEN}  ✓ Kconfig already patched${NC}"
else
    # Apply the patch using sed (portable across macOS and Linux)
    
    # Patch 1: Fix slave selection and soc_wifi_caps
    sed -i.bak '
        /orsource "\.\/idf_v\$ESP_IDF_VERSION\/Kconfig\.slave_select\.in"/c\
        # PATCH: $ESP_IDF_VERSION expansion not working in orsource, hardcode IDF 5.5 paths\
        orsource "./idf_v5.5.1/Kconfig.slave_select.in"\
        orsource "./idf_v5.5/Kconfig.slave_select.in"
    ' "$KCONFIG_FILE"
    
    sed -i.bak '
        /orsource "\.\/idf_v\$ESP_IDF_VERSION\/Kconfig\.soc_wifi_caps\.in"/c\
        # Fallback to 5.5 if 5.5.1 doesn'\''t exist\
        orsource "./idf_v5.5.1/Kconfig.soc_wifi_caps.in"\
        orsource "./idf_v5.5/Kconfig.soc_wifi_caps.in"
    ' "$KCONFIG_FILE"
    
    # Patch 2: Fix WiFi configuration menu
    sed -i.bak '
        /orsource "\.\/idf_v\$ESP_IDF_VERSION\/Kconfig\.wifi\.in"/c\
            # PATCH: $ESP_IDF_VERSION expansion not working in orsource, hardcode IDF 5.5 paths\
            orsource "./idf_v5.5.1/Kconfig.wifi.in"\
            orsource "./idf_v5.5/Kconfig.wifi.in"
    ' "$KCONFIG_FILE"
    
    # Remove sed backup files
    rm -f "$KCONFIG_FILE.bak"
    
    echo -e "${GREEN}  ✓ Kconfig patched successfully${NC}"
fi

echo
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}✓ Patch applied successfully!${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo
echo -e "${YELLOW}Next steps:${NC}"
echo -e "  1. Run: ${BLUE}idf.py reconfigure${NC}"
echo -e "  2. Verify slave target: ${BLUE}grep SLAVE_IDF_TARGET_ESP32C6 sdkconfig${NC}"
echo -e "  3. Build: ${BLUE}idf.py build${NC}"
echo
echo -e "${YELLOW}When to re-run this script:${NC}"
echo -e "  • After ${BLUE}idf.py update-dependencies${NC}"
echo -e "  • After ${BLUE}rm -rf managed_components${NC}"
echo -e "  • After ${BLUE}git clean -fdx${NC}"
echo
echo -e "${YELLOW}This patch can be removed when:${NC}"
echo -e "  • esp_wifi_remote version > 1.1.4 is released with the fix"
echo -e "  • Update main/idf_component.yml to use the new version"
echo
