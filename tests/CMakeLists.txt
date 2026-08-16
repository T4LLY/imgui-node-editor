cmake_minimum_required(VERSION 3.12)

project(imgui_node_editor_compatibility_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(IMGUI_NODE_EDITOR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(
    IMGUI_NODE_EDITOR_IMGUI_DIR
    "${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui"
    CACHE PATH
    "Dear ImGui source directory used by compatibility tests"
)

foreach(_required_file imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp imgui.h imgui_internal.h)
    if (NOT EXISTS "${IMGUI_NODE_EDITOR_IMGUI_DIR}/${_required_file}")
        message(FATAL_ERROR "IMGUI_NODE_EDITOR_IMGUI_DIR is not a Dear ImGui source tree: missing ${_required_file}")
    endif()
endforeach()

add_library(imgui_compat STATIC
    "${IMGUI_NODE_EDITOR_IMGUI_DIR}/imgui.cpp"
    "${IMGUI_NODE_EDITOR_IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_NODE_EDITOR_IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_NODE_EDITOR_IMGUI_DIR}/imgui_tables.cpp"
)
target_include_directories(imgui_compat PUBLIC "${IMGUI_NODE_EDITOR_IMGUI_DIR}")
target_compile_definitions(imgui_compat PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

add_library(imgui_node_editor_compat STATIC
    "${IMGUI_NODE_EDITOR_ROOT_DIR}/crude_json.cpp"
    "${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.cpp"
    "${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor.cpp"
    "${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor_api.cpp"
)
target_include_directories(imgui_node_editor_compat PUBLIC "${IMGUI_NODE_EDITOR_ROOT_DIR}")
target_link_libraries(imgui_node_editor_compat PUBLIC imgui_compat)

add_executable(imgui_node_editor_compatibility_tests compatibility_tests.cpp)
target_link_libraries(imgui_node_editor_compatibility_tests PRIVATE imgui_node_editor_compat)

if (MSVC)
    target_compile_options(imgui_node_editor_compatibility_tests PRIVATE /W4 /WX)
else()
    target_compile_options(imgui_node_editor_compatibility_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME imgui_node_editor_compatibility COMMAND imgui_node_editor_compatibility_tests)
