#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace lvv {

enum class TestStatus {
    Pass,
    Fail,
    Error,
    Skip
};

struct TestResult {
    std::string name;
    TestStatus status = TestStatus::Pass;
    double duration_seconds = 0.0;
    std::string message;
    std::string output;
};

struct TestSuiteResult {
    std::string name;
    std::vector<TestResult> tests;
    double total_duration_seconds = 0.0;

    int passed() const {
        int n = 0;
        for (const auto& t : tests) if (t.status == TestStatus::Pass) n++;
        return n;
    }
    int failed() const {
        int n = 0;
        for (const auto& t : tests)
            if (t.status == TestStatus::Fail || t.status == TestStatus::Error) n++;
        return n;
    }
    bool all_passed() const { return failed() == 0; }
};

} // namespace lvv
