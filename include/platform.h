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
#include <fstream>

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
#include <fcntl.h>
#include <sys/file.h>
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

// 数据目录（环境变量 CLIJUDGE_DATA_DIR 优先，其次 exe 目录/data）
// lang.h、settings.h、main.cpp 统一使用此函数
inline std::string dataDir() {
    const char* env = std::getenv("CLIJUDGE_DATA_DIR");
    if (env && env[0]) {
        return std::string(env);
    }
    std::string exe = exeDir();
    if (!exe.empty() && exe != ".") {
        return pathJoin(exe, "data");
    }
    return pathJoin(".", "data");
}

// 旧版平铺布局迁移（幂等）：data/*.json → data/<类别>/*.json
// problems.json/problem_*.json → problems/；articles.json/article_*.json → articles/；
// contests.json/contest_*.json → contests/；submissions.json → submissions/
// 每次启动执行一次：目标已存在则跳过，失败保留原位下次重试
inline void migrateFlatLayout(const std::string& dataDir) {
    std::error_code ec;
    if (!fs::is_directory(dataDir, ec)) return;
    auto startsWith = [](const std::string& s, const std::string& pre) {
        return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
    };
    auto endsWith = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };
    auto targetSubdir = [&](const std::string& name) -> std::string {
        if (name == "problems.json") return "problems";
        if (name == "articles.json") return "articles";
        if (name == "contests.json") return "contests";
        if (name == "submissions.json") return "submissions";
        if (!endsWith(name, ".json")) return "";
        if (startsWith(name, "problem_")) return "problems";
        if (startsWith(name, "article_")) return "articles";
        if (startsWith(name, "contest_")) return "contests";
        return "";
    };
    for (fs::directory_iterator it(dataDir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().string();
        std::string sub = targetSubdir(name);
        if (sub.empty()) continue;
        fs::path destDir = fs::path(dataDir) / sub;
        fs::path dest = destDir / name;
        if (fs::exists(dest, ec2)) continue;
        fs::create_directories(destDir, ec2);
        fs::rename(it->path(), dest, ec2);
    }
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

// 原子写文件：先写同目录临时文件，再 rename 覆盖。
// 防止进程崩溃 / 并发读取时看到半截（截断）文件——索引类 JSON 的关键写路径。
inline bool writeFileAtomic(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp." + std::to_string(pid());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(content.data(), (std::streamsize)content.size());
        f.flush();
        if (!f.good()) {
            f.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);  // 语义：替换已存在的目标（POSIX rename / MOVEFILE_REPLACE_EXISTING）
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
    return true;
}

// ── 跨进程数据锁（修改类命令持有） ─────────────────────────
// 整个数据目录一把排他文件锁：修改类命令开始时获取，进程结束时由 OS 释放
// （句柄/fd 关闭即解锁，崩溃不会留下陈旧锁）。读命令不加锁——索引写入是
// 原子替换（writeFileAtomic），任何读取者看到的都是完整快照。
class DataLock {
public:
    DataLock() = default;
    ~DataLock() { unlock(); }
    DataLock(const DataLock&) = delete;
    DataLock& operator=(const DataLock&) = delete;

    // 阻塞获取：另一 clijudge 修改进程持锁时等待，对方崩溃/退出则立即获得
    bool lock(const std::string& path) {
        if (locked_) return true;
#ifdef _WIN32
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        OVERLAPPED ov = {};
        if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ov)) {
            CloseHandle(h);
            return false;
        }
        handle_ = h;
#else
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0) return false;
        if (flock(fd, LOCK_EX) != 0) {
            ::close(fd);
            return false;
        }
        fd_ = fd;
#endif
        locked_ = true;
        return true;
    }

    void unlock() {
        if (!locked_) return;
#ifdef _WIN32
        OVERLAPPED ov = {};
        UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &ov);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
#endif
        locked_ = false;
    }

private:
    bool locked_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

} // namespace platform
} // namespace clijudge

#endif // CLIJUDGE_PLATFORM_H
