cmake_minimum_required(VERSION 3.20)
project(lvv VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Third-party dependencies ---
find_package(Threads REQUIRED)
find_package(JPEG REQUIRED)
if(JPEG_FOUND)
    message(STATUS "JPEG library: ${JPEG_LIBRARIES}")
endif()

# PocketPy (C library)
add_library(pocketpy STATIC third_party/pocketpy/pocketpy.c)
target_include_directories(pocketpy PUBLIC third_party/pocketpy)
target_compile_definitions(pocketpy PRIVATE PK_ENABLE_OS=1 PK_ENABLE_WATCHDOG=1)

# stb implementations (compiled once)
add_library(stb STATIC third_party/stb/stb_impl.cpp)
target_include_directories(stb PUBLIC third_party/stb)

# --- Main executable ---
set(LVV_SOURCES
    src/main.cpp
    src/transport/tcp_transport.cpp
    src/transport/serial_transport.cpp
    src/protocol/protocol.cpp
    src/core/widget_tree.cpp
    src/core/screen_capture.cpp
    src/core/visual_regression.cpp
    src/core/test_runner.cpp
    src/core/process_runner.cpp
    src/scripting/script_engine.cpp
    src/scripting/lvv_module.cpp
    src/server/web_server.cpp
    src/server/api_routes.cpp
    src/server/ws_handler.cpp
    src/app/app.cpp
    src/app/config.cpp
)

add_executable(lvv ${LVV_SOURCES})

target_include_directories(lvv PRIVATE
    src
    third_party/cli11
    third_party/nlohmann
    third_party/stb
    third_party/crow
    third_party/pocketpy
    third_party
)

target_link_libraries(lvv PRIVATE
    pocketpy
    stb
    JPEG::JPEG
    Threads::Threads
)

# Crow needs these on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(lvv PRIVATE dl)
endif()

target_compile_definitions(lvv PRIVATE
    PK_IS_PUBLIC_INCLUDE
    CROW_STATIC_DIRECTORY="web/dist/"
    CROW_STATIC_ENDPOINT="/static/<path>"
)

# --- Install ---
install(TARGETS lvv RUNTIME DESTINATION bin)
install(DIRECTORY web/dist/ DESTINATION share/lvv/web
        OPTIONAL)

# --- Tests ---
option(LVV_BUILD_TESTS "Build unit tests" ON)

if(LVV_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doctest)

    add_executable(lvv_tests
        tests/test_widget_tree.cpp
        tests/test_visual_regression.cpp
        tests/test_screen_capture.cpp
        tests/test_protocol_parse.cpp
        tests/test_selectors.cpp
        tests/test_api_types.cpp
        tests/test_protocol.cpp
        src/core/widget_tree.cpp
        src/core/visual_regression.cpp
        src/core/screen_capture.cpp
        src/protocol/protocol.cpp
    )

    target_include_directories(lvv_tests PRIVATE
        src
        third_party/nlohmann
        third_party/stb
    )

    target_link_libraries(lvv_tests PRIVATE
        stb
        JPEG::JPEG
        doctest::doctest
    )

    include(CTest)
    add_test(NAME lvv_tests COMMAND lvv_tests)
endif()
