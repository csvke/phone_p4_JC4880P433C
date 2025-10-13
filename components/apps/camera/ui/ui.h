#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Global UI objects
extern lv_obj_t *ui_ScreenCameraCsi;
extern lv_obj_t *ui_PanelCameraCsiTitle;
extern lv_obj_t *ui_LabelCameraCsiTitle;
extern lv_obj_t *ui_ImageCameraCsiPreview;
extern lv_obj_t *ui_PanelCameraCsiControls;
extern lv_obj_t *ui_ButtonCameraCsiCapture;
extern lv_obj_t *ui_ButtonCameraCsiGallery;
extern lv_obj_t *ui_ButtonCameraCsiSettings;

// Function declarations
void ui_camera_init(lv_obj_t *parent);
void ui_init(void);
lv_obj_t *ui_get_preview_area(void);
lv_obj_t *ui_get_capture_btn(void);
lv_obj_t *ui_get_gallery_btn(void);
lv_obj_t *ui_get_settings_btn(void);

#ifdef __cplusplus
}
#endif

#endif // UI_H