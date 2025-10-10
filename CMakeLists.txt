# Top-level CMakeLists for JC4880P4 Brookesia Phone
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/components
    ${CMAKE_CURRENT_LIST_DIR}/components/esp-brookesia/core/brookesia_core
    ${CMAKE_CURRENT_LIST_DIR}/components/phone_apps/calculator
    ${CMAKE_CURRENT_LIST_DIR}/components/phone_apps/settings
    ${CMAKE_CURRENT_LIST_DIR}/components/phone_apps/camera
    # Note: Not using components/esp-brookesia/apps (designed for speaker system)
    # Using custom phone-compatible apps in components/phone_apps instead
    
    # Camera support: Using override_path in main/idf_component.yml instead of EXTRA_COMPONENT_DIRS
    # to properly handle esp-video-components with OV02C10 sensor
)
project(phone_p4_JC4880P433C)
