#include "test_runner.hpp"
#include "scripting/script_engine.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace lvv {

TestRunner::TestRunner(ScriptEngine& engine)
    : engine_(engine) {}

TestResult TestRunner::run_file(const std::string& path) {
    TestResult result;
    result.name = std::filesystem::path(path).stem().string();

    engine_.set_timeout(timeout_seconds_);

    auto start = std::chrono::steady_clock::now();

    auto [success, output] = engine_.run_file(path);

    auto end = std::chrono::steady_clock::now();
    result.duration_seconds = std::chrono::duration<double>(end - start).count();
    result.output = output;

    if (success) {
        result.status = TestStatus::Pass;
    } else {
        result.status = TestStatus::Fail;
        result.message = output;
    }

    if (verbose_) {
        const char* status_str = (result.status == TestStatus::Pass) ? "PASS" : "FAIL";
        std::cout << "  [" << status_str << "] " << result.name
                  << " (" << result.duration_seconds << "s)\n";
        if (result.status != TestStatus::Pass && !result.message.empty()) {
            std::cout << "    " << result.message << "\n";
        }
    }

    return result;
}

TestSuiteResult TestRunner::run_suite(const std::string& name,
                                      const std::vector<std::string>& files) {
    TestSuiteResult suite;
    suite.name = name;

    auto start = std::chrono::steady_clock::now();

    for (const auto& file : files) {
        suite.tests.push_back(run_file(file));
    }

    auto end = std::chrono::steady_clock::now();
    suite.total_duration_seconds = std::chrono::duration<double>(end - start).count();

    return suite;
}

} // namespace lvv
