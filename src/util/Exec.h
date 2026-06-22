// Shell command execution utilities.
// Safe replacement for system() with output capture and shell-injection guards.

#pragma once

#include <string>
#include <vector>

namespace cosmo::util {

// Execute a shell command and capture combined stdout+stderr into a string.
// Returns the process exit code, or 0x7F on popen failure.
int Exec(const std::string& cmd, std::string& out);

// Execute a shell command and capture output lines into a vector.
// If remove_newline is true, trailing '\n' is stripped from each line.
int Exec(const std::string& cmd, std::vector<std::string>& out, bool remove_newline = false);

// Validate that a path contains no shell metacharacters (; | & $ ` () \n).
bool IsPathSafe(const std::string& path);

// Shell-escape a path by wrapping in single quotes.
std::string ShellEscape(const std::string& path);

// Validate that a hostname/IP contains only safe characters [a-zA-Z0-9.:-].
bool IsHostnameSafe(const std::string& host);

// Resolve a hostname to an IPv4 address using a background ping probe.
// Returns the resolved IP string, or empty string on failure.
// The host must pass IsHostnameSafe() validation.
std::string PingResolveIp(const std::string& host);

// Probe IP accessibility by ping and extract round-trip time.
// Returns the latency string (e.g. "1.234"), or empty string if unreachable.
// The host must pass IsHostnameSafe() validation.
std::string PingLatency(const std::string& host);

}  // namespace cosmo::util
