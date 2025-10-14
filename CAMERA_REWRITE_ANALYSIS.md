# Camera Rewrite Analysis - JC4880P443-13

**Date:** October 14, 2025  
**Status:** WIP - System crashes after app restart  
**Goal:** Clean rewrite with proper task decoupling

---

## Current Implementation Issues

### Critical Bugs
1. **Watchdog Timeout (5 seconds)**
   - Camera task calls `lv_lock()` which blocks when LVGL is busy
   - Blocking time: 100-230ms during rendering contention
   - Starves idle task on CPU1 → watchdog triggers
   - Error: `E (49437) task_wdt: CPU 1: camera_preview`

2. **System Crash on App Restart**
   - First launch: Works, shows camera preview
   - Kill app: Camera cleanup completes
   - Relaunch: **System crashes immediately**
   - Likely cause: ISP/Camera controller not properly released

3. **Frame Rate Instability**
   - Stable 30fps initially (6-7ms canvas update)
   - Drops to 8fps when LVGL lock contention occurs
   - Canvas update jumps to 100-230ms during drops

### Root Cause
**Architecture Violation:** Camera task calling LVGL functions directly causes cross-task lock contention. Camera and LVGL tasks MUST be completely decoupled.

---

## OV02C10 Driver Review

### Current Settings (Modified)
```c
// File: esp-video-components/esp_cam_sensor/sensors/ov02c10/private_include/ov02c10_settings.h
// Lines 44-51

// VTS (Vertical Timing Size) - Frame timing control
#define OV02C10_1LANE_10BIT_1288x728_30FPS_VTS   1500  // MODIFIED from 0x048c (1164)
#define OV02C10_1LANE_10BIT_1920x1080_30FPS_VTS  1500  // MODIFIED from 0x048c (1164)  
#define OV02C10_2LANE_10BIT_1920x1080_30FPS_VTS  1500  // MODIFIED from 0x048c (1164)
```

### Original Settings (Baseline)
```c
// Original from esp-video-components repository
#define OV02C10_1LANE_10BIT_1288x728_30FPS_VTS   0x048c  // 1164 decimal
#define OV02C10_1LANE_10BIT_1920x1080_30FPS_VTS  0x048c  // 1164 decimal
#define OV02C10_2LANE_10BIT_1920x1080_30FPS_VTS  0x048c  // 1164 decimal
```

### Analysis
- **Question:** Why was VTS changed from 1164 to 1500?
- **Impact:** Increases frame period (slower timing)
- **Status:** May be unnecessary - original value worked
- **Action:** Test with original VTS=1164 first

### Register 0x4837 (Critical)
```c
// MUST NOT CHANGE - Manufacturer calibrated
{0x4837, 0x15}  // MIPI PCLK period
```
This register is **critical** and must remain at 0x15 as per manufacturer calibration.

---

## Baseline Architecture (Commit d910666d)

### File Structure
```
components/apps/camera/
├── src/
│   └── camera_preview.c    (Original simple implementation)
└── ui/
    └── camera.c             (LVGL UI code)
```

### Original camera_preview.c Pattern
```c
// Simple LVGL image widget approach
static lv_obj_t *camera_img = NULL;

void camera_preview_init(void) {
    // Create LVGL image widget
    camera_img = lv_img_create(lv_scr_act());
    lv_img_set_src(camera_img, &img_camera_preview);
}

// Camera task just acquires frames
// LVGL timer/task handles display updates separately
```

**Key Insight:** Original used **LVGL image widget**, not direct canvas manipulation. Camera and display were decoupled.

---

## Recommended Rewrite Approach

### Architecture Changes

#### 1. Task Separation
```
┌─────────────────┐         ┌──────────────────┐
│  Camera Task    │         │   LVGL Task      │
│  (CPU 1)        │         │   (CPU 0)        │
├─────────────────┤         ├──────────────────┤
│ • Acquire frame │         │ • Check flag     │
│ • Process ISP   │  Flag   │ • Update canvas  │
│ • Set flag      │────────>│ • Clear flag     │
│ • NO LVGL calls!│         │ • Render display │
└─────────────────┘         └──────────────────┘
```

