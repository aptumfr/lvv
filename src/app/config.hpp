#pragma once

#include <string>
#include <vector>

namespace lvv {

/// Resolve the default static directory relative to the executable
std::string default_static_dir();

/// Find the directory containing the shipped lvv.py module
std::string find_lvv_python_dir();

/// Find a free TCP port on localhost
int find_free_port();

struct AppConfig {
    // Target connection
    std::string target_host = "localhost";
    int target_port = 5555;

    // Serial connection
    std::string serial_device;
    int serial_baud = 115200;

    // Web server
    int web_port = 8080;
    std::string static_dir;  // set by default_static_dir() in main()

    // Test execution
    std::vector<std::string> test_files;
    std::string junit_output;
    std::string ref_images_dir = "ref_images";
    double diff_threshold = 0.1;
    double timeout = 30.0;
    bool verbose = false;
    bool use_system_python = false;
    std::string python_exe;  // empty = auto-detect
    std::string setup_script;  // run before each test (for test isolation)
};

} // namespace lvv
