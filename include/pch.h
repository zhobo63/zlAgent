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
#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT

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
#include <conio.h>
#include <clocale>
#include <thread>

#include <remote_log.h>
#define JSON_THROW_USER
#include <json.hpp>
