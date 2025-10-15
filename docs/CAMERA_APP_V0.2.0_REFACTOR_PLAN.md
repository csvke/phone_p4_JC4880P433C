# Camera App v0.2.0 Refactoring Plan

**Project:** JC4880P443C Phone Camera Application  
**Current Version:** v0.2.0 (Monolithic)  
**Target Version:** v0.3.0 (Modular Architecture)  
**JIRA Key:** JC4880P443-15  
**Date:** October 15, 2025  
**Author:** Development Team

---

## 📋 Executive Summary

This document outlines the refactoring plan for the Camera Application to transform it from a monolithic 771-line `camera.c` file into a modular, maintainable, and reusable architecture. The refactoring focuses on:

1. **Code Organization** - Separate concerns into logical modules
2. **Reusability** - Enable code sharing with future UVC camera implementation
3. **Maintainability** - Cleaner structure for debugging and feature addition
4. **Sensor Calibration** - JSON-based ISP profiles (inspired by manufacturer's approach)
5. **Resolution Independence** - Brookesia-styled UI that adapts to different displays
6. **Professional Features** - JPEG capture and H.264 video recording

---

## 🎯 Project Goals

### Primary Objectives
- ✅ **Live Preview** - Real-time camera feed display
- ✅ **Photo Capture** - Single frame capture saved as JPEG
- ✅ **Video Recording** - H.264 video recording with start/pause/stop
- ✅ **Source Selection** - Switch between built-in OV02C10 CSI or external USB UVC
- ✅ **ISP Controls** - User-adjustable brightness, contrast, saturation, hue, CCM
- ✅ **Sensor Profiles** - JSON-based calibration for different sensors

### Out of Scope (For Now)
- ❌ Thermal camera application (requires different processing pipeline)
- ❌ AI/ML features (face detection, pedestrian detection)
- ❌ Audio recording (video only)
- ❌ Multiple camera support (one source at a time)

---

## 📊 Current State Analysis

### Current Architecture
```
camera.c (771 lines) - Monolithic file containing:
├── Camera Controller (CSI) initialization
├── ISP Processor configuration (CCM, AWB, Color)
├── OV02C10 Sensor initialization
├── LVGL Canvas integration
├── Frame acquisition loop
├── Preview display logic
└── Resource cleanup
```

### Issues Identified
1. **Monolithic Structure** - All functionality in one file
2. **Hardcoded Configuration** - Resolution and ISP settings in #defines
3. **No Abstraction** - CSI-specific code mixed with generic logic
4. **No Calibration System** - ISP values hardcoded
5. **Limited Features** - No photo/video capture capabilities
6. **Unused Code** - `ui.c` and `ui.h` files not integrated
7. **Commented Test Code** - 1920x1080 config commented out

### What Works Well
- ✅ Stable 30.1 FPS @ 1288x728 with 1-lane MIPI CSI
- ✅ ISP pipeline functional (CCM, AWB, Color adjustments)
- ✅ LVGL integration working
- ✅ Efficient PSRAM frame buffer usage
- ✅ Proper resource cleanup

---

## 🏗️ Proposed Architecture

```
┌─────────────────────────────────────────────────────────────┐
│              Camera Application (Brookesia App)              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  camera_app.c - Main App Entry Point                   │ │
│  │  • Implements ESP_Brookesia_PhoneApp interface         │ │
│  │  • Manages app lifecycle (init/run/pause/resume/close) │ │
│  │  • Coordinates UI and camera pipeline                  │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                  UI Layer (Brookesia Styled)                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ ui_preview.c │  │ ui_capture.c │  │ ui_playback.c│      │
│  │ Live Preview │  │ Photo/Video  │  │ Gallery View │      │
│  │ Screen       │  │ Controls     │  │ & Playback   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ui_controls.c │  │ ui_settings.c│  │ ui_styles.c  │      │
│  │Buttons/Status│  │ISP Sliders   │  │Resolution    │      │
│  │Indicators    │  │& Toggles     │  │Independent   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              Camera Pipeline (Source Abstraction)            │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  camera_pipeline.c - Abstract Camera Interface         │ │
│  │  • camera_source_t (CSI or UVC)                        │ │
│  │  • Unified frame acquisition API                       │ │
│  │  • Source switching logic                              │ │
│  └────────────────────────────────────────────────────────┘ │
│         ↓                              ↓                     │
│  ┌──────────────┐              ┌──────────────┐            │
│  │camera_csi.c  │              │camera_uvc.c  │            │
│  │MIPI CSI      │              │USB UVC       │            │
│  │OV02C10 Driver│              │Driver        │            │
│  │(Current)     │              │(Future)      │            │
│  └──────────────┘              └──────────────┘            │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              Processing Layer (Reusable)                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │camera_isp.c  │  │camera_codec.c│  │camera_storage│      │
│  │ISP Pipeline  │  │JPEG Encoding │  │File I/O      │      │
│  │CCM, AWB, AE  │  │H.264 Encoding│  │SD Card Mgmt  │      │
│  │Color Adjust  │  │(ESP32-P4)    │  │Path Handling │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  camera_profile.c - Sensor Calibration (JSON)          │ │
│  │  • Load sensor-specific ISP profiles                   │ │
│  │  • AWB gains, AE weights, CCM matrices                 │ │
│  │  • Runtime and compile-time profiles                   │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              Display Layer (Resolution Independent)          │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  camera_display.c - LVGL Integration                   │ │
│  │  • Canvas rendering with frame buffer                  │ │
│  │  • Brookesia theme/style application                   │ │
│  │  • Resolution scaling and aspect ratio handling        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 📁 Proposed File Structure

```
components/apps/camera/
├── CMakeLists.txt                  # Build configuration
├── idf_component.yml               # Component dependencies
├── Kconfig                         # Camera app configuration options
│
├── include/                        # Public API Headers
│   ├── camera_app.h                # Main app interface (Brookesia app)
│   ├── camera_pipeline.h           # Abstract camera pipeline API
│   ├── camera_types.h              # Common types, enums, structs
│   ├── camera_isp.h                # ISP API (reusable across sources)
│   ├── camera_codec.h              # JPEG/H.264 encoder API
│   ├── camera_storage.h            # File storage API
│   ├── camera_profile.h            # Sensor calibration profiles API
│   └── camera_ui.h                 # UI components API
│
├── src/
│   ├── core/                       # Core Functionality
│   │   ├── camera_app.c            # Main Brookesia app (~300 lines)
│   │   ├── camera_pipeline.c       # Pipeline orchestration (~200 lines)
│   │   ├── camera_csi.c            # CSI/MIPI implementation (~250 lines)
│   │   └── camera_uvc.c            # UVC implementation (future ~200 lines)
│   │
│   ├── processing/                 # Processing Modules
│   │   ├── camera_isp.c            # ISP pipeline (~350 lines)
│   │   ├── camera_codec.c          # JPEG/H.264 encoding (~250 lines)
│   │   ├── camera_storage.c        # File I/O (~150 lines)
│   │   └── camera_profile.c        # JSON profile loader (~200 lines)
│   │
│   ├── ui/                         # UI Components
│   │   ├── ui_preview.c            # Preview screen (~150 lines)
│   │   ├── ui_capture.c            # Capture controls (~150 lines)
│   │   ├── ui_playback.c           # Gallery/playback (~200 lines)
│   │   ├── ui_controls.c           # Buttons/sliders (~200 lines)
│   │   ├── ui_settings.c           # Settings panel (~200 lines)
│   │   └── ui_styles.c             # Brookesia theme wrapper (~100 lines)
│   │
│   └── utils/                      # Utility Functions
│       ├── camera_display.c        # LVGL canvas helper (~150 lines)
│       └── camera_events.c         # Event system (~100 lines)
│
├── profiles/                       # Sensor Calibration Profiles (JSON)
│   ├── ov02c10_default.json        # OV02C10 ISP calibration
│   ├── ov02c10_outdoor.json        # Outdoor lighting profile
│   ├── ov02c10_indoor.json         # Indoor lighting profile
│   └── sensor_profile_schema.json  # JSON schema documentation
│
└── assets/                         # Assets
    ├── camera_app_icon.c           # App icon
    └── README.md                   # Asset documentation
```

**Files to Delete:**
- ❌ `ui/ui.c` - Unused placeholder
- ❌ `ui/ui.h` - Unused placeholder

**Total Estimated Lines:**
- Core: ~950 lines
- Processing: ~950 lines
- UI: ~1000 lines
- Utils: ~250 lines
- **Total: ~3150 lines** (vs current 771 lines, but much more maintainable)

---

## 🔧 Key Components Description

### 1. Camera Pipeline (`camera_pipeline.c`)
**Purpose:** Abstract camera source interface

**Features:**
- Source type selection (CSI or UVC)
- Unified frame acquisition API
- Source-agnostic frame handling
- Hot-swapping between sources (future)

**API:**
```c
typedef enum {
    CAMERA_SOURCE_CSI,     // Built-in MIPI CSI
    CAMERA_SOURCE_UVC      // USB UVC
} camera_source_type_t;

esp_err_t camera_pipeline_init(camera_pipeline_config_t *config, 
                                camera_pipeline_handle_t *out_handle);
esp_err_t camera_pipeline_start(camera_pipeline_handle_t handle);
esp_err_t camera_pipeline_get_frame(camera_pipeline_handle_t handle, 
                                     uint8_t **frame, size_t *size);
esp_err_t camera_pipeline_stop(camera_pipeline_handle_t handle);
```

### 2. ISP Module (`camera_isp.c`)
**Purpose:** Reusable ISP pipeline configuration

**Features:**
- CCM (Color Correction Matrix)
- AWB (Automatic White Balance)
- AE (Auto Exposure)
- Color adjustments (brightness, contrast, saturation, hue)
- Profile-based configuration

**API:**
```c
esp_err_t camera_isp_init(camera_isp_config_t *config, camera_isp_handle_t *out_handle);
esp_err_t camera_isp_load_profile(camera_isp_handle_t handle, const char *profile_path);
esp_err_t camera_isp_set_ccm(camera_isp_handle_t handle, float matrix[3][3]);
esp_err_t camera_isp_set_brightness(camera_isp_handle_t handle, int8_t value);
esp_err_t camera_isp_enable_awb(camera_isp_handle_t handle, bool enable);
esp_err_t camera_isp_enable_ae(camera_isp_handle_t handle, bool enable);
```

### 3. Sensor Profiles (`camera_profile.c`)
**Purpose:** JSON-based sensor calibration (inspired by manufacturer's `sc2336_custom.json`)

**Profile Structure:**
```json
{
    "version": 1,
    "sensor": "OV02C10",
    "resolution": {
        "width": 1288,
        "height": 728,
        "fps": 30
    },
    "awb": {
        "sample_point": "after_ccm",
        "min_counted": 2000,
        "window": {
            "x_start_ratio": 0.166,
            "y_start_ratio": 0.166,
            "x_end_ratio": 0.833,
            "y_end_ratio": 0.833
        },
        "white_patch": {
            "luminance_min": 0,
            "luminance_max": 660,
            "rg_ratio_max": 3.999,
            "bg_ratio_max": 3.999
        }
    },
    "ae": {
        "sample_point": "after_demosaic",
        "target_luminance": 130,
        "weight_matrix": [
            1, 1, 2, 1, 1,
            1, 2, 3, 2, 1,
            1, 3, 5, 3, 1,
            1, 2, 3, 2, 1,
            1, 1, 2, 1, 1
        ]
    },
    "ccm": {
        "matrix": [
            [1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 0.75]
        ],
        "saturation": true
    },
    "color": {
        "brightness": 0,
        "contrast": 1.0,
        "saturation": 1.0,
        "hue": 0
    }
}
```

### 4. Codec Module (`camera_codec.c`)
**Purpose:** Image and video encoding

**Features:**
- JPEG encoding for photo capture
- H.264 encoding for video recording (leveraging ESP32-P4 hardware)
- Configurable quality settings
- Frame rate control

**API:**
```c
// JPEG
esp_err_t camera_codec_jpeg_encode(uint8_t *rgb565, size_t width, size_t height,
                                    uint8_t **jpeg_out, size_t *jpeg_size, int quality);

// H.264
esp_err_t camera_codec_h264_init(camera_codec_h264_config_t *config);
esp_err_t camera_codec_h264_encode_frame(uint8_t *frame, size_t size);
esp_err_t camera_codec_h264_finalize(char **output_path);
```

### 5. UI Styles (`ui_styles.c`)
**Purpose:** Resolution-independent UI (Brookesia best practices)

**Features:**
- Automatic scaling based on screen resolution
- Consistent spacing and sizing ratios
- Brookesia theme integration
- Support for different aspect ratios

**API:**
```c
typedef struct {
    uint16_t screen_width;
    uint16_t screen_height;
    lv_coord_t button_size;
    lv_coord_t icon_size;
    lv_coord_t padding;
    lv_coord_t margin;
    const lv_font_t *title_font;
    const lv_font_t *label_font;
} camera_ui_style_t;

esp_err_t camera_ui_style_init(uint16_t width, uint16_t height);
const camera_ui_style_t *camera_ui_style_get(void);
```

---

## 📝 Implementation Plan

### Phase 0: Preparation & Cleanup (1 hour)
**Goal:** Prepare repository and create clean structure

**Tasks:**
1. Create new branch: `JC4880P443-15-camera-refactor-v2`
2. Delete unused files: `ui/ui.c`, `ui/ui.h`
3. Create directory structure:
   ```bash
   mkdir -p components/apps/camera/src/{core,processing,ui,utils}
   mkdir -p components/apps/camera/profiles
   mkdir -p components/apps/camera/docs
   ```
4. Backup current working code
5. Document current performance baseline

**Deliverables:**
- Clean directory structure
- Git branch created
- Baseline documentation

**Commit:** `"JC4880P443-15: Prepare for camera refactor - cleanup & structure"`

---

### Phase 1: Core Camera Pipeline (4-5 hours)
**Goal:** Extract and abstract camera pipeline

#### Phase 1A: Pipeline Abstraction (2 hours)
**Tasks:**
1. Create `camera_pipeline.h` - Define abstract interface
2. Create `camera_pipeline.c` - Implement source abstraction
3. Define `camera_source_type_t` enum
4. Create unified frame acquisition API
5. Add source selection logic

**Deliverables:**
- `camera_pipeline.h` - 100 lines
- `camera_pipeline.c` - 200 lines

#### Phase 1B: CSI Module Extraction (2-3 hours)
**Tasks:**
1. Create `camera_csi.h` - CSI-specific API
2. Create `camera_csi.c` - Extract from current `camera.c`
3. Move CSI controller initialization
4. Move OV02C10 sensor initialization
5. Implement pipeline interface
6. Test that camera still works

**Deliverables:**
- `camera_csi.h` - 50 lines
- `camera_csi.c` - 250 lines
- Working camera with new structure

**Commit:** `"JC4880P443-15: Extract camera pipeline with source abstraction"`

---

### Phase 2: ISP Module (2-3 hours)
**Goal:** Extract ISP into reusable module

**Tasks:**
1. Create `camera_isp.h` - ISP API
2. Create `camera_isp.c` - Extract ISP code from `camera.c`
3. Move ISP processor initialization
4. Move CCM configuration
5. Move AWB configuration
6. Move Color adjustments
7. Add profile-based configuration support
8. Test ISP functionality

**Deliverables:**
- `camera_isp.h` - 80 lines
- `camera_isp.c` - 350 lines
- ISP working with abstracted interface

**Commit:** `"JC4880P443-15: Extract ISP module with profile support"`

---

### Phase 3: Sensor Calibration System (2-3 hours)
**Goal:** Implement JSON-based sensor profiles

**Tasks:**
1. Create `camera_profile.h` - Profile API
2. Create `camera_profile.c` - JSON parser
3. Create `profiles/ov02c10_default.json` - Default profile
4. Create `profiles/sensor_profile_schema.json` - Schema docs
5. Implement profile loading
6. Implement profile application to ISP
7. Add profile validation
8. Test profile loading and application

**Deliverables:**
- `camera_profile.h` - 60 lines
- `camera_profile.c` - 200 lines
- `ov02c10_default.json` - JSON profile
- Profile schema documentation

**Commit:** `"JC4880P443-15: Add sensor calibration profile system"`

---

### Phase 4: Codec & Storage (3-4 hours)
**Goal:** Implement photo and video capture

#### Phase 4A: JPEG Encoding (1.5 hours)
**Tasks:**
1. Create `camera_codec.h` - Codec API
2. Implement JPEG encoder in `camera_codec.c`
3. Add quality settings
4. Test JPEG encoding

#### Phase 4B: H.264 Encoding (1.5 hours)
**Tasks:**
1. Implement H.264 encoder in `camera_codec.c`
2. Leverage ESP32-P4 hardware acceleration
3. Add bitrate configuration
4. Test H.264 encoding

#### Phase 4C: Storage Layer (1 hour)
**Tasks:**
1. Create `camera_storage.h` - Storage API
2. Create `camera_storage.c` - File I/O
3. Implement JPEG save
4. Implement video file handling
5. Add SD card detection
6. Test file writing

**Deliverables:**
- `camera_codec.h` - 70 lines
- `camera_codec.c` - 250 lines
- `camera_storage.h` - 50 lines
- `camera_storage.c` - 150 lines
- Working photo and video capture

**Commit:** `"JC4880P443-15: Add JPEG/H.264 encoding and storage"`

---

### Phase 5: UI Components (4-5 hours)
**Goal:** Create Brookesia-styled UI

#### Phase 5A: UI Styles (1 hour)
**Tasks:**
1. Create `ui_styles.h` - Style API
2. Create `ui_styles.c` - Resolution-independent styling
3. Implement Brookesia theme integration
4. Add automatic scaling logic

#### Phase 5B: Preview Screen (1 hour)
**Tasks:**
1. Create `ui_preview.c` - Preview screen
2. Implement canvas display
3. Add status indicators (FPS, resolution)
4. Test preview rendering

#### Phase 5C: Capture Controls (1.5 hours)
**Tasks:**
1. Create `ui_capture.c` - Control buttons
2. Add photo button
3. Add video record/pause/stop buttons
4. Add source selection dropdown
5. Wire up callbacks

#### Phase 5D: Settings Panel (1.5 hours)
**Tasks:**
1. Create `ui_settings.c` - Settings panel
2. Add ISP sliders (brightness, contrast, saturation, hue)
3. Add CCM sliders (R, G, B)
4. Add AWB/AE toggle switches
5. Add profile selection
6. Test real-time ISP adjustments

**Deliverables:**
- `ui_styles.h/c` - 100 lines total
- `ui_preview.c` - 150 lines
- `ui_capture.c` - 150 lines
- `ui_settings.c` - 200 lines
- Complete UI functional

**Commit:** `"JC4880P443-15: Add Brookesia-styled UI components"`

---

### Phase 6: Brookesia App Integration (2-3 hours)
**Goal:** Integrate as proper Brookesia app

**Tasks:**
1. Create `camera_app.h` - App interface
2. Create `camera_app.c` - Brookesia app implementation
3. Implement app lifecycle (init/run/pause/resume/close)
4. Wire up UI to pipeline
5. Add event handling
6. Test app lifecycle
7. Test integration with Brookesia phone system

**Deliverables:**
- `camera_app.h` - 40 lines
- `camera_app.c` - 300 lines
- Fully integrated Brookesia app

**Commit:** `"JC4880P443-15: Complete Brookesia camera app integration"`

---

### Phase 7: Auto Exposure (Optional, 2-3 hours)
**Goal:** Implement Auto Exposure controller

**Tasks:**
1. Add AE controller to `camera_isp.c`
2. Implement AE callbacks
3. Add exposure adjustment logic
4. Add manual override option
5. Test in different lighting conditions

**Deliverables:**
- AE functionality in `camera_isp.c`
- AE configuration in JSON profiles

**Commit:** `"JC4880P443-15: Add auto exposure support"`

---

### Phase 8: Testing & Documentation (2-3 hours)
**Goal:** Comprehensive testing and documentation

**Tasks:**
1. Full integration testing
2. Performance regression testing (ensure 30.1 FPS maintained)
3. Memory leak testing
4. Create user documentation
5. Create API documentation
6. Create developer guide
7. Update README
8. Add usage examples

**Deliverables:**
- Test report
- API documentation
- User guide
- Developer guide
- Updated README

**Commit:** `"JC4880P443-15: Camera app refactor complete - tested and documented"`

---

## ⏱️ Timeline & Resource Estimate

| Phase | Description | Estimated Time | Dependencies |
|-------|-------------|----------------|--------------|
| 0 | Preparation | 1 hour | None |
| 1 | Core Pipeline | 4-5 hours | Phase 0 |
| 2 | ISP Module | 2-3 hours | Phase 1 |
| 3 | Calibration System | 2-3 hours | Phase 2 |
| 4 | Codec & Storage | 3-4 hours | Phase 2 |
| 5 | UI Components | 4-5 hours | Phase 1, 2 |
| 6 | Brookesia Integration | 2-3 hours | Phase 5 |
| 7 | Auto Exposure (Optional) | 2-3 hours | Phase 2 |
| 8 | Testing & Documentation | 2-3 hours | All phases |
| **Total (without AE)** | | **18-24 hours** | |
| **Total (with AE)** | | **20-27 hours** | |

**Realistic Timeline:**
- **Minimum:** 3 working days (if working full-time)
- **Recommended:** 4-5 working days (with testing and polish)
- **Conservative:** 1 week (with documentation and thorough testing)

---

## 🎯 Reusability Analysis

### For Future UVC Camera Implementation

#### ✅ Fully Reusable (No Changes Required)
| Module | Lines | Reusability | Notes |
|--------|-------|-------------|-------|
| `camera_isp.c` | 350 | 100% | ISP works same for any RGB source |
| `camera_codec.c` | 250 | 100% | Encoding is source-agnostic |
| `camera_storage.c` | 150 | 100% | File I/O is source-agnostic |
| `camera_profile.c` | 200 | 100% | Profile system is generic |
| `camera_display.c` | 150 | 100% | LVGL integration same |
| All UI files | 1000 | 100% | UI is source-agnostic |
| `camera_events.c` | 100 | 100% | Event system generic |
| **Subtotal** | **2200** | **100%** | |

#### 🔧 Requires Minor Adaptation
| Module | Lines | Reusability | Required Changes |
|--------|-------|-------------|------------------|
| `camera_pipeline.c` | 200 | 90% | Add UVC to source enum (already designed for it) |
| `camera_app.c` | 300 | 95% | Add source selection UI dropdown |
| **Subtotal** | **500** | **92%** | |

#### 📝 New Implementation Required
| Module | Lines | Reusability | Notes |
|--------|-------|-------------|-------|
| `camera_uvc.c` | 200 | 0% | New USB UVC driver implementation |
| **Subtotal** | **200** | **0%** | |

#### Summary
- **Total Lines:** ~2900 lines
- **Reusable:** ~2700 lines (93%)
- **New Code:** ~200 lines (7%)

**Conclusion:** 93% code reuse for UVC camera implementation!

---

## 🎨 Benefits Summary

### Development Benefits
✅ **Modularity** - Each file has single, clear responsibility  
✅ **Testability** - Each module can be tested independently  
✅ **Debuggability** - Easier to isolate and fix issues  
✅ **Maintainability** - Changes localized to specific modules  
✅ **Readability** - Smaller files, clearer structure

### Feature Benefits
✅ **Photo Capture** - Professional JPEG encoding  
✅ **Video Recording** - H.264 hardware-accelerated encoding  
✅ **ISP Controls** - Real-time adjustments via UI sliders  
✅ **Sensor Profiles** - JSON-based calibration like manufacturer  
✅ **Source Selection** - Easy switching between CSI/UVC  
✅ **Resolution Independence** - Brookesia-styled, works on any display

### Future Benefits
✅ **UVC Support** - 93% code reuse, only need USB driver  
✅ **Multiple Sensors** - Easy to add new sensor profiles  
✅ **Team Development** - Clear boundaries for parallel work  
✅ **Third-Party Integration** - Other developers can adapt UI easily  
✅ **Thermal Camera** - Can reuse UI elements and display logic

---

## ⚠️ Risks & Mitigations

### Risk 1: Performance Regression
**Risk:** Modular architecture might impact frame rate  
**Impact:** High  
**Mitigation:**
- Keep frame processing path optimized
- Avoid unnecessary copies between modules
- Profile after each phase
- Target: Maintain 30.1 FPS @ 1288x728

### Risk 2: Memory Footprint Increase
**Risk:** More code might use more RAM  
**Impact:** Medium  
**Mitigation:**
- Monitor heap usage after each phase
- Use static allocation where possible
- Keep frame buffers in PSRAM
- Profile memory usage continuously

### Risk 3: Integration Issues with Brookesia
**Risk:** App might not integrate cleanly with Brookesia framework  
**Impact:** High  
**Mitigation:**
- Follow Brookesia app template strictly
- Test lifecycle methods thoroughly
- Reference manufacturer's Camera.cpp structure
- Early integration testing in Phase 6

### Risk 4: H.264 Encoding Complexity
**Risk:** Hardware H.264 encoder might be complex to implement  
**Impact:** Medium  
**Mitigation:**
- Start with raw frame recording first
- Use ESP-IDF examples as reference
- Make H.264 optional initially
- Can fall back to MJPEG if needed

### Risk 5: JSON Profile Parsing
**Risk:** Parsing complex JSON might be resource-intensive  
**Impact:** Low  
**Mitigation:**
- Parse profiles only at initialization
- Cache parsed values
- Use cJSON library (efficient)
- Validate profiles at build time if possible

---

## ✅ Success Criteria

### Functional Requirements
- ✅ Live preview @ 30.1 FPS (1288x728)
- ✅ Photo capture saves JPEG to SD card
- ✅ Video recording saves H.264 to SD card
- ✅ ISP adjustments work in real-time via UI
- ✅ Sensor profiles load from JSON
- ✅ Source selection between CSI and UVC (UI ready, UVC future)
- ✅ App integrates with Brookesia phone system
- ✅ UI scales properly for 480x800 display

### Non-Functional Requirements
- ✅ Code coverage > 80% (by file count)
- ✅ No memory leaks (valgrind clean)
- ✅ Frame rate maintained (30.1 FPS)
- ✅ Heap usage < 2MB
- ✅ Build time < 3 minutes
- ✅ All files < 400 lines (maintainability)

### Documentation Requirements
- ✅ API documentation for all public interfaces
- ✅ User guide with screenshots
- ✅ Developer guide for extending
- ✅ JSON schema documentation
- ✅ Updated README with examples

---

## 📚 References

### Manufacturer's Code Structure
- Location: `/Users/frankieyuen/JC4880P443C/Manufacturer Docs/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/esp_brookesia_phone/components/apps/camera/`
- Key Files:
  - `Camera.cpp` / `Camera.hpp` - App structure
  - `app_camera_pipeline.cpp` - Pipeline pattern
  - `sc2336_custom.json` - Calibration profile example
  - `ui/` - UI component organization

### ESP-IDF Documentation
- Camera Controller: `esp_cam_ctlr` API
- ISP Driver: `driver/isp.h`
- ISP Components: `driver/isp_ccm.h`, `driver/isp_awb.h`, `driver/isp_ae.h`, `driver/isp_color.h`
- Video Encoding: ESP32-P4 hardware acceleration

### Brookesia Framework
- App Interface: `ESP_Brookesia_PhoneApp` class
- Theme System: Brookesia style API
- Component Guidelines: Brookesia best practices

---

## 🚦 Next Steps

### Immediate Actions (Before Starting)
1. ✅ Review this document
2. ✅ Approve architecture and file structure
3. ✅ Confirm timeline is acceptable
4. ✅ Assign resources
5. ✅ Set up testing environment

### To Begin Refactoring
1. Create branch: `JC4880P443-15-camera-refactor-v2`
2. Start with Phase 0 (Preparation)
3. Proceed incrementally through phases
4. Commit after each phase
5. Test thoroughly at each stage

### Communication Plan
- Daily standup on progress
- Commit messages reference JIRA key: JC4880P443-15
- Document blockers immediately
- Demo after Phase 6 (Brookesia integration)
- Final review after Phase 8 (Testing complete)

---

## 📞 Contact & Approval

**Document Author:** Development Team  
**Review Required By:**
- [ ] Technical Lead
- [ ] Product Owner
- [ ] Architecture Review

**Approval Status:** 🟡 Pending Review

**Questions/Feedback:** Please add comments or schedule review meeting

---

**Document Version:** 1.0  
**Last Updated:** October 15, 2025  
**Next Review:** After Phase 0 completion
