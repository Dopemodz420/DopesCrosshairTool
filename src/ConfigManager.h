#pragma once
#include "Crosshair.h"
#include <string>

namespace dopes {

class ConfigManager {
public:
    static bool Load(AppConfig& out, const std::wstring& path = L"");
    static bool Save(const AppConfig& cfg, const std::wstring& path = L"");
    static std::wstring DefaultPath();
    static std::wstring AppDataPath();
};

} // namespace dopes
