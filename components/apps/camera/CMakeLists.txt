idf_component_register(SRCS "src/CameraCsi.cpp"
                              "src/camera_preview.c"
                              "ui/ui.c"
                              "assets/img_app_camera.c"
                       INCLUDE_DIRS "include" "ui"
                       PRIV_REQUIRES brookesia_core lvgl freertos esp_cam_sensor esp_video esp_ipa esp_sccb_intf esp32_p4_jc4880p433c_bsp driver esp_timer heap esp_lvgl_port)
