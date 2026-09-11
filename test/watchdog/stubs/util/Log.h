#pragma once

// Keep the standalone driver test independent of the application's log backend.
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERRO(...) ((void)0)
