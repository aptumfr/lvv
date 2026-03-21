#pragma once

#include "test_result.hpp"
#include <sstream>
#include <string>

namespace lvv {

inline std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

/// Generate an HTML test report with inline styling.
inline std::string generate_html_report(const TestSuiteResult& suite) {
    std::ostringstream os;

    os << R"(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<title>LVV Test Report</title>
<style>
body { font-family: -apple-system, system-ui, sans-serif; margin: 2em; background: #f8f9fa; color: #212529; }
h1 { margin-bottom: 0.3em; }
.summary { font-size: 1.1em; margin-bottom: 1.5em; color: #495057; }
.pass { color: #198754; } .fail { color: #dc3545; }
table { border-collapse: collapse; width: 100%; margin-bottom: 2em; }
th, td { padding: 8px 12px; text-align: left; border-bottom: 1px solid #dee2e6; }
th { background: #e9ecef; font-weight: 600; }
tr.failed { background: #fff5f5; }
pre { background: #212529; color: #adb5bd; padding: 12px; border-radius: 6px; overflow-x: auto; font-size: 0.85em; white-space: pre-wrap; }
.badge { display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 0.8em; font-weight: 600; }
.badge-pass { background: #d1e7dd; color: #0f5132; }
.badge-fail { background: #f8d7da; color: #842029; }
.badge-skip { background: #e2e3e5; color: #41464b; }
</style></head><body>
)";

    os << "<h1>LVV Test Report</h1>\n";
    os << "<div class=\"summary\">"
       << "<span class=\"pass\">" << suite.passed() << " passed</span>, "
       << "<span class=\"fail\">" << suite.failed() << " failed</span>, "
       << suite.tests.size() << " total — "
       << std::fixed;
    os.precision(2);
    os << suite.total_duration_seconds << "s</div>\n";

    os << "<table>\n<tr><th>Test</th><th>Status</th><th>Duration</th><th>Details</th></tr>\n";

    for (const auto& t : suite.tests) {
        const char* badge_class = "badge-pass";
        const char* status = "PASS";
        bool failed = false;
        if (t.status == TestStatus::Fail || t.status == TestStatus::Error) {
            badge_class = "badge-fail";
            status = "FAIL";
            failed = true;
        } else if (t.status == TestStatus::Skip) {
            badge_class = "badge-skip";
            status = "SKIP";
        }

        os << "<tr" << (failed ? " class=\"failed\"" : "") << ">"
           << "<td>" << html_escape(t.name) << "</td>"
           << "<td><span class=\"badge " << badge_class << "\">" << status << "</span></td>"
           << "<td>" << std::fixed;
        os.precision(2);
        os << t.duration_seconds << "s</td>"
           << "<td>";

        if (!t.message.empty() && failed) {
            os << "<pre>" << html_escape(t.message) << "</pre>";
        } else if (!t.output.empty()) {
            // Truncate long output
            auto out = t.output;
            if (out.size() > 500) out = out.substr(0, 500) + "\n... (truncated)";
            os << "<pre>" << html_escape(out) << "</pre>";
        }

        os << "</td></tr>\n";
    }

    os << "</table>\n</body></html>\n";
    return os.str();
}

} // namespace lvv
