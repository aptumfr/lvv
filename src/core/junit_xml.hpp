#pragma once

#include "test_result.hpp"
#include <sstream>
#include <string>

namespace lvv {

inline std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

inline std::string generate_junit_xml(const TestSuiteResult& suite) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<testsuites>\n";
    os << "  <testsuite name=\"" << xml_escape(suite.name) << "\""
       << " tests=\"" << suite.tests.size() << "\""
       << " failures=\"" << suite.failed() << "\""
       << " time=\"" << suite.total_duration_seconds << "\">\n";

    for (const auto& t : suite.tests) {
        os << "    <testcase name=\"" << xml_escape(t.name) << "\""
           << " time=\"" << t.duration_seconds << "\"";

        if (t.status == TestStatus::Pass && t.output.empty()) {
            os << "/>\n";
        } else {
            os << ">\n";
            if (t.status == TestStatus::Fail) {
                os << "      <failure message=\"" << xml_escape(t.message)
                   << "\"/>\n";
            } else if (t.status == TestStatus::Error) {
                os << "      <error message=\"" << xml_escape(t.message)
                   << "\"/>\n";
            } else if (t.status == TestStatus::Skip) {
                os << "      <skipped/>\n";
            }
            if (!t.output.empty()) {
                os << "      <system-out>" << xml_escape(t.output)
                   << "</system-out>\n";
            }
            os << "    </testcase>\n";
        }
    }

    os << "  </testsuite>\n";
    os << "</testsuites>\n";
    return os.str();
}

} // namespace lvv
