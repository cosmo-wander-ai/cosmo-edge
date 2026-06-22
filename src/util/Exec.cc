// Shell command execution utilities.

#include "util/Exec.h"

#include <cstring>

#include "fmt/format.h"

namespace cosmo::util {

constexpr int kExecFailureCode = 0x7F;

bool IsPathSafe(const std::string& path) {
    const std::string kBadChars = ";|&$`()\n";
    return path.find_first_of(kBadChars) == std::string::npos;
}

std::string ShellEscape(const std::string& path) {
    std::string escaped;
    escaped.reserve(path.size() + 2);
    escaped.push_back('\'');
    for (char c : path) {
        if (c == '\'') {
            escaped.append("'\\''");
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

int Exec(const std::string& cmd, std::string& out) {
    FILE* file = popen(fmt::format("{} 2>&1", cmd).c_str(), "r");
    if (!file) {
        return kExecFailureCode;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), file)) {
        out.append(buf);
    }
    int ret = pclose(file);
    if (WIFEXITED(ret)) {
        return WEXITSTATUS(ret);
    }
    return kExecFailureCode;
}

int Exec(const std::string& cmd, std::vector<std::string>& out, bool remove_newline) {
    FILE* file = popen(fmt::format("{} 2>&1", cmd).c_str(), "r");
    if (!file) {
        return kExecFailureCode;
    }
    char buf[1024]{};
    while (fgets(buf, sizeof(buf), file)) {
        size_t len = std::strlen(buf);
        if (remove_newline && len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        out.emplace_back(buf);
    }
    int ret = pclose(file);
    if (WIFEXITED(ret)) {
        return WEXITSTATUS(ret);
    }
    return kExecFailureCode;
}

bool IsHostnameSafe(const std::string& host) {
    if (host.empty() || host.size() > 253) {
        return false;
    }
    for (char c : host) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':') {
            return false;
        }
    }
    return true;
}

std::string PingResolveIp(const std::string& host) {
    if (host.empty() || !IsHostnameSafe(host)) {
        return "";
    }
    // Already an IP address — no resolution needed
    if (host.find_first_not_of("0123456789.") == std::string::npos) {
        return host;
    }
    auto escaped = ShellEscape(host);
    // Ping once with 1s timeout, extract IP from first response line.
    // Pipeline: ping → kill stale process → head first line → awk column 3 → grep IPv4 pattern
    const std::string cmd = "(ping " + escaped +
                            R"( -w 1 -c 1 & 2>/dev/null;)"
                            R"(epid=$!;proc=`ps -ef | grep $epid | grep -v "grep")"
                            R"(| grep -v "\[.*\]"`;)"
                            R"(if [[ "$epid" && "$proc" ]];then sleep 10;kill -9 $epid 2>/dev/null;fi;))"
                            R"(|  head -n 1 | awk '{print $3}' | grep -oE "([0-9]{1,3}[.]){3}[0-9]{1,3}")";

    std::vector<std::string> result;
    int ret = Exec(cmd, result, true);
    if (ret != 0) {
        return "";
    }
    return result.empty() ? "" : result[0];
}

std::string PingLatency(const std::string& host) {
    if (host.empty() || !IsHostnameSafe(host)) {
        return "";
    }
    auto escaped = ShellEscape(host);
    // Ping once with 1s timeout, extract round-trip time from response.
    // Pipeline: ping → kill stale process → head 4 lines → strip spaces → grep time=N.N → awk extract value
    const std::string cmd =
        "(ping " + escaped +
        R"( -w 1 -c 1 & 2>/dev/null;)"
        R"(epid=$!;proc=`ps -ef | grep $epid | grep -v "grep")"
        R"( | grep -v "\[.*\]"`;)"
        R"(if [[ "$epid" && "$proc" ]];then sleep 10;kill -9 $epid 2>/dev/null;fi;))"
        R"( |  head -n 4 | sed s/[[:space:]]//g | grep -oE "time=[0-9]{1,3}.[0-9]{1,3}" | awk -F= '{print $2}')";

    std::string result;
    int ret = Exec(cmd, result);
    if (ret != 0) {
        return "";
    }
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

}  // namespace cosmo::util
