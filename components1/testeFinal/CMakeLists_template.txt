file(GLOB_RECURSE UI_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

file(GLOB_RECURSE UI_HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
)

# Coleta automaticamente todos os arquivos de fonte na pasta fonts/
file(GLOB_RECURSE UI_FONTS
    "${CMAKE_CURRENT_LIST_DIR}/fonts/*.bin"
)

set(UI_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}"
)

foreach(header ${UI_HEADERS})
    get_filename_component(header_dir "${header}" DIRECTORY)
    list(APPEND UI_INCLUDE_DIRS "${header_dir}")
endforeach()

list(REMOVE_DUPLICATES UI_INCLUDE_DIRS)

idf_component_register(
    SRCS ${UI_SOURCES}
    INCLUDE_DIRS ${UI_INCLUDE_DIRS}
    REQUIRES lvgl
    EMBED_FILES ${UI_FONTS}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)