idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp32_p4_jc4880p433c_bsp esp_lvgl_port nvs_flash spiffs 
                                     brookesia_core calculator settings camera
                                     esp_cam_sensor esp_video esp_ipa esp_sccb_intf)
