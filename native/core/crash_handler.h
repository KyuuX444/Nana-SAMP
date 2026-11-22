#pragma once

#include <string>

namespace nana::core {

class CrashHandler {
public:
    static void Install(const std::string& logDirectory);
    static void SetGameState(int gameState);
    static void RecordFileOpen(const char* path, bool ok);
};

} // namespace nana::core
