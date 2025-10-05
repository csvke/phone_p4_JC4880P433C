# Top-level CMakeLists for JC4880P4 Brookesia Phone
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/components
    ${CMAKE_CURRENT_LIST_DIR}/../esp32_p4_jc4880p433c_bsp
)
project(phone_p4_JC4880P433C)
