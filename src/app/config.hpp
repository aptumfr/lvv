#pragma once

#include <string>
#include <vector>

namespace lvv {

/// Resolve the default static directory relative to the executable
std::string default_static_dir();

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
};

} // namespace lvv
