#ifndef CLIJUDGE_PLATFORM_H
#define CLIJUDGE_PLATFORM_H

// platform.h
// 跨平台基础工具（Windows / Linux）

#include <string>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace clijudge {
namespace platform {

namespace fs = std::filesystem;

// 临时目录（不带尾部分隔符）
inline std::string tempDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) return ".";
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
#else
    const char* t = getenv("TMPDIR");
    if (t && t[0]) return t;
    return "/tmp";
#endif
}

// 可执行文件所在目录
inline std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetModuleFileNameA(NULL, buf, MAX_PATH)) {
        return fs::path(buf).parent_path().string();
    }
    return ".";
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::path(buf).parent_path().string();
    }
    return ".";
#endif
}

// 当前进程 ID
inline uint64_t pid() {
#ifdef _WIN32
    return (uint64_t)GetCurrentProcessId();
#else
    return (uint64_t)getpid();
#endif
}

// 单调时钟毫秒数
inline uint64_t tickMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// 本地时间（可移植的 localtime）
inline void localTime(const time_t* t, struct tm* out) {
#ifdef _WIN32
    localtime_s(out, t);
#else
    localtime_r(t, out);
#endif
}

// 路径拼接（使用当前平台分隔符）
inline std::string pathJoin(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

// 数据目录（环境变量 CLIJUDGE_DATA_DIR/JUDGELITE_DATA_DIR 优先，其次 exe 目录/data）
// lang.h、settings.h、main.cpp 统一使用此函数
inline std::string dataDir() {
    const char* env = std::getenv("CLIJUDGE_DATA_DIR");
    if (!env || !env[0]) {
        env = std::getenv("JUDGELITE_DATA_DIR"); // 兼容旧变量名
    }
    if (env && env[0]) {
        return std::string(env);
    }
    std::string exe = exeDir();
    if (!exe.empty() && exe != ".") {
        return pathJoin(exe, "data");
    }
    return pathJoin(".", "data");
}

// 可执行文件扩展名
inline const char* exeSuffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

// 编译静态链接选项
inline const char* staticLinkFlag() {
#ifdef _WIN32
    return " -static";
#else
    return "";
#endif
}

} // namespace platform
} // namespace clijudge

#endif // CLIJUDGE_PLATFORM_H
