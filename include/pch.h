#pragma once

// ============================================================================
// Precompiled Header - zlagent
// ============================================================================

// Windows-specific guards (prevent min/max macro conflicts, reduce bloat)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define popen _popen
#define pclose _pclose
#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CONNECT_TIMEOUT 5
#define READ_TIMEOUT 600
#define WRITE_TIMEOUT 60

// Core standard library headers used across the project
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cctype>
#include <clocale>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <iomanip>

#ifdef _WIN32
#include <conio.h>
#endif

#include <remote_log.h>
#define JSON_THROW_USER
#include <json.hpp>
#include <logger.h>
