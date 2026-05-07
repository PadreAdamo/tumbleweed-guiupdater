#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include <filesystem>

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

static bool compute_reboot_required(const std::string &packageList)
{
    return contains_any(packageList, {
        "kernel-default",
        "kernel-",
        "glibc",
        "systemd",
        "udev",
        "dracut",
        "microcode_ctl",
        "shim",
        "grub2"
    });
}

// ---- reboot detection after apply ----

static bool reboot_required_file()
{
    FILE *f = fopen("/run/reboot-required", "r");
    if (f) { fclose(f); return true; }
    return false;
}

static bool running_kernel_differs_from_installed()
{
    // uname -r gives e.g. "6.8.9-1-default"
    std::string running;
    {
        FILE *p = popen("uname -r 2>/dev/null", "r");
        if (!p) return false;
        std::array<char, 128> buf{};
        if (fgets(buf.data(), buf.size(), p))
            running = buf.data();
        pclose(p);
        while (!running.empty() &&
               (running.back() == '\n' || running.back() == '\r'))
            running.pop_back();
    }
    if (running.empty()) return false;

    // Strip the flavor suffix to get "version-release", e.g. "6.8.9-1"
    const size_t lastDash = running.rfind('-');
    if (lastDash == std::string::npos) return false;
    const std::string verRel = running.substr(0, lastDash);

    // rpm -q kernel-default returns "kernel-default-6.8.9-1.1.x86_64".
    // Search for "verRel." so "6.8.9-1." matches "6.8.9-1.1.x86_64" but not
    // a newer "6.9.0-2.1.x86_64".
    std::string installed;
    {
        FILE *p = popen("rpm -q kernel-default 2>/dev/null", "r");
        if (!p) return false;
        std::array<char, 512> buf{};
        while (fgets(buf.data(), buf.size(), p))
            installed += buf.data();
        pclose(p);
    }
    if (installed.empty() || installed.find("not installed") != std::string::npos)
        return false;

    return installed.find(verRel + ".") == std::string::npos;
}

static bool compute_apply_reboot_required(const std::string &zypperOutput)
{
    // Primary: distro marker file written by some zypper plugins
    if (reboot_required_file())
        return true;

    // Secondary: critical packages appear in the zypper dup output
    if (contains_any(zypperOutput, {
            "kernel-default", "kernel-",
            "glibc", "systemd", "dbus",
            "udev", "dracut",
        }))
        return true;

    // Tertiary: running kernel differs from the now-installed kernel package
    if (running_kernel_differs_from_installed())
        return true;

    return false;
}

// ---- Snapper integration ----

static bool read_snapper_enabled()
{
    const char *home = std::getenv("HOME");
    if (!home) return true;

    const std::string path =
        std::string(home) + "/.config/TumbleweedUpdater/controller.conf";

    FILE *f = fopen(path.c_str(), "r");
    if (!f) return true;

    char buf[256];
    bool result = true;
    while (fgets(buf, sizeof(buf), f)) {
        const std::string line = trim_copy(std::string(buf));
        if (line.rfind("SnapperEnabled=", 0) == 0) {
            result = (line.substr(15) != "false");
            break;
        }
    }
    fclose(f);
    return result;
}

static int create_pre_snapshot()
{
    if (!std::filesystem::exists("/usr/bin/snapper"))
        return -1;

    std::string output;
    const int rc = run_command_capture(
        "snapper --csvout create --type pre --print-number"
        " --description \"Tumbleweed Updater: pre-update\""
        " --cleanup-algorithm number 2>/dev/null",
        output);

    if (rc != 0) return -1;

    const std::string trimmed = trim_copy(output);
    if (trimmed.empty()) return -1;

    char *end = nullptr;
    const long n = std::strtol(trimmed.c_str(), &end, 10);
    return (end != trimmed.c_str() && n > 0) ? static_cast<int>(n) : -1;
}