#### 2. Communication Mechanism
**Option A: Atomic Flag (Simplest)**
```c
static volatile bool frame_ready = false;
static uint8_t *latest_frame_buffer = NULL;

// Camera task
void camera_task(void) {
    while (running) {
        acquire_frame(&frame);
        latest_frame_buffer = frame.buffer;
        frame_ready = true;  // Signal LVGL
        vTaskDelay(1);       // Yield to idle task
    }
}

// LVGL timer callback (runs in LVGL task)
void lvgl_update_timer_cb(lv_timer_t *timer) {
    if (frame_ready) {
        lv_canvas_set_buffer(canvas, latest_frame_buffer, ...);
        lv_obj_invalidate(canvas);
        frame_ready = false;
    }
}
```

**Option B: Event System**
```c
// Camera task posts event
lv_event_send(canvas, LV_EVENT_REFRESH, NULL);

// LVGL event handler updates canvas
```

**Option C: LVGL Message Queue**
```c
// Camera task sends message
lv_msg_send(MSG_NEW_FRAME, frame_buffer);

// LVGL subscriber updates canvas
```

#### 3. Cleanup Sequence (Fix Restart Crash)
```c
esp_err_t camera_stop(void) {
    // 1. Stop camera task
    preview_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for task to exit
    
    // 2. Stop camera controller BEFORE ISP
    esp_cam_ctlr_stop(cam_handle);
    
    // 3. Stop ISP processor
    esp_isp_del_processor(isp_proc);
    
    // 4. Delete camera controller
    esp_cam_ctlr_del(cam_handle);
    
    // 5. Free frame buffer
    free(frame_buffer);
    
    // 6. NULL all handles
    cam_handle = NULL;
    isp_proc = NULL;
    frame_buffer = NULL;
    
    return ESP_OK;
}
```

---

## Implementation Plan

### Phase 1: Driver Verification
1. ✅ Review OV02C10 settings (documented above)
2. [ ] Test with original VTS=1164
3. [ ] Verify register 0x4837=0x15 unchanged
4. [ ] Confirm 1288x728 @ 1-lane still optimal

### Phase 2: Clean Rewrite
1. [ ] Start from commit d910666d baseline
2. [ ] Implement camera acquisition task (NO LVGL calls)
3. [ ] Implement LVGL timer callback for display
4. [ ] Use atomic flag for frame signaling
5. [ ] Add proper cleanup sequence

### Phase 3: Testing
1. [ ] Verify 30fps acquisition maintained
2. [ ] Verify display updates smoothly
3. [ ] Test app restart (no crash)
4. [ ] Test app switching (no crash)
5. [ ] Run for 60+ seconds (no watchdog)

---

## Working Components to Preserve

✅ **Camera Acquisition:** 30.1 FPS stable (33,176 µs intervals)  
✅ **ISP Processing:** RAW10 → RGB565 conversion working  
✅ **Pure Callback Pattern:** Semaphore-based frame notification  
✅ **Resolution:** 1288x728 @ 1-lane optimal for ISP bandwidth (80MHz)  
✅ **Register 0x4837:** 0x15 (MIPI PCLK period) - DO NOT CHANGE

---

## Reference Logs

### Successful Frame Acquisition
```
I (26347) camera: Frame #7 received, first pixels: 0x18a3 0x20e4 0x18a3
I (26348) camera: Frame #7 interval: 33174 us (30.1 fps)
I (26348) camera: Frame #7 - Start: 0x18a3 Mid: 0x49a8 End: 0x18c3
I (26354) camera: Frame #7 timing - Cache sync: 8 us, Canvas update: 6406 us
```

### Watchdog Crash
```
E (49437) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (49437) task_wdt:  - IDLE1 (CPU 1)
E (49437) task_wdt: Tasks currently running:
E (49437) task_wdt: CPU 0: IDLE0
E (49437) task_wdt: CPU 1: camera_preview
```

---

## Conclusion

The current implementation has **architectural flaws** that prevent stable operation:
1. Cross-task LVGL calls cause lock contention
2. Improper cleanup causes restart crashes
3. VTS changes may be unnecessary

**Next Steps:**
1. Revert VTS to original 1164
2. Implement clean task-decoupled architecture
3. Test thoroughly with proper cleanup sequence

This rewrite will achieve:
- ✅ Stable 30fps display
- ✅ No watchdog timeouts
- ✅ Smooth app restart
- ✅ Clean separation of concerns
