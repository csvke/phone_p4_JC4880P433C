# WiFi Component Patch Documentation

## Overview

This project requires a **temporary patch** to the `esp_wifi_remote` component (v1.1.4) to fix a Kconfig bug that prevents WiFi configuration on ESP-IDF 5.5.1.

**Status:** ⏰ **Temporary workaround** - Will be removed when upstream fix is available

**Upstream Issue:** https://github.com/espressif/esp-wifi-remote/issues/XXX  
*(Update with actual issue number)*

---

## The Problem

### Root Cause
The `esp_wifi_remote` component's Kconfig file uses `$ESP_IDF_VERSION` variable expansion in `orsource` directives, but Kconfig doesn't support this syntax. This causes the slave target selection menu to fail to load.

### Impact
- ❌ Cannot select ESP32-C6 as WiFi slave chip
- ❌ SDIO transport option not available
- ❌ Build fails with "Unknown Slave Target" errors
- ❌ `CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="invalid"`

### Affected Versions
- **Works:** ESP-IDF 5.5.0 (and other .0 releases)
- **Broken:** ESP-IDF 5.5.1, 5.5.2, and all patch versions

---

## The Solution

We've created an automated patch script that:
1. Creates a symlink: `idf_v5.5.1` → `idf_v5.5`
2. Patches the Kconfig to use hardcoded paths instead of variable expansion
3. Backs up the original Kconfig for safety

### Patch Script Location
```
tools/patch_wifi_remote.sh
```

---

## When to Run the Patch

Run the patch script **after any operation that reinstalls/cleans components**:

### ✅ **Always Run After:**

1. **Installing dependencies** (first time setup):
   ```bash
   idf.py update-dependencies
   ./tools/patch_wifi_remote.sh  # ← Run this!
   ```

2. **Updating dependencies** (component version changes):
   ```bash
   idf.py update-dependencies
   ./tools/patch_wifi_remote.sh  # ← Run this!
   ```

3. **Manual component cleanup**:
   ```bash
   rm -rf managed_components
   idf.py update-dependencies
   ./tools/patch_wifi_remote.sh  # ← Run this!
   ```

4. **Full project clean** (git clean):
   ```bash
   git clean -fdx
   idf.py update-dependencies
   ./tools/patch_wifi_remote.sh  # ← Run this!
   ```

### ❌ **NOT Needed After:**

