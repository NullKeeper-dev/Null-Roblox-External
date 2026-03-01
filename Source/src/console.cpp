/*
 * console.cpp - Debug console with colored timestamped output
 */

#include "../include/console.h"
#include "../include/esp.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>

static HANDLE g_hConsole = NULL;

static void PrintTimestamp() {
    time_t now = time(nullptr);
    tm t;
    localtime_s(&t, &now);
    printf("[%02d:%02d:%02d] ", t.tm_hour, t.tm_min, t.tm_sec);
}

void Console::Init() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hConsole, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    mode |= ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hConsole, mode);

    SetConsoleTitleA("Null External - Debug Console");

    // Initial visibility based on settings
    HWND hWnd = GetConsoleWindow();
    if (hWnd) {
        ShowWindow(hWnd, ESP::Settings().debugMode ? SW_SHOW : SW_HIDE);
    }

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    printf("========================================\n");
    printf("       Null External Debug Console    \n");
    printf("========================================\n\n");
}

void Console::Log(const char* fmt, ...) {
    SetConsoleTextAttribute(g_hConsole, 7); // White
    PrintTimestamp();
    printf("[LOG] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    SetConsoleTextAttribute(g_hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void Console::Warn(const char* fmt, ...) {
    SetConsoleTextAttribute(g_hConsole, 14); // Yellow
    PrintTimestamp();
    printf("[*] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    SetConsoleTextAttribute(g_hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void Console::Error(const char* fmt, ...) {
    SetConsoleTextAttribute(g_hConsole, 12); // Red
    PrintTimestamp();
    printf("[!] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    SetConsoleTextAttribute(g_hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void Console::Success(const char* fmt, ...) {
    SetConsoleTextAttribute(g_hConsole, 10); // Green
    PrintTimestamp();
    printf("[+] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    SetConsoleTextAttribute(g_hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void Console::SetVisible(bool visible) {
    HWND hWnd = GetConsoleWindow();
    if (hWnd) {
        ShowWindow(hWnd, visible ? SW_SHOW : SW_HIDE);
    }
}
