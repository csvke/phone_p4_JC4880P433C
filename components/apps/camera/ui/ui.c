#include "ui.h"

// Global UI objects
lv_obj_t *ui_ScreenCameraCsi;
lv_obj_t *ui_PanelCameraCsiTitle;
lv_obj_t *ui_LabelCameraCsiTitle;
lv_obj_t *ui_ImageCameraCsiPreview;
lv_obj_t *ui_PanelCameraCsiControls;
lv_obj_t *ui_ButtonCameraCsiCapture;
lv_obj_t *ui_ButtonCameraCsiGallery;
lv_obj_t *ui_ButtonCameraCsiSettings;

// Internal UI objects
static lv_obj_t *preview_area;
static lv_obj_t *capture_btn;
static lv_obj_t *gallery_btn;
static lv_obj_t *settings_btn;

void ui_camera_init(lv_obj_t *parent)
{
    if (!parent) return;

    // Create main container
    lv_obj_t *main_cont = lv_obj_create(parent);
    lv_obj_set_size(main_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(main_cont, 0, 0);
    lv_obj_set_style_radius(main_cont, 0, 0);

    // Create title panel
    lv_obj_t *title_panel = lv_obj_create(main_cont);
    lv_obj_set_size(title_panel, LV_PCT(100), 60);
    lv_obj_set_align(title_panel, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_panel, 0, 0);
    lv_obj_set_style_bg_color(title_panel, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(title_panel, 255, 0);
    lv_obj_set_style_border_width(title_panel, 0, 0);

    // Create title label
    lv_obj_t *title_label = lv_label_create(title_panel);
    lv_label_set_text(title_label, "Camera CSI");
    lv_obj_set_align(title_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(title_label, 255, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    // Create camera preview area
    preview_area = lv_obj_create(main_cont);
    lv_obj_set_size(preview_area, LV_PCT(90), LV_PCT(60));
    lv_obj_set_align(preview_area, LV_ALIGN_CENTER);
    lv_obj_set_y(preview_area, -30);
    lv_obj_clear_flag(preview_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(preview_area, 10, 0);
    lv_obj_set_style_bg_color(preview_area, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(preview_area, 255, 0);
    lv_obj_set_style_border_color(preview_area, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_opa(preview_area, 255, 0);
    lv_obj_set_style_border_width(preview_area, 2, 0);

    // Create preview placeholder label
    lv_obj_t *preview_label = lv_label_create(preview_area);
    lv_label_set_text(preview_label, "Camera Preview\n1920x1080@30fps");
    lv_obj_set_style_text_color(preview_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(preview_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(preview_label);

    // Create control panel
    lv_obj_t *control_panel = lv_obj_create(main_cont);
    lv_obj_set_size(control_panel, LV_PCT(100), 120);
    lv_obj_set_align(control_panel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(control_panel, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(control_panel, 200, 0);
    lv_obj_set_style_border_width(control_panel, 0, 0);
    lv_obj_set_style_radius(control_panel, 0, 0);
    lv_obj_set_style_pad_all(control_panel, 10, 0);
    lv_obj_set_flex_flow(control_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create gallery button
    gallery_btn = lv_btn_create(control_panel);
    lv_obj_set_size(gallery_btn, 60, 50);
    lv_obj_set_style_bg_color(gallery_btn, lv_color_make(64, 64, 64), 0);
    lv_obj_set_style_radius(gallery_btn, 8, 0);

    lv_obj_t *gallery_label = lv_label_create(gallery_btn);
    lv_label_set_text(gallery_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(gallery_label, lv_color_white(), 0);
    lv_obj_center(gallery_label);

    // Create capture button
    capture_btn = lv_btn_create(control_panel);
    lv_obj_set_size(capture_btn, 80, 60);
    lv_obj_set_style_bg_color(capture_btn, lv_color_make(255, 64, 64), 0);
    lv_obj_set_style_radius(capture_btn, 30, 0);
    lv_obj_set_style_border_width(capture_btn, 3, 0);
    lv_obj_set_style_border_color(capture_btn, lv_color_white(), 0);

    lv_obj_t *capture_label = lv_label_create(capture_btn);
    lv_label_set_text(capture_label, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(capture_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(capture_label, &lv_font_montserrat_18, 0);
    lv_obj_center(capture_label);

    // Create settings button
    settings_btn = lv_btn_create(control_panel);
    lv_obj_set_size(settings_btn, 60, 50);
    lv_obj_set_style_bg_color(settings_btn, lv_color_make(64, 64, 64), 0);
    lv_obj_set_style_radius(settings_btn, 8, 0);

    lv_obj_t *settings_label = lv_label_create(settings_btn);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_label, lv_color_white(), 0);
    lv_obj_center(settings_label);
}

void ui_ScreenCameraCsi_screen_init(void)
{
    ui_ScreenCameraCsi = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenCameraCsi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ScreenCameraCsi, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenCameraCsi, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Initialize camera UI in the screen
    ui_camera_init(ui_ScreenCameraCsi);

    // Set global UI objects
    ui_PanelCameraCsiTitle = lv_obj_get_child(ui_ScreenCameraCsi, 0);
    ui_LabelCameraCsiTitle = lv_obj_get_child(ui_PanelCameraCsiTitle, 0);
    ui_ImageCameraCsiPreview = lv_obj_get_child(ui_ScreenCameraCsi, 1);
    ui_PanelCameraCsiControls = lv_obj_get_child(ui_ScreenCameraCsi, 2);
    ui_ButtonCameraCsiGallery = lv_obj_get_child(ui_PanelCameraCsiControls, 0);
    ui_ButtonCameraCsiCapture = lv_obj_get_child(ui_PanelCameraCsiControls, 1);
    ui_ButtonCameraCsiSettings = lv_obj_get_child(ui_PanelCameraCsiControls, 2);
}

lv_obj_t *ui_get_preview_area(void)
{
    return preview_area;
}

lv_obj_t *ui_get_capture_btn(void)
{
    return capture_btn;
}

lv_obj_t *ui_get_gallery_btn(void)
{
    return gallery_btn;
}

lv_obj_t *ui_get_settings_btn(void)
{
    return settings_btn;
}

void ui_init(void)
{
    ui_ScreenCameraCsi_screen_init();
    lv_disp_load_scr(ui_ScreenCameraCsi);
}