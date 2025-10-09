idf_component_register(
    SRCS "Settings.cpp" "assets/img_app_setting.c"
    INCLUDE_DIRS "." "assets"
    REQUIRES brookesia_core nvs_flash
)
