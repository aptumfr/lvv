#include "test_runner.hpp"
#include "log.hpp"
#include "scripting/script_engine.hpp"

#include <chrono>
#include <filesystem>

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
        if (result.status == TestStatus::Pass) {
            LOG_INFO(log::get(), "  [PASS] {} ({:.2f}s)", result.name, result.duration_seconds);
        } else {
            LOG_ERROR(log::get(), "  [FAIL] {} ({:.2f}s)", result.name, result.duration_seconds);
            if (!result.message.empty()) {
                LOG_ERROR(log::get(), "    {}", result.message);
            }
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
