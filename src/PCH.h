#pragma once

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#pragma warning(push)
#include <RE/Fallout.h>
#include <F4SE/F4SE.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

using namespace std::literals;

namespace logger = spdlog;

#define DLLEXPORT __declspec(dllexport)
