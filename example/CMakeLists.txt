cmake_minimum_required(VERSION 3.20)
project(lvv_example LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths — configurable via cmake -D, env var, or default to sibling dirs
set(LV_CPP_DIR "$ENV{LV_CPP_DIR}" CACHE PATH "Path to lv:: C++ wrapper repo")
set(LVGL_DIR "$ENV{LVGL_DIR}" CACHE PATH "Path to LVGL repo")
set(SPY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../spy" CACHE PATH "Path to LVV spy sources")

if(LV_CPP_DIR STREQUAL "")
    set(LV_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lv")
endif()
if(LVGL_DIR STREQUAL "")
    set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lvgl")
endif()

# Build LVGL (it finds lv_conf.h in its parent dir — the lv project has it)
# We need lv_conf.h to be visible. Set the include path explicitly.
if(NOT TARGET lvgl)
    add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)
endif()

# Make lv_conf.h visible (it's in the lv project root)
target_include_directories(lvgl PUBLIC ${LV_CPP_DIR})

# lv:: C++ wrapper (header-only)
add_library(lv_cpp INTERFACE)
target_include_directories(lv_cpp INTERFACE ${LV_CPP_DIR}/include)
target_link_libraries(lv_cpp INTERFACE lvgl)
target_compile_features(lv_cpp INTERFACE cxx_std_20)

# Spy library
add_library(lvv_spy STATIC ${SPY_DIR}/lvv_spy.c)
target_include_directories(lvv_spy PUBLIC ${SPY_DIR})
target_link_libraries(lvv_spy PUBLIC lvgl)

# Headless display driver
add_library(lvv_headless STATIC ${SPY_DIR}/lvv_headless.c)
target_include_directories(lvv_headless PUBLIC ${SPY_DIR})
target_link_libraries(lvv_headless PUBLIC lvgl lvv_spy)

# Counter with spy
add_executable(counter_with_spy counter_with_spy.cpp)
target_link_libraries(counter_with_spy PRIVATE lv_cpp lvv_spy)

# Demo with spy (rich UI for test automation)
add_executable(demo_with_spy demo_with_spy.cpp)
target_link_libraries(demo_with_spy PRIVATE lv_cpp lvv_spy)

# Demo headless (for CI - no X11/SDL needed)
add_executable(demo_headless demo_with_spy.cpp)
target_link_libraries(demo_headless PRIVATE lv_cpp lvv_spy lvv_headless)
target_compile_definitions(demo_headless PRIVATE LVV_HEADLESS)

# Find X11
find_package(X11 QUIET)
if(X11_FOUND)
    target_link_libraries(lvgl PUBLIC ${X11_LIBRARIES})
    target_include_directories(lvgl PUBLIC ${X11_INCLUDE_DIR})
    message(STATUS "X11 found - using native display")
endif()

# Find SDL2
find_package(SDL2 QUIET)
if(SDL2_FOUND OR TARGET SDL2::SDL2)
    if(TARGET SDL2::SDL2)
        target_link_libraries(lvgl PUBLIC SDL2::SDL2)
    else()
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
    message(STATUS "SDL2 found")
endif()