- `idf.py build` (normal builds don't touch components)
- `idf.py fullclean` (only cleans build directory)
- `idf.py clean` (only cleans build directory)
- `idf.py menuconfig` (doesn't reinstall components)
- Git commits/checkouts (components are in .gitignore)

---

## Usage

### Running the Patch Script

```bash
# From project root
./tools/patch_wifi_remote.sh
```

**Output:**
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   ESP WiFi Remote Component Patcher
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Component version: 1.1.4

[1/3] Creating IDF version symlink...
  ✓ Created: idf_v5.5.1 → idf_v5.5
[2/3] Backing up original Kconfig...
  ✓ Backup created: Kconfig.original
[3/3] Patching Kconfig file...
  ✓ Kconfig patched successfully

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
✓ Patch applied successfully!
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### Verifying the Patch

After patching, reconfigure and check:

```bash
# Reconfigure to load patched Kconfig
idf.py reconfigure

# Verify slave target is set correctly
grep SLAVE_IDF_TARGET_ESP32C6 sdkconfig
# Should output: CONFIG_SLAVE_IDF_TARGET_ESP32C6=y

# Verify ESP-Hosted recognizes the slave
grep ESP_HOSTED_IDF_SLAVE_TARGET sdkconfig
# Should output: CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c6"

# Verify SDIO transport is available
grep ESP_HOSTED_SDIO_HOST_INTERFACE sdkconfig
# Should output: CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
```

✅ **All checks pass?** → Patch is working!

❌ **Still seeing "invalid"?** → Re-run the patch script or check troubleshooting section below.

---

## Troubleshooting

### Issue: "WiFi Remote component not found"

**Cause:** Component not installed yet.

**Fix:**
```bash
idf.py update-dependencies
./tools/patch_wifi_remote.sh
```

### Issue: "Kconfig file not found"

**Cause:** Corrupted component installation.

**Fix:**
```bash
rm -rf managed_components/espressif__esp_wifi_remote
idf.py update-dependencies
./tools/patch_wifi_remote.sh
```

### Issue: Still shows "invalid" after patching

**Symptoms:**
```bash
$ grep ESP_HOSTED_IDF_SLAVE_TARGET sdkconfig
CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="invalid"
```

**Debug Steps:**

1. **Check if symlink exists:**
   ```bash
   ls -la managed_components/espressif__esp_wifi_remote/idf_v5.5*
   # Should show: idf_v5.5.1 -> idf_v5.5
   ```

2. **Check if Kconfig was patched:**
   ```bash
   grep "PATCH:" managed_components/espressif__esp_wifi_remote/Kconfig
   # Should show comment lines about the patch
   ```

3. **Check for duplicate orsource entries:**
   ```bash
   grep "orsource.*Kconfig.slave_select" managed_components/espressif__esp_wifi_remote/Kconfig
   # Should show TWO lines (idf_v5.5.1 and idf_v5.5)
   ```

4. **Force clean reconfigure:**
   ```bash
   rm sdkconfig
   idf.py reconfigure
   ```

### Issue: Patch script fails with "version mismatch"

**Cause:** Component version changed (updated upstream).

**Action:**
- If version > 1.1.4: Check if upstream bug is fixed
- Update `main/idf_component.yml` to pin to 1.1.4 temporarily:
  ```yaml
  espressif/esp_wifi_remote: "==1.1.4"  # Pin to 1.1.4 until patch needed
  ```

---

## Removing the Patch (Future)

When Espressif releases `esp_wifi_remote` version **> 1.1.4** with the fix:

### Step 1: Update Component Version

Edit `main/idf_component.yml`:
```yaml
# OLD (with patch needed):
espressif/esp_wifi_remote: "~1.1.4"

# NEW (patch no longer needed):
espressif/esp_wifi_remote: "~1.2.0"  # Or whatever version has the fix
```

### Step 2: Clean and Reinstall

```bash
# Remove old patched component
rm -rf managed_components/espressif__esp_wifi_remote

# Install new version
idf.py update-dependencies

# Verify it works WITHOUT the patch
idf.py reconfigure
grep SLAVE_IDF_TARGET_ESP32C6 sdkconfig
# Should output: CONFIG_SLAVE_IDF_TARGET_ESP32C6=y
```

### Step 3: Remove Patch Files

```bash
# Remove patch script (no longer needed)
rm tools/patch_wifi_remote.sh

# Remove this documentation
rm docs/WIFI_PATCH.md

# Update CHANGELOG.md to note patch removal
```

---

## Technical Details

### What the Patch Does

**Original Kconfig (broken):**
```kconfig
orsource "./idf_v$ESP_IDF_VERSION/Kconfig.slave_select.in"
```

**Patched Kconfig (working):**
```kconfig
# PATCH: $ESP_IDF_VERSION expansion not working in orsource
orsource "./idf_v5.5.1/Kconfig.slave_select.in"
orsource "./idf_v5.5/Kconfig.slave_select.in"
```

### Why It Works

- Kconfig's `orsource` tries each path in order
- First tries `idf_v5.5.1/` (symlink → `idf_v5.5/`)
- If that fails, tries `idf_v5.5/` (actual directory)
- At least one succeeds, loading the slave target choice menu

### Files Modified by Patch

1. **Symlink created:**
   ```
   managed_components/espressif__esp_wifi_remote/idf_v5.5.1 -> idf_v5.5
   ```

2. **Kconfig backup created:**
   ```
   managed_components/espressif__esp_wifi_remote/Kconfig.original
   ```

3. **Kconfig modified:**
   ```
   managed_components/espressif__esp_wifi_remote/Kconfig
   ```

---

## Team Onboarding

### For New Developers

When setting up the project for the first time:

```bash
# 1. Clone repository
git clone <repo-url>
cd phone_p4_JC4880P433C

# 2. Install ESP-IDF 5.5.1
# (Follow ESP-IDF installation guide)

# 3. Install project dependencies
idf.py update-dependencies

# 4. Apply WiFi patch (IMPORTANT!)
./tools/patch_wifi_remote.sh

# 5. Build project
idf.py build
```

**Important:** Read this document first to understand why the patch is needed!

---

## FAQ

**Q: Why not fix this in `components/` instead of `managed_components/`?**  
A: We want to use the official component from the registry and apply minimal patches. Once upstream is fixed, we can drop the patch entirely without code changes.

**Q: Will this patch interfere with upstream updates?**  
A: Yes, if you run `idf.py update-dependencies`, the patch will be overwritten. Just re-run `./tools/patch_wifi_remote.sh` afterward.

**Q: Can I skip the patch and configure manually?**  
A: No, manual `sdkconfig` edits don't work because Kconfig doesn't define the symbols. The Kconfig files must be loaded via `orsource`.

**Q: Is this safe for production?**  
A: Yes, the patch only fixes Kconfig loading. The actual component code is unmodified. However, you should migrate to the official fixed version once available.

**Q: What if I forget to run the patch?**  
A: Your build will fail with "Unknown Slave Target" errors. Just run the patch script and rebuild.

---

## Monitoring for Upstream Fix

**Subscribe to the GitHub issue:**  
https://github.com/espressif/esp-wifi-remote/issues/XXX

**Check component updates:**
```bash
# List available versions
idf.py component-manager find espressif/esp_wifi_remote

# Check current version
grep "version:" managed_components/espressif__esp_wifi_remote/idf_component.yml
```

When version > 1.1.4 is released, test if the patch is still needed before removing it.

---

## Change Log

| Date       | Action                           | Notes                                    |
|------------|----------------------------------|------------------------------------------|
| 2025-10-08 | Initial patch created            | Bug reported to Espressif (issue #XXX)   |
| TBD        | Upstream fix released            | Version X.X.X includes fix               |
| TBD        | Patch removed from project       | Migrated to fixed component version      |

---

## References

- **Upstream Bug Report:** https://github.com/espressif/esp-wifi-remote/issues/XXX
- **ESP-Hosted Documentation:** https://github.com/espressif/esp-hosted-mcu
- **ESP32-P4 WiFi Setup Guide:** https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md
- **Project WiFi Architecture:** See `WIFI_ARCHITECTURE.md`

---

**Last Updated:** 2025-10-08  
**Patch Status:** ⏰ Active (waiting for upstream fix)  
**Component Version:** esp_wifi_remote 1.1.4  
**ESP-IDF Version:** 5.5.1
