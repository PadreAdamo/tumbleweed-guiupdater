#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <array>
#include <vector>

static std::string iso8601_now_utc()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static int run_command_capture(const std::string &cmd, std::string &output)
{
    std::array<char, 4096> buffer{};
    output.clear();

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int rc = pclose(pipe);
    return rc;
}

static bool contains_any(const std::string &hay, const std::initializer_list<const char*> needles)
{
    for (const char* n : needles) {
        if (hay.find(n) != std::string::npos) return true;
    }
    return false;
}

static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string trim_copy(const std::string &s)
{
    const size_t left = s.find_first_not_of(" \t");
    if (left == std::string::npos) return "";
    const size_t right = s.find_last_not_of(" \t");
    return s.substr(left, right - left + 1);
}

static std::string build_package_preview(const std::string &output, int maxNames = 5)
{
    std::vector<std::string> names;

    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();

        std::string line = trim_copy(output.substr(start, end - start));

        if (line.rfind("v", 0) == 0) {
            std::vector<std::string> cols;
            size_t colStart = 0;
            while (colStart <= line.size()) {
                size_t pipePos = line.find('|', colStart);
                if (pipePos == std::string::npos) {
                    cols.push_back(line.substr(colStart));
                    break;
                }
                cols.push_back(line.substr(colStart, pipePos - colStart));
                colStart = pipePos + 1;
            }

            if (cols.size() >= 3) {
                std::string name = trim_copy(cols[2]);
                if (!name.empty())
                    names.push_back(name);
            }
        }

        start = end + 1;
    }

    if (names.empty())
        return "";

    std::string preview;
    const int count = static_cast<int>(names.size());
    const int shown = std::min(count, maxNames);

    for (int i = 0; i < shown; ++i) {
        if (i > 0)
            preview += ", ";
        preview += names[i];
    }

    if (count > shown)
        preview += "...";

    return preview;
}

static std::string build_package_list(const std::string &output)
{
    std::string result;

    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();

        std::string line = trim_copy(output.substr(start, end - start));

        if (line.rfind("v", 0) == 0) {
            std::vector<std::string> cols;
            size_t colStart = 0;
            while (colStart <= line.size()) {
                size_t pipePos = line.find('|', colStart);
                if (pipePos == std::string::npos) {
                    cols.push_back(line.substr(colStart));
                    break;
                }
                cols.push_back(line.substr(colStart, pipePos - colStart));
                colStart = pipePos + 1;
            }

            if (cols.size() >= 3) {
                std::string name = trim_copy(cols[2]);
                if (!name.empty()) {
                    if (!result.empty())
                        result += "\n";
                    result += name;
                }
            }
        }

        start = end + 1;
    }

    return result;
}

static int cmd_status()
{
    const std::string timestamp = iso8601_now_utc();
    const std::string zypperCmd = "zypper -n lu 2>&1";

    std::string output;
    const int rc = run_command_capture(zypperCmd, output);

    bool updatesAvailable = false;
    bool ok = true;
    bool needsAuth = false;
    int updateCount = 0;
    std::string summary;
    std::string details;
    std::string packagePreview;
    std::string packageList;

    const bool authish = contains_any(output, {
        "Root privileges are required",
        "root privileges",
        "requires root",
        "permission denied",
        "Permission denied",
        "System management is locked",
        "is locked by the application",
        "Another instance of zypper is running",
        "You must be root"
    });

    if (authish) {
        ok = false;
        needsAuth = true;
        summary = "Permission needed";
        details =
            "Unable to check for updates without administrator rights.\n"
            "This system requires elevated privileges for zypper operations.";
    } else if (rc != 0) {
        ok = false;
        summary = "Status check failed";
        details = output;
    } else if (contains_any(output, { "No updates found", "No updates needed", "No updates." })) {
        updatesAvailable = false;
        updateCount = 0;
        summary = "System up to date";
        details = "No updates listed by zypper.";
    } else {
        size_t start = 0;
        while (start < output.size()) {
            size_t end = output.find('\n', start);
            if (end == std::string::npos)
                end = output.size();

            std::string line = trim_copy(output.substr(start, end - start));
            if (line.rfind("v", 0) == 0) {
                updateCount++;
            }

            start = end + 1;
        }

        packagePreview = build_package_preview(output, 5);
        packageList = build_package_list(output);

        updatesAvailable = (updateCount > 0);
        summary = "Updates available";
        details =
            "zypper lists updates.\n"
            "Administrator privileges will be required to apply them.";
    }

    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"summary\":\"" << json_escape(summary) << "\","
        << "\"details\":\"" << json_escape(details) << "\","
        << "\"packagePreview\":\"" << json_escape(packagePreview) << "\","
        << "\"packageList\":\"" << json_escape(packageList) << "\","
        << "\"updateCount\":" << updateCount << ","
        << "\"updatesAvailable\":" << (updatesAvailable ? "true" : "false") << ","
        << "\"needsAuth\":" << (needsAuth ? "true" : "false") << ","
        << "\"timestamp\":\"" << json_escape(timestamp) << "\""
        << "}\n";

    return ok ? 0 : 1;
}

static int cmd_apply()
{
    const std::string zypperApply = "zypper -n dup 2>&1";

    std::string output;
    const int rc = run_command_capture(zypperApply, output);

    const std::string timestamp = iso8601_now_utc();
    const bool ok = (rc == 0);

    const std::string summary = ok ? "Update complete" : "Update failed";
    const std::string details = output;

    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"summary\":\"" << json_escape(summary) << "\","
        << "\"details\":\"" << json_escape(details) << "\","
        << "\"packagePreview\":\"\","
        << "\"packageList\":\"\","
        << "\"updateCount\":0,"
        << "\"updatesAvailable\":false,"
        << "\"needsAuth\":true,"
        << "\"timestamp\":\"" << json_escape(timestamp) << "\""
        << "}\n";

    return ok ? 0 : 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cout << "Usage: twu-ctl status | apply\n";
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "status") return cmd_status();
    if (cmd == "apply")  return cmd_apply();

    std::cout << "Unknown command: " << cmd << "\n";
    return 1;
}
