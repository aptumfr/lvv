#pragma once

#include "test_result.hpp"
#include <string>
#include <vector>

namespace lvv {

class ScriptEngine; // forward declare

class TestRunner {
public:
    explicit TestRunner(ScriptEngine& engine);

    /// Run a single test file
    TestResult run_file(const std::string& path);

    /// Run all test files, return suite result
    TestSuiteResult run_suite(const std::string& name,
                              const std::vector<std::string>& files);

    void set_timeout(double seconds) { timeout_seconds_ = seconds; }
    void set_verbose(bool v) { verbose_ = v; }
    void set_fail_fast(bool v) { fail_fast_ = v; }
    void set_setup_script(const std::string& path) { setup_script_ = path; }

private:
    ScriptEngine& engine_;
    double timeout_seconds_ = 30.0;
    bool verbose_ = false;
    bool fail_fast_ = false;
    std::string setup_script_;
};

} // namespace lvv
