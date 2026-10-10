# Inclui as fontes geradas e especificadas pelo LVGL Editor
include(${CMAKE_CURRENT_LIST_DIR}/file_list_gen.cmake)

# Fontes C do componente UI
set(UI_SOURCES ${LV_EDITOR_PROJECT_SOURCES})

# Coleta fontes adicionais se houverem
file(GLOB_RECURSE UI_FONTS
    "${CMAKE_CURRENT_LIST_DIR}/fonts/*.bin"
)

# Diretórios de include do componente UI
set(UI_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/screens"
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

# Adiciona subdiretórios de componentes locais em components/ui/components
file(GLOB SUB_COMP_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/*")
foreach(dir ${SUB_COMP_DIRS})
    if(IS_DIRECTORY "${dir}")
        list(APPEND UI_INCLUDE_DIRS "${dir}")
        # Subdiretórios base/ e derived/
        file(GLOB NESTED_DIRS "${dir}/*")
        foreach(nested_dir ${NESTED_DIRS})
            if(IS_DIRECTORY "${nested_dir}")
                list(APPEND UI_INCLUDE_DIRS "${nested_dir}")
            endif()
        endforeach()
    endif()
endforeach()

list(REMOVE_DUPLICATES UI_INCLUDE_DIRS)

idf_component_register(
    SRCS ${UI_SOURCES}
    INCLUDE_DIRS ${UI_INCLUDE_DIRS}
    REQUIRES lvgl
    EMBED_FILES ${UI_FONTS}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)