static int create_post_snapshot(int pre_num)
{
    if (!std::filesystem::exists("/usr/bin/snapper"))
        return -1;

    const std::string cmd =
        "snapper --csvout create --type post --pre-number " +
        std::to_string(pre_num) +
        " --print-number"
        " --description \"Tumbleweed Updater: post-update\""
        " --cleanup-algorithm number 2>/dev/null";

    std::string output;
    const int rc = run_command_capture(cmd, output);

    if (rc != 0) return -1;

    const std::string trimmed = trim_copy(output);
    if (trimmed.empty()) return -1;

    char *end = nullptr;
    const long n = std::strtol(trimmed.c_str(), &end, 10);
    return (end != trimmed.c_str() && n > 0) ? static_cast<int>(n) : -1;
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
    bool rebootRequired = false;
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
        rebootRequired = false;
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
        rebootRequired = compute_reboot_required(packageList);

        updatesAvailable = (updateCount > 0);
        summary = "Updates available";

        if (rebootRequired) {
            details =
                "zypper lists updates.\n"
                "A reboot will likely be required after applying them.";
        } else {
            details =
                "zypper lists updates.\n"
                "Administrator privileges will be required to apply them.";
        }
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
        << "\"rebootRequired\":" << (rebootRequired ? "true" : "false") << ","
        << "\"needsAuth\":" << (needsAuth ? "true" : "false") << ","
        << "\"timestamp\":\"" << json_escape(timestamp) << "\""
        << "}\n";

    return ok ? 0 : 1;
}

static int cmd_apply()
{
    const std::string timestamp = iso8601_now_utc();
    const bool snapperEnabled = read_snapper_enabled();

    int snapshotPre  = -1;
    int snapshotPost = -1;
    bool snapperUsed = false;

    if (snapperEnabled) {
        snapshotPre = create_pre_snapshot();
        if (snapshotPre < 0)
            fprintf(stderr, "[snapper] pre-snapshot failed or snapper unavailable — continuing without snapshot\n");
    }

    const std::string zypperCmd = "zypper -n dup 2>&1";

    std::string output;
    bool ok = false;

    FILE *pipe = popen(zypperCmd.c_str(), "r");
    if (!pipe) {
        // Still attempt post-snapshot so the pair is balanced if pre succeeded
        if (snapperEnabled && snapshotPre >= 0) {
            snapshotPost = create_post_snapshot(snapshotPre);
            snapperUsed  = (snapshotPost >= 0);
        }
        std::cout
            << "{"
            << "\"ok\":false,"
            << "\"summary\":\"Failed to start zypper\","
            << "\"details\":\"popen failed\","
            << "\"packagePreview\":\"\","
            << "\"packageList\":\"\","
            << "\"updateCount\":0,"
            << "\"updatesAvailable\":false,"
            << "\"rebootRequired\":false,"
            << "\"needsAuth\":false,"
            << "\"snapshotPre\":" << snapshotPre << ","
            << "\"snapshotPost\":" << snapshotPost << ","
            << "\"snapperUsed\":" << (snapperUsed ? "true" : "false") << ","
            << "\"timestamp\":\"" << json_escape(timestamp) << "\""
            << "}\n";
        std::cout.flush();
        return 1;
    }

    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        const std::string chunk = buf.data();
        output += chunk;
        std::cout << chunk;
        std::cout.flush();
    }

    const int rc = pclose(pipe);
    ok = (rc == 0);

    if (snapperEnabled && snapshotPre >= 0) {
        snapshotPost = create_post_snapshot(snapshotPre);
        if (snapshotPost < 0)
            fprintf(stderr, "[snapper] post-snapshot failed\n");
        else
            snapperUsed = true;
    }

    const bool needsAuth = !ok && contains_any(output, {
        "Root privileges are required",
        "root privileges",
        "requires root",
        "Permission denied",
        "permission denied",
        "You must be root"
    });

    const bool rebootRequired = ok && compute_apply_reboot_required(output);

    const std::string summary = ok          ? "Update complete"
                               : needsAuth  ? "Permission denied"
                                            : "Update failed";

    // Ensure the JSON result starts on its own line
    if (!output.empty() && output.back() != '\n')
        std::cout << '\n';

    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"summary\":\"" << json_escape(summary) << "\","
        << "\"details\":\"\","
        << "\"packagePreview\":\"\","
        << "\"packageList\":\"\","
        << "\"updateCount\":0,"
        << "\"updatesAvailable\":false,"
        << "\"rebootRequired\":" << (rebootRequired ? "true" : "false") << ","
        << "\"needsAuth\":" << (needsAuth ? "true" : "false") << ","
        << "\"snapshotPre\":" << snapshotPre << ","
        << "\"snapshotPost\":" << snapshotPost << ","
        << "\"snapperUsed\":" << (snapperUsed ? "true" : "false") << ","
        << "\"timestamp\":\"" << json_escape(timestamp) << "\""
        << "}\n";
    std::cout.flush();

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
