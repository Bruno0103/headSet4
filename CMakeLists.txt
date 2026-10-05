# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(LV_BUILD_CONF_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/main/lv_conf.h"
    CACHE PATH "LVGL configuration file"
    FORCE
)

# Garante que o LVGL encontre o lv_conf.h diretamente pelo caminho absoluto
add_compile_definitions(LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/main/lv_conf.h")
add_compile_options(-I"${CMAKE_CURRENT_SOURCE_DIR}/main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(headSet4)
