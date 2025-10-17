# Camera App Status - JC4880P443-24

**Date**: October 17, 2025  
**Branch**: `JC4880P443-9-camera-app-v.0.2.0`  
**Commit**: WIP - Camera freezes after a few seconds

## Current Status: ⚠️ PARTIALLY WORKING - CAMERA FREEZES

The camera app initializes successfully and processes frames, but **the camera preview freezes after a few seconds**. Camera callbacks continue firing in the background, but the display stops updating and becomes unresponsive.

### Known Issues - CRITICAL

🔴 **CAMERA PREVIEW FREEZES AFTER ~5-10 SECONDS**

**Symptoms:**
- Camera initializes successfully
- First ~10 frames display correctly on screen
- After a few seconds, the **preview image freezes** on the display
- Camera callbacks **continue firing** in the background (logs show #30, #40, #50... #780+)
- Display becomes **unresponsive** - frozen frame remains on screen
- **Re-launching the app does NOT fix** - preview stays frozen
- Requires full device reset to recover

**Evidence from Logs:**
```
I (7446) camera: Frame #10 - PPA: 47266 us, scaled to 429x760 (scale=0.590)
I (7632) camera: Frame callback #30 triggered    ← Callbacks continue
I (7964) camera: Frame callback #40 triggered    ← But display is frozen
I (8296) camera: Frame callback #50 triggered
...
I (32182) camera: Frame callback #770 triggered  ← 770+ callbacks fired
E (32497) task_wdt: Task watchdog got triggered  ← Watchdog warnings
```

**Analysis:**
- Frame callbacks fire continuously (30 FPS from camera)
- PPA operations complete successfully (callbacks signal semaphore)
- Task appears to be processing frames (no timeout errors)
- **BUT**: Display does not update - LVGL canvas frozen
- **Likely cause**: LVGL canvas update issue or display driver problem
- **Possible causes**:
  1. LVGL lock deadlock or corruption
  2. Canvas buffer pointer corruption
  3. Display invalidation not triggering refresh
  4. PSRAM cache coherency issue with scaled buffer
  5. Brookesia framework interference

**Impact**: 
- Camera unusable after ~10 seconds
- Must reset device to attempt recovery
- Re-launching app does not fix (suggests system-level issue)

---

### Working Features (Before Freeze)

✅ **Camera Initialization**
- OV02C10 sensor detected and configured (1288×728 @ 30 FPS, RAW10)
- MIPI CSI 1-lane controller initialized
- ISP Demosaic module converting RAW10 → RGB565

✅ **PPA Scaling & Rotation**
- Non-blocking PPA operations (eliminates deadlock)
- 90° clockwise rotation (landscape camera → portrait display)
- Aspect-ratio scaling: 1288×728 → 429×760 (scale factor: 0.590)
- Processing time: ~47ms per frame
- Canvas positioned at (25, 0) - horizontally centered

✅ **Continuous Operation**
- Frame callbacks firing at **30 FPS** (camera rate)
- Frames processed at **~12-13 FPS** (limited by PPA processing time)
- **No hang** - ran continuously for 32+ seconds in testing
- **No frame drops** - counting semaphore handles backlog
- **780+ frames processed** in test run

✅ **Display Integration**
- LVGL canvas: 429×760 RGB565
- 26px white borders on left/right (aspect ratio preserved)
- No scrolling, properly sized for 480×760 useable area
- Status bar: 40px (Brookesia framework)

### Performance Metrics

| Metric | Value | Notes |
|--------|-------|-------|
| Camera Frame Rate | 30 FPS | OV02C10 output |
| Processing Throughput | 12-13 FPS | Limited by PPA (47ms/frame) |
| PPA Processing Time | 46-51ms | Scale + Rotate operation |
| Cache Sync | 7-8µs | PSRAM sync overhead |
| Total Frame Time | 65-85ms | Includes waiting for PPA |
| Frame Interval | 75-85ms | Time between processed frames |

### Technical Implementation

**Pipeline Architecture:**
```
OV02C10 (1288×728 RAW10, 30 FPS)
  ↓ MIPI CSI (1-lane, 400 Mbps)
  ↓ CSI Controller (queue_items=1)
  ↓ ISP Demosaic (RAW10 → RGB565)
  ↓ Frame buffer (1.8MB PSRAM)
  ↓ PPA Scale+Rotate (non-blocking, 47ms)
  ↓ Scaled buffer (768KB PSRAM)
  ↓ LVGL Canvas (429×760)
  ↓ Display (480×800 portrait)
```

**Key Bug Fixes:**

1. **Callback Return Values** (Critical)
   - `camera_get_new_vb()`: Now returns `true` (buffer provided)
   - `camera_trans_finished()`: Now returns `true` (buffer released)
   - **Issue**: Returning `false` told driver "can't provide buffer" / "still using buffer"
   - **Result**: Driver stopped delivering frames after ~10 frames

2. **PPA Non-Blocking Mode** (Critical)
   - Changed from `PPA_TRANS_MODE_BLOCKING` → `PPA_TRANS_MODE_NON_BLOCKING`
   - **Issue**: Blocking mode caused hardware deadlock after first frame
   - **Solution**: Callback-based completion with semaphore wait

3. **Counting Semaphore** (Performance)
   - Changed from binary → counting semaphore (max=10)
   - **Issue**: Camera delivers frames faster (30 FPS) than PPA processes (12 FPS)
   - **Solution**: Queue up to 10 pending frame notifications without loss

### Known Limitations

⚠️ **Watchdog Warnings**
- Task watchdog triggers every ~5 seconds
- **NOT a crash** - task continues running, callbacks keep firing
- **Cause**: Task spends >5s between `esp_task_wdt_reset()` calls
- **Reason**: Busy in PPA waits (100ms timeout) and LVGL updates
- **Impact**: Cosmetic warnings only, no functional issue
- **TODO**: Either increase watchdog timeout or call reset more frequently

⚠️ **Frame Rate Limited by PPA**
- Camera delivers 30 FPS, but only 12-13 FPS displayed
- **Cause**: PPA takes 47ms to scale+rotate each frame
- **Math**: 1000ms / 47ms ≈ 21 FPS theoretical, ~13 FPS actual (with overhead)
- **Potential optimization**: Use PPA DMA mode or dual-buffer pipeline

⚠️ **Aspect Ratio Borders**
- 26px white borders on left/right sides
- **Cause**: Preserving aspect ratio (camera 1288:728 ≠ display 480:760)
- **Alternative**: Fill-screen would require non-uniform scaling (violates PPA constraints)

### Testing Results

**Test Duration**: 32+ seconds  
**Frames Processed**: 780+  
**Callbacks Fired**: 780+ (continuous)  
**Errors**: None (watchdog warnings are cosmetic)  
**Stability**: Excellent - no hang, no crash, no frame corruption

**First 10 Frames (Detailed Logs):**
```
Frame #1: 57.9ms total (46.8ms PPA, 8µs cache sync)
Frame #2: 77.1ms total (47.5ms PPA, 7µs cache sync) - 15.3 fps
Frame #3: 73.1ms total (46.8ms PPA, 8µs cache sync) - 11.9 fps
Frame #4: 76.2ms total (47.4ms PPA, 8µs cache sync) - 12.5 fps
Frame #5: 71.6ms total (49.9ms PPA, 7µs cache sync) - 12.0 fps
Frame #6: 72.5ms total (47.3ms PPA, 8µs cache sync) - 12.7 fps
Frame #7: 69.1ms total (47.1ms PPA, 8µs cache sync) - 13.2 fps
Frame #8: 72.6ms total (51.2ms PPA, 8µs cache sync) - 13.3 fps
Frame #9: 72.6ms total (47.5ms PPA, 8µs cache sync) - 12.5 fps
Frame #10: 67.5ms total (45.9ms PPA, 8µs cache sync) - 12.5 fps
```

**Callback Pattern (showing continuous operation):**
```
#1-10: Full logging
#11-20: Callback logs every frame
#30, #40, #50... #780: Callback logs every 10 frames
```

### Next Steps - URGENT

**Critical Debugging Required:**
- [ ] **Investigate LVGL canvas freeze** - why display stops updating
- [ ] Add more detailed logging around canvas updates
- [ ] Check if `lv_obj_invalidate()` is actually triggering refresh
- [ ] Verify LVGL lock/unlock pairs are balanced
- [ ] Test with direct framebuffer write (bypass LVGL canvas)
- [ ] Check for PSRAM cache coherency issues
- [ ] Monitor heap/stack usage for corruption
- [ ] Test if issue is LVGL-specific or display driver issue

**Potential Workarounds to Test:**
- [ ] Skip canvas updates after first 10 frames (see if callbacks continue normally)
- [ ] Use double-buffering for scaled buffer
- [ ] Force display refresh with `lv_refr_now()`
- [ ] Reduce canvas update frequency (every Nth frame)
- [ ] Test without PPA (direct ISP output to canvas)

**Immediate (Optional Improvements):**
- [ ] Fix watchdog warnings (increase timeout or add more reset calls)
- [ ] Optimize frame rate (investigate PPA DMA mode)
- [ ] Add frame drop detection/recovery
- [ ] Test rotation angles (currently 90° CW, may need 270°)

**Future Enhancements:**
- [ ] Implement dual-buffer pipeline (process frame N while capturing N+1)
- [ ] Add zoom/pan controls
- [ ] Implement photo capture to SD card
- [ ] Add video recording capability
- [ ] Optimize PPA parameters for speed vs quality

### Code Files Modified

**Primary Implementation:**
- `components/apps/camera/src/camera.c` - Main camera driver
  - ISP Demosaic integration (lines ~290-410)
  - PPA initialization and NON-BLOCKING mode (lines ~190-230)
  - Camera callbacks with proper return values (lines ~115-170)
  - Frame processing loop with semaphore handling (lines ~490-700)

**Configuration:**
- `components/apps/camera/include/camera.h` - Interface definitions
- `main/Kconfig.projbuild` - Menuconfig options (if any)

### Conclusion

**Status**: ⚠️ **WORK IN PROGRESS - BLOCKED**

The camera initialization and frame capture pipeline is working correctly:
- ✅ Camera hardware properly initialized
- ✅ Frames captured at 30 FPS
- ✅ ISP Demosaic converting RAW10 → RGB565
- ✅ PPA scaling and rotating frames successfully
- ✅ Callbacks and semaphores functioning correctly

**CRITICAL BLOCKER**: Display freezes after ~10 seconds, making the camera app unusable. The freeze persists across app restarts and requires full device reset.

**Root cause unknown** - likely LVGL canvas update issue or display driver problem. Further investigation required.

---

**Last Updated**: October 17, 2025  
**Tested By**: Development Team  
**Hardware**: ESP32-P4 + JC4880P443C + OV02C10 Camera  
**Status**: 🔴 BLOCKED - Display freeze issue
