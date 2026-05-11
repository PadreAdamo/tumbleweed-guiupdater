#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

static int run_command_capture(const std::string &cmd, std::string &output)
{
    std::array<char, 4096> buffer{};
    output.clear();
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();
    return pclose(pipe);
}

static std::string trim(const std::string &s)
{
    const size_t left = s.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) return "";
    const size_t right = s.find_last_not_of(" \t\r\n");
    return s.substr(left, right - left + 1);
}

static std::string get_home()
{
    const char *home = std::getenv("HOME");
    if (home && home[0]) return std::string(home);
    const struct passwd *pw = getpwuid(getuid());
    return pw ? std::string(pw->pw_dir) : std::string{};
}

static bool json_bool(const std::string &json, const std::string &key)
{
    const std::string search = "\"" + key + "\":";
    const size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    const size_t v = pos + search.size();
    return json.size() >= v + 4 && json.substr(v, 4) == "true";
}

static int json_int(const std::string &json, const std::string &key)
{
    const std::string search = "\"" + key + "\":";
    const size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    size_t i = pos + search.size();
    if (i >= json.size()) return 0;
    int result = 0;
    for (; i < json.size() && (json[i] >= '0' && json[i] <= '9'); ++i)
        result = result * 10 + (json[i] - '0');
    return result;
}

static std::string data_dir()
{
    const std::string home = get_home();
    return home.empty() ? std::string{} : home + "/.local/share/TumbleweedUpdater";
}

static int read_last_notified_count()
{
    const std::string dir = data_dir();
    if (dir.empty()) return -1;
    std::ifstream f(dir + "/last-notified-count");
    if (!f) return -1;
    int n = -1;
    f >> n;
    return n;
}

static void write_last_notified_count(int count)
{
    const std::string dir = data_dir();
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir + "/last-notified-count");
    if (f) f << count << "\n";
}

int main()
{
    // Run the unprivileged status check
    std::string statusOut;
    run_command_capture("/usr/bin/twu-ctl status 2>/dev/null", statusOut);

    const std::string json = trim(statusOut);
    if (json.empty() || json.front() != '{')
        return 0;

    const bool updatesAvailable = json_bool(json, "updatesAvailable");
    const int  totalCount       = json_int(json, "updateCount");
    const int  flatpakCount     = json_int(json, "flatpakUpdateCount");

    if (!updatesAvailable) {
        write_last_notified_count(0);
        return 0;
    }

    // Skip if the GUI tray is running — it handles its own notifications
    std::string pgrepOut;
    run_command_capture("pgrep -x tumbleweed-updater 2>/dev/null", pgrepOut);
    if (!trim(pgrepOut).empty())
        return 0;

    // Skip if we already notified for this exact count
    if (totalCount == read_last_notified_count())
        return 0;

    // Build message
    const int sysCount = totalCount - flatpakCount;
    std::string msg;
    if (sysCount > 0 && flatpakCount > 0)
        msg = std::to_string(sysCount) + " system + "
            + std::to_string(flatpakCount) + " Flatpak updates available";
    else if (flatpakCount > 0)
        msg = std::to_string(flatpakCount) + " Flatpak updates available";
    else
        msg = std::to_string(totalCount) + " system updates available";

    std::string notifOut;
    run_command_capture(
        "notify-send"
        " -i tumbleweed-updater"
        " -a 'Tumbleweed Updater'"
        " 'Updates Available'"
        " '" + msg + "' 2>/dev/null",
        notifOut);

    write_last_notified_count(totalCount);
    return 0;
}
