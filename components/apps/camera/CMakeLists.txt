idf_component_register(SRCS "src/camera.c"
                              "src/camera_dma.c"
                              "src/camera_ppa.c"
                              "src/camera_isp.c"
                              "src/CameraApp.cpp"
                              "ui/ui.c"
                              "assets/camera_app_icon.c"
                       INCLUDE_DIRS "include" "ui" "assets"
                       PRIV_REQUIRES brookesia_core lvgl freertos esp_cam_sensor esp_driver_cam esp_driver_isp esp_driver_ppa esp32_p4_jc4880p433c_bsp driver esp_timer heap esp_lvgl_port)
