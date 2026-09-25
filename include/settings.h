#ifndef CLIJUDGE_SETTINGS_H
#define CLIJUDGE_SETTINGS_H

// settings.h
// 评测配置模块 —— 对应 LemonLime Settings 的评测相关设置
//
// 存储位置: data/config.json 的 "judge" 段（与 current_lang 等共存，互不覆盖）
//   {
//     "current_lang": "en",
//     "judge": {
//       "compile_time_limit_ms": 10000,
//       "special_judge_time_limit_ms": 10000,
//       "source_size_limit_kb": 50,
//       "rejudge_times": 1,
//       "max_rejudge_times": 12,
//       "max_judging_threads": 4,
//       "extra_time_ratio": 0.1,
//       "file_write_limit_kb": 16384,
//       "env": { "KEY": "VALUE" }
//     }
//   }

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include "json.hpp"
#include "platform.h"

namespace clijudge {
namespace settings {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 配置文件路径（与 lang.h getConfigPath 保持一致）
inline std::string configPath() {
    return platform::pathJoin(platform::pathJoin(platform::exeDir(), "data"), "config.json");
}

// 评测设置
struct JudgeSettings {
    int compileTimeLimitMs = 10000;          // 编译超时 (LemonLime getCompileTimeLimit)
    int specialJudgeTimeLimitMs = 10000;     // 特殊评测/交互进程超时
    int sourceSizeLimitKB = 50;              // 源代码大小上限 (fileSizeLimit * 1024 字节)
    int rejudgeTimes = 1;                    // 边界 TLE 自动重评测次数 (0..12)
    int maxRejudgeTimes = 12;                // 手动 rejudge 允许的最大评测次数
    int maxJudgingThreads = 4;               // 并行评测线程数
    double extraTimeRatio = 0.1;             // 额外时间比 (defaultExtraTimeRatio)
    long long fileWriteLimitKB = 16384;      // 子进程写文件大小上限 (RLIMIT_FSIZE)
    std::map<std::string, std::string> env;  // 追加到子进程的环境变量
};

inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline JudgeSettings fromJson(const json& j) {
    JudgeSettings s;
    if (!j.is_object()) return s;
    s.compileTimeLimitMs = clampInt(j.value("compile_time_limit_ms", s.compileTimeLimitMs), 100, 300000);
    s.specialJudgeTimeLimitMs = clampInt(j.value("special_judge_time_limit_ms", s.specialJudgeTimeLimitMs), 100, 300000);
    s.sourceSizeLimitKB = clampInt(j.value("source_size_limit_kb", s.sourceSizeLimitKB), 1, 1024 * 1024);
    s.rejudgeTimes = clampInt(j.value("rejudge_times", s.rejudgeTimes), 0, 12);
    s.maxRejudgeTimes = clampInt(j.value("max_rejudge_times", s.maxRejudgeTimes), 0, 100000);
    s.maxJudgingThreads = clampInt(j.value("max_judging_threads", s.maxJudgingThreads), 1, 64);
    double r = j.value("extra_time_ratio", s.extraTimeRatio);
    if (r < 0.0) r = 0.0;
    if (r > 10.0) r = 10.0;
    s.extraTimeRatio = r;
    long long fw = j.value("file_write_limit_kb", (long long)s.fileWriteLimitKB);
    if (fw < 64) fw = 64;
    s.fileWriteLimitKB = fw;
    if (j.contains("env") && j["env"].is_object()) {
        for (auto it = j["env"].begin(); it != j["env"].end(); ++it) {
            if (it.value().is_string()) s.env[it.key()] = it.value().get<std::string>();
        }
    }
    return s;
}

inline json toJson(const JudgeSettings& s) {
    json j;
    j["compile_time_limit_ms"] = s.compileTimeLimitMs;
    j["special_judge_time_limit_ms"] = s.specialJudgeTimeLimitMs;
    j["source_size_limit_kb"] = s.sourceSizeLimitKB;
    j["rejudge_times"] = s.rejudgeTimes;
    j["max_rejudge_times"] = s.maxRejudgeTimes;
    j["max_judging_threads"] = s.maxJudgingThreads;
    j["extra_time_ratio"] = s.extraTimeRatio;
    j["file_write_limit_kb"] = s.fileWriteLimitKB;
    json env = json::object();
    for (const auto& [k, v] : s.env) env[k] = v;
    j["env"] = env;
    return j;
}

// 读取整个 config.json（容忍不存在/损坏）
inline json loadRawConfig() {
    std::ifstream f(configPath());
    if (!f.is_open()) return json::object();
    try {
        json j;
        f >> j;
        if (j.is_object()) return j;
    } catch (...) {}
    return json::object();
}

// 读取评测设置
inline JudgeSettings load() {
    json raw = loadRawConfig();
    if (raw.contains("judge")) return fromJson(raw["judge"]);
    return JudgeSettings{};
}

// 保存评测设置（保留 config.json 其它键）
inline bool save(const JudgeSettings& s) {
    json raw = loadRawConfig();
    raw["judge"] = toJson(s);
    try {
        fs::create_directories(fs::path(configPath()).parent_path());
        std::ofstream f(configPath());
        if (!f.is_open()) return false;
        f << raw.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

// 获取当前设置（进程内缓存）
inline const JudgeSettings& get() {
    static JudgeSettings s = load();
    return s;
}

// ── 便捷 getter（对应 LemonLime Settings::get*）──────────────
inline int getCompileTimeLimit()          { return get().compileTimeLimitMs; }
inline int getSpecialJudgeTimeLimit()     { return get().specialJudgeTimeLimitMs; }
inline int getFileSizeLimitKB()           { return get().sourceSizeLimitKB; }
inline int getRejudgeTimes()              { return get().rejudgeTimes; }
inline int getMaxRejudgeTimes()           { return get().maxRejudgeTimes; }
inline int getMaxJudgingThreads()         { return get().maxJudgingThreads; }
inline double getDefaultExtraTimeRatio()  { return get().extraTimeRatio; }
inline long long getFileWriteLimitBytes() { return get().fileWriteLimitKB * 1024; }
inline const std::map<std::string, std::string>& getExtraEnv() { return get().env; }

// 评测设置转为子进程环境变量列表 ("K=V")
inline std::vector<std::string> extraEnvList() {
    std::vector<std::string> out;
    for (const auto& [k, v] : get().env) out.push_back(k + "=" + v);
    return out;
}

} // namespace settings
} // namespace clijudge

#endif // CLIJUDGE_SETTINGS_H
