#pragma once
/*
 * console.h - Debug console with colored output
 */

#include <Windows.h>
#include <string>

namespace Console {
    void Init();
    void SetVisible(bool visible);
    void Log(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);
    void Success(const char* fmt, ...);
}
