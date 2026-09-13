#ifndef CLIJUDGE_JUDGE_H
#define CLIJUDGE_JUDGE_H

// judge.h
// CliJudge 评判核心模块
//
// 支持的评判模式:
//   text_strict  - 逐字节严格比较
//   text_line    - 逐行比较（忽略行末空白）
//   float_abs    - 实数绝对误差比较
//   float_rel    - 实数相对误差比较
//   float_all    - 实数综合比较（绝对+相对）
//   spj          - Special Judge 特殊评测
//
// 支持子任务:
//   subtask_mode: simple (独立) / group (分组)
//   依赖子任务: dependence_subtask

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include "json.hpp"
#include "sandbox_runner.hpp"

namespace clijudge {
namespace judge {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 评判结果状态
enum class JudgeStatus {
    ACCEPTED,
    WRONG_ANSWER,
    TIME_LIMIT_EXCEEDED,
    MEMORY_LIMIT_EXCEEDED,
    RUNTIME_ERROR,
    COMPILATION_ERROR,
    SYSTEM_ERROR,
    SKIPPED
};

// 单个测试点结果
struct TestCaseResult {
    int id;
    int score;
    int maxScore;
    JudgeStatus status;
    int timeUsedMs;
    int memoryUsedKB;
    std::string message;
};

// 子任务结果
struct SubtaskResult {
    int id;
    int score;
    int maxScore;
    JudgeStatus status;
    std::vector<TestCaseResult> testCases;
};

// 完整评判结果
struct JudgeResult {
    int totalScore;
    int maxScore;
    JudgeStatus status;
    std::vector<SubtaskResult> subtasks;
    std::string compileError;
    int totalTimeMs;
    int maxMemoryKB;
};

// 状态转字符串
inline std::string statusToString(JudgeStatus s) {
    switch (s) {
        case JudgeStatus::ACCEPTED: return "Accepted";
        case JudgeStatus::WRONG_ANSWER: return "Wrong Answer";
        case JudgeStatus::TIME_LIMIT_EXCEEDED: return "Time Limit Exceeded";
        case JudgeStatus::MEMORY_LIMIT_EXCEEDED: return "Memory Limit Exceeded";
        case JudgeStatus::RUNTIME_ERROR: return "Runtime Error";
        case JudgeStatus::COMPILATION_ERROR: return "Compilation Error";
        case JudgeStatus::SYSTEM_ERROR: return "System Error";
        case JudgeStatus::SKIPPED: return "Skipped";
        default: return "Unknown";
    }
}

// 状态转缩写
inline std::string statusToAbbr(JudgeStatus s) {
    switch (s) {
        case JudgeStatus::ACCEPTED: return "AC";
        case JudgeStatus::WRONG_ANSWER: return "WA";
        case JudgeStatus::TIME_LIMIT_EXCEEDED: return "TLE";
        case JudgeStatus::MEMORY_LIMIT_EXCEEDED: return "MLE";
        case JudgeStatus::RUNTIME_ERROR: return "RE";
        case JudgeStatus::COMPILATION_ERROR: return "CE";
        case JudgeStatus::SYSTEM_ERROR: return "SE";
        case JudgeStatus::SKIPPED: return "SK";
        default: return "??";
    }
}

// ── 文件读取工具 ─────────────────────────────────────────────
inline std::string readFileContent(const std::string& path) {
    if (!fs::exists(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 写文件
inline bool writeFileContent(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(content.data(), content.size());
    return f.good();
}

// 去除行末空白
inline std::string trimLineEnd(const std::string& s) {
    std::string result = s;
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    return result;
}

// ── 比较函数 ─────────────────────────────────────────────────

// 严格文本比较
inline bool compareTextStrict(const std::string& expected, const std::string& actual) {
    return expected == actual;
}

// 逐行比较（忽略行末空白）
inline bool compareTextLine(const std::string& expected, const std::string& actual) {
    std::istringstream eStream(expected);
    std::istringstream aStream(actual);
    std::string eLine, aLine;
    
    while (true) {
        bool eOk = (bool)std::getline(eStream, eLine);
        bool aOk = (bool)std::getline(aStream, aLine);
        
        if (!eOk && !aOk) return true;  // 都结束
        if (eOk != aOk) return false;   // 一个结束一个没结束
        
        if (trimLineEnd(eLine) != trimLineEnd(aLine)) return false;
    }
}

// 实数比较（绝对误差）
inline bool compareFloatAbs(const std::string& expected, const std::string& actual, double tolerance) {
    std::istringstream eStream(expected);
    std::istringstream aStream(actual);
    std::string eToken, aToken;
    
    while (eStream >> eToken && aStream >> aToken) {
        try {
            double eVal = std::stod(eToken);
            double aVal = std::stod(aToken);
            if (std::abs(eVal - aVal) > tolerance) return false;
        } catch (...) {
            if (eToken != aToken) return false;
        }
    }
    
    // 检查是否都读完了
    return !(eStream >> eToken) && !(aStream >> aToken);
}

// 实数比较（相对误差）
inline bool compareFloatRel(const std::string& expected, const std::string& actual, double tolerance) {
    std::istringstream eStream(expected);
    std::istringstream aStream(actual);
    std::string eToken, aToken;
    
    while (eStream >> eToken && aStream >> aToken) {
        try {
            double eVal = std::stod(eToken);
            double aVal = std::stod(aToken);
            if (eVal == 0) {
                if (std::abs(aVal) > tolerance) return false;
            } else {
                if (std::abs((eVal - aVal) / eVal) > tolerance) return false;
            }
        } catch (...) {
            if (eToken != aToken) return false;
        }
    }
    
    return !(eStream >> eToken) && !(aStream >> aToken);
}

// 实数比较（综合：绝对+相对）
inline bool compareFloatAll(const std::string& expected, const std::string& actual, 
                           double absTol, double relTol) {
    std::istringstream eStream(expected);
    std::istringstream aStream(actual);
    std::string eToken, aToken;
    
    while (eStream >> eToken && aStream >> aToken) {
        try {
            double eVal = std::stod(eToken);
            double aVal = std::stod(aToken);
            double diff = std::abs(eVal - aVal);
            if (diff > absTol) {
                if (eVal == 0) {
                    if (diff > relTol) return false;
                } else {
                    if (diff / std::abs(eVal) > relTol) return false;
                }
            }
        } catch (...) {
            if (eToken != aToken) return false;
        }
    }
    
    return !(eStream >> eToken) && !(aStream >> aToken);
}

// Special Judge 比较
inline bool compareSpecialJudge(const std::string& spjExe, 
                               const std::string& inputData, 
                               const std::string& expectedOutput,
                               const std::string& actualOutput) {
    if (spjExe.empty() || !fs::exists(spjExe)) return false;
    
    // 创建临时目录用于 SPJ
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string workDir = std::string(tempPath) + "clijudge_spj_" + std::to_string(GetCurrentProcessId());
    fs::create_directories(workDir);
    
    // 写入临时文件
    writeFileContent(workDir + "\\input.txt", inputData);
    writeFileContent(workDir + "\\output.txt", expectedOutput);
    writeFileContent(workDir + "\\answer.txt", actualOutput);
    
    // 运行 SPJ
    std::string metaFile = workDir + "\\_meta.json";
    auto result = clijudge::sandbox_run(
        5000,   // 5秒超时
        256,    // 256MB 内存
        1,
        metaFile.c_str(),
        spjExe.c_str(),
        {workDir + "\\input.txt", workDir + "\\output.txt", workDir + "\\answer.txt"},
        false
    );
    
    // 读取 SPJ 结果
    bool accepted = false;
    if (fs::exists(metaFile)) {
        std::ifstream f(metaFile);
        if (f.is_open()) {
            json meta;
            f >> meta;
            accepted = (meta.value("exit_code", 1) == 0);
        }
    }
    
    // 清理
    try { fs::remove_all(workDir); } catch (...) {}
    
    return accepted;
}

// ── 编译检查 ─────────────────────────────────────────────────
// 简单检查可执行文件是否存在
inline bool isCompiled(const std::string& exePath) {
    return fs::exists(exePath);
}

// ── 单个测试点评判 ───────────────────────────────────────────
inline TestCaseResult judgeTestCase(
    int tcId,
    int maxScore,
    const std::string& inputData,
    const std::string& expectedOutput,
    const std::string& exePath,
    int timeLimitMs,
    int memoryLimitMB,
    const std::string& compareMode,
    double floatAbsTol,
    double floatRelTol,
    const std::string& spjExe
) {
    TestCaseResult result;
    result.id = tcId;
    result.score = 0;
    result.maxScore = maxScore;
    result.status = JudgeStatus::SYSTEM_ERROR;
    result.timeUsedMs = 0;
    result.memoryUsedKB = 0;
    
    // 转换为绝对路径
    std::string absExePath = fs::absolute(exePath).string();
    
    // 检查可执行文件
    if (!fs::exists(absExePath)) {
        result.status = JudgeStatus::COMPILATION_ERROR;
        result.message = "Executable not found: " + absExePath;
        return result;
    }
    
    // 创建临时目录
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string workDir = std::string(tempPath) + "clijudge_tc_" + std::to_string(tcId) + "_" + std::to_string(GetCurrentProcessId());
    fs::create_directories(workDir);
    
    std::string metaFile = workDir + "\\_meta.json";
    
    // 创建管道用于 stdin/stdout
    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0) ||
        !CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
        !CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        result.message = "Failed to create pipes";
        try { fs::remove_all(workDir); } catch (...) {}
        return result;
    }
    
    // 写入输入数据到 stdin 管道
    DWORD written = 0;
    if (!inputData.empty()) {
        WriteFile(hStdinWrite, inputData.data(), (DWORD)inputData.size(), &written, NULL);
    }
    CloseHandle(hStdinWrite);
    hStdinWrite = NULL;
    
    // 使用沙箱运行 — 直接运行可执行文件
    // 临时替换进程的 stdin/stdout handle
    HANDLE hOrigStdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOrigStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOrigStderr = GetStdHandle(STD_ERROR_HANDLE);
    
    SetStdHandle(STD_INPUT_HANDLE, hStdinRead);
    SetStdHandle(STD_OUTPUT_HANDLE, hStdoutWrite);
    SetStdHandle(STD_ERROR_HANDLE, hStderrWrite);
    
    auto sandboxResult = clijudge::sandbox_run(
        timeLimitMs,
        memoryLimitMB,
        1,
        metaFile.c_str(),
        absExePath.c_str(),
        {},
        false
    );
    
    // 恢复进程的 stdin/stdout handle
    SetStdHandle(STD_INPUT_HANDLE, hOrigStdin);
    SetStdHandle(STD_OUTPUT_HANDLE, hOrigStdout);
    SetStdHandle(STD_ERROR_HANDLE, hOrigStderr);
    
    // 关闭写端
    CloseHandle(hStdoutWrite);
    hStdoutWrite = NULL;
    CloseHandle(hStderrWrite);
    hStderrWrite = NULL;
    
    // 读取 stdout 输出
    std::string actualOutput;
    {
        char buf[4096];
        DWORD bytesRead = 0;
        while (ReadFile(hStdoutRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
            actualOutput.append(buf, bytesRead);
        }
    }
    
    // 读取 stderr 输出（用于调试）
    std::string stderrOutput;
    {
        char buf[4096];
        DWORD bytesRead = 0;
        while (ReadFile(hStderrRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
            stderrOutput.append(buf, bytesRead);
        }
    }
    
    // 关闭读端
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    
    // 读取元数据
    if (fs::exists(metaFile)) {
        std::ifstream f(metaFile);
        if (f.is_open()) {
            json meta;
            f >> meta;
            result.timeUsedMs = meta.value("time_used", 0);
            result.memoryUsedKB = meta.value("memory_used", 0);
            
            std::string signal = meta.value("signal", "null");
            int exitCode = meta.value("exit_code", 0);
            
            if (signal == "SIGKILL") {
                result.status = JudgeStatus::TIME_LIMIT_EXCEEDED;
                result.message = "Time Limit Exceeded";
            } else if (signal == "MEMORY_LIMIT") {
                result.status = JudgeStatus::MEMORY_LIMIT_EXCEEDED;
                result.message = "Memory Limit Exceeded";
            } else if (exitCode != 0) {
                result.status = JudgeStatus::RUNTIME_ERROR;
                result.message = "Runtime Error (exit code: " + std::to_string(exitCode) + ")";
                if (!stderrOutput.empty()) {
                    result.message += "\n" + stderrOutput;
                }
            } else {
                // 比较输出
                bool accepted = false;
                
                if (compareMode == "text_strict") {
                    accepted = compareTextStrict(expectedOutput, actualOutput);
                } else if (compareMode == "text_line") {
                    accepted = compareTextLine(expectedOutput, actualOutput);
                } else if (compareMode == "float_abs") {
                    accepted = compareFloatAbs(expectedOutput, actualOutput, floatAbsTol);
                } else if (compareMode == "float_rel") {
                    accepted = compareFloatRel(expectedOutput, actualOutput, floatRelTol);
                } else if (compareMode == "float_all") {
                    accepted = compareFloatAll(expectedOutput, actualOutput, floatAbsTol, floatRelTol);
                } else if (compareMode == "spj") {
                    accepted = compareSpecialJudge(spjExe, inputData, expectedOutput, actualOutput);
                } else {
                    accepted = compareTextStrict(expectedOutput, actualOutput);
                }
                
                result.status = accepted ? JudgeStatus::ACCEPTED : JudgeStatus::WRONG_ANSWER;
                result.score = accepted ? maxScore : 0;
                result.message = accepted ? "Correct" : "Wrong Answer";
            }
        }
    }
    
    // 清理
    try { fs::remove_all(workDir); } catch (...) {}
    
    return result;
}

// ── 完整评判 ─────────────────────────────────────────────────
inline JudgeResult judgeSubmission(
    const json& problem,
    const std::string& exePath,
    const std::string& compareMode = "",
    double floatAbsTol = 0.0,
    double floatRelTol = 0.0,
    const std::string& spjExe = ""
) {
    JudgeResult result;
    result.totalScore = 0;
    result.maxScore = 0;
    result.status = JudgeStatus::ACCEPTED;
    result.compileError = "";
    result.totalTimeMs = 0;
    result.maxMemoryKB = 0;
    
    // 获取题目配置
    std::string mode = compareMode.empty() ? 
        problem["problem"].value("compare_mode", "text_strict") : compareMode;
    double absTol = problem["problem"].value("float_abs_tolerance", 0.0);
    double relTol = problem["problem"].value("float_rel_tolerance", 0.0);
    std::string spj = spjExe.empty() ? 
        problem["problem"].value("special_judge_exe", "") : spjExe;
    
    // 获取测试用例
    json testCases = problem.value("test_cases", json::array());
    if (testCases.empty()) {
        result.status = JudgeStatus::SYSTEM_ERROR;
        result.compileError = "No test cases";
        return result;
    }
    
    // 按 sort_order 排序
    std::vector<json> sortedCases(testCases.begin(), testCases.end());
    std::sort(sortedCases.begin(), sortedCases.end(), [](const json& a, const json& b) {
        return a.value("sort_order", 0) < b.value("sort_order", 0);
    });
    
    // 获取题目级别的时限和内存
    int defaultTimeLimit = problem["problem"].value("time_limit", 1000);
    int defaultMemoryLimit = problem["problem"].value("memory_limit", 256);
    
    // 评判每个测试点
    SubtaskResult currentSubtask;
    currentSubtask.id = 1;
    currentSubtask.score = 0;
    currentSubtask.maxScore = 0;
    currentSubtask.status = JudgeStatus::ACCEPTED;
    
    for (const auto& tc : sortedCases) {
        int tcId = tc.value("id", 0);
        int tcScore = tc.value("score", 10);
        int tcTimeLimit = tc.value("time_limit", -1);
        int tcMemoryLimit = tc.value("memory_limit", -1);
        
        // 使用测试点特定的时限和内存，如果没有则使用题目默认值
        int timeLimit = (tcTimeLimit > 0) ? tcTimeLimit : defaultTimeLimit;
        int memoryLimit = (tcMemoryLimit > 0) ? tcMemoryLimit : defaultMemoryLimit;
        
        std::string inputData = tc.value("input_data", "");
        std::string expectedOutput = tc.value("output_data", "");
        
        TestCaseResult tcResult = judgeTestCase(
            tcId, tcScore, inputData, expectedOutput, exePath,
            timeLimit, memoryLimit, mode, absTol, relTol, spj
        );
        
        currentSubtask.testCases.push_back(tcResult);
        currentSubtask.maxScore += tcScore;
        result.maxScore += tcScore;
        
        if (tcResult.status == JudgeStatus::ACCEPTED) {
            currentSubtask.score += tcResult.score;
            result.totalScore += tcResult.score;
        } else {
            // 某个测试点失败，整个子任务失败
            currentSubtask.status = tcResult.status;
            result.status = tcResult.status;
        }
        
        result.totalTimeMs += tcResult.timeUsedMs;
        if (tcResult.memoryUsedKB > result.maxMemoryKB) {
            result.maxMemoryKB = tcResult.memoryUsedKB;
        }
    }
    
    result.subtasks.push_back(currentSubtask);
    
    return result;
}

// ── JSON 输出 ────────────────────────────────────────────────
inline json resultToJson(const JudgeResult& result) {
    json j;
    j["total_score"] = result.totalScore;
    j["max_score"] = result.maxScore;
    j["status"] = statusToString(result.status);
    j["status_abbr"] = statusToAbbr(result.status);
    j["total_time_ms"] = result.totalTimeMs;
    j["max_memory_kb"] = result.maxMemoryKB;
    j["compile_error"] = result.compileError;
    
    json subtasks = json::array();
    for (const auto& st : result.subtasks) {
        json stJson;
        stJson["id"] = st.id;
        stJson["score"] = st.score;
        stJson["max_score"] = st.maxScore;
        stJson["status"] = statusToString(st.status);
        stJson["status_abbr"] = statusToAbbr(st.status);
        
        json cases = json::array();
        for (const auto& tc : st.testCases) {
            json tcJson;
            tcJson["id"] = tc.id;
            tcJson["score"] = tc.score;
            tcJson["max_score"] = tc.maxScore;
            tcJson["status"] = statusToString(tc.status);
            tcJson["status_abbr"] = statusToAbbr(tc.status);
            tcJson["time_ms"] = tc.timeUsedMs;
            tcJson["memory_kb"] = tc.memoryUsedKB;
            tcJson["message"] = tc.message;
            cases.push_back(tcJson);
        }
        stJson["test_cases"] = cases;
        subtasks.push_back(stJson);
    }
    j["subtasks"] = subtasks;
    
    return j;
}

// ── 显示评判结果 ─────────────────────────────────────────────
inline void printResult(const JudgeResult& result) {
    std::cout << "=== Judge Result ===" << std::endl;
    std::cout << "Score: " << result.totalScore << " / " << result.maxScore << std::endl;
    std::cout << "Status: " << statusToAbbr(result.status) << " (" << statusToString(result.status) << ")" << std::endl;
    std::cout << "Time: " << result.totalTimeMs << " ms" << std::endl;
    std::cout << "Memory: " << result.maxMemoryKB << " KB" << std::endl;
    
    if (!result.compileError.empty()) {
        std::cout << "Compile Error: " << result.compileError << std::endl;
    }
    
    for (const auto& st : result.subtasks) {
        std::cout << "\n--- Subtask " << st.id << " ---" << std::endl;
        std::cout << "Score: " << st.score << " / " << st.maxScore << std::endl;
        std::cout << "Status: " << statusToAbbr(st.status) << std::endl;
        
        for (const auto& tc : st.testCases) {
            std::cout << "  Test " << tc.id << ": " << statusToAbbr(tc.status) 
                      << " (" << tc.timeUsedMs << " ms, " << tc.memoryUsedKB << " KB)" 
                      << std::endl;
        }
    }
}

} // namespace judge
} // namespace clijudge

#endif // CLIJUDGE_JUDGE_H