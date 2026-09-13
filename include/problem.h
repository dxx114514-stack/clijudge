#ifndef JUDGELITE_PROBLEM_H
#define JUDGELITE_PROBLEM_H

// problem.h
// JudgeLite 题目管理子命令
//
// 子命令:
//   count - 统计题目数量
//   create [标题] - 创建题目
//   delete [编号] - 删除题目
//   edit [编号] - 编辑题目
//   export [zip路径] - 导出题目
//   import [zip路径] - 导入题目
//   list [L=1] [R=50] - 列出题目
//   submit [编号] [程序文件路径] - 提交题目
//   testdata [题目编号]
//     -set-all - 设置所有测试数据
//     -zip [zip路径] - 从zip导入测试数据
//     create [in] [out] [time] [mem] [pts] - 创建测试数据
//     delete [编号] - 删除测试数据
//     list - 列出测试数据
//   view [编号] - 查看题目

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include "json.hpp"
#include "sandbox_runner.hpp"
#include "submit.h"

namespace judgelite {
namespace problem {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 测试数据结构
struct TestCase {
    int id;
    std::string inputData;
    std::string outputData;
    std::string inputFile;
    std::string outputFile;
    int score;
    int timeLimit;      // null表示使用题目默认值
    int memoryLimit;    // null表示使用题目默认值
    int sortOrder;
};

// 题目数据结构
struct Problem {
    int id;
    std::string title;
    std::string description;
    std::string inputDesc;
    std::string outputDesc;
    std::string hint;
    int timeLimit;      // 毫秒
    int memoryLimit;    // MB
    bool isPublic;
    bool isHidden;
    std::string sampleInput;
    std::string sampleOutput;
    std::string subtaskMode;
    std::string problemType;      // traditional, interaction, submit_answer
    std::string compareMode;      // text_strict, text_line, float_abs, float_rel, float_all, spj
    double floatAbsTolerance;     // 绝对误差
    double floatRelTolerance;     // 相对误差
    std::string spjCode;          // Special Judge 代码
    std::string specialJudgeExe;  // Special Judge 可执行文件路径
    std::vector<std::string> allowedLanguages;  // 允许的语言
    std::vector<TestCase> testCases;
    std::string createdAt;
    std::string updatedAt;
};

// 提交记录结构
struct Submission {
    int id;
    int problemId;
    std::string filePath;
    std::string submittedAt;
    std::string status;
    int score;
    int timeUsed;
    int memoryUsed;
    std::string username;
};

// 数据存储类
class ProblemStore {
private:
    std::string dataDir;
    json data;

    void ensureDataDir() {
        if (!fs::exists(dataDir)) {
            fs::create_directories(dataDir);
        }
    }

    std::string getProblemPath(int id) {
        return dataDir + "/problem_" + std::to_string(id) + ".json";
    }

    void loadIndex() {
        std::string indexPath = dataDir + "/problems.json";
        if (fs::exists(indexPath)) {
            std::ifstream f(indexPath);
            if (f.is_open()) {
                // 检查文件是否为空
                f.seekg(0, std::ios::end);
                if (f.tellg() > 0) {
                    f.seekg(0, std::ios::beg);
                    try {
                        f >> data;
                    } catch (...) {
                        data = json::array();
                    }
                }
            }
        }
        if (data.empty()) {
            data = json::array();
        }
    }

    void saveIndex() {
        ensureDataDir();
        std::string indexPath = dataDir + "/problems.json";
        std::ofstream f(indexPath);
        if (f.is_open()) {
            f << data.dump(2);
        }
    }

    int getNextId() {
        int maxId = 0;
        for (const auto& item : data) {
            // 检查顶层 id（旧格式）
            if (item.contains("id") && item["id"].get<int>() > maxId) {
                maxId = item["id"].get<int>();
            }
            // 检查嵌套的 problem.id（NoldOJ格式）
            if (item.contains("problem") && item["problem"].contains("id")) {
                int pid = item["problem"]["id"].get<int>();
                if (pid > maxId) maxId = pid;
            }
        }
        return maxId + 1;
    }

    int getNextTestCaseId(const json& testCases) {
        int maxId = 0;
        for (const auto& tc : testCases) {
            if (tc.contains("id") && tc["id"].get<int>() > maxId) {
                maxId = tc["id"].get<int>();
            }
        }
        return maxId + 1;
    }

public:
    ProblemStore(const std::string& dir) : dataDir(dir) {
        loadIndex();
    }

    // 统计题目数量
    int count() {
        return (int)data.size();
    }

    // 创建题目
    int create(const std::string& title) {
        int id = getNextId();

        json problem = {
            {"problem", {
                {"id", id},
                {"title", title},
                {"description", ""},
                {"input_desc", ""},
                {"output_desc", ""},
                {"hint", ""},
                {"time_limit", 1000},
                {"memory_limit", 256},
                {"is_public", true},
                {"is_hidden", false},
                {"sample_input", ""},
                {"sample_output", ""},
                {"subtask_mode", "simple"},
                {"problem_type", "traditional"},
                {"compare_mode", "text_strict"},
                {"float_abs_tolerance", 0.0},
                {"float_rel_tolerance", 0.0},
                {"spj_code", ""},
                {"special_judge_exe", ""},
                {"allowed_languages", json::array()}
            }},
            {"test_cases", json::array()}
        };

        data.push_back(problem);
        saveIndex();

        // 保存题目详情到单独文件
        ensureDataDir();
        std::ofstream f(getProblemPath(id));
        if (f.is_open()) {
            f << problem.dump(2);
        }

        return id;
    }

    // 删除题目
    bool deleteProblem(int id) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it->contains("problem") && (*it)["problem"].contains("id") && (*it)["problem"]["id"].get<int>() == id) {
                data.erase(it);
                saveIndex();

                // 删除题目文件
                std::string path = getProblemPath(id);
                if (fs::exists(path)) {
                    fs::remove(path);
                }
                return true;
            }
        }
        return false;
    }

    // 查看题目
    json view(int id) {
        for (const auto& item : data) {
            if (item.contains("problem") && item["problem"].contains("id") && item["problem"]["id"].get<int>() == id) {
                // 加载完整详情
                std::string path = getProblemPath(id);
                if (fs::exists(path)) {
                    std::ifstream f(path);
                    if (f.is_open()) {
                        json fullProblem;
                        f >> fullProblem;
                        return fullProblem;
                    }
                }
                return item;
            }
        }
        return nullptr;
    }

    // 编辑题目
    bool edit(int id, const json& updates) {
        for (auto& item : data) {
            if (item.contains("problem") && item["problem"].contains("id") && item["problem"]["id"].get<int>() == id) {
                // 更新字段
                for (auto it = updates.begin(); it != updates.end(); ++it) {
                    item["problem"][it.key()] = it.value();
                }
                saveIndex();

                // 保存到文件
                ensureDataDir();
                std::ofstream f(getProblemPath(id));
                if (f.is_open()) {
                    f << item.dump(2);
                }
                return true;
            }
        }
        return false;
    }

    // 列出题目
    json list(int left = 1, int right = 50) {
        json result = json::array();
        int count = 0;
        for (const auto& item : data) {
            count++;
            if (count >= left && count <= right) {
                // 只返回基本信息
                json summary = {
                    {"id", item["problem"]["id"]},
                    {"title", item["problem"]["title"]}
                };
                result.push_back(summary);
            }
            if (count > right) break;
        }
        return result;
    }

    // 提交题目
    Submission submit(int problemId, const std::string& filePath) {
        Submission sub;
        sub.id = 0;
        sub.problemId = problemId;
        sub.filePath = filePath;
        sub.submittedAt = getCurrentTime();
        sub.status = "pending";
        sub.score = 0;
        sub.timeUsed = 0;
        sub.memoryUsed = 0;

        // 验证题目存在
        json problem = view(problemId);
        if (problem.is_null()) {
            sub.status = "error";
            return sub;
        }

        // 验证文件存在
        if (!fs::exists(filePath)) {
            sub.status = "error";
            return sub;
        }

        // 获取题目配置
        int timeLimit = problem["problem"].value("time_limit", 1000);
        int memoryLimit = problem["problem"].value("memory_limit", 256);

        // 创建临时工作目录
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string workDir = std::string(tempPath) + "judgelite_submit_" + std::to_string(problemId);
        fs::create_directories(workDir);

        std::string metaFile = workDir + "\\_meta.json";

        // 使用沙箱运行
        auto result = judgelite::sandbox_run(
            timeLimit,
            memoryLimit,
            1,
            metaFile.c_str(),
            filePath.c_str(),
            {},
            false
        );

        // 读取元数据
        if (fs::exists(metaFile)) {
            std::ifstream f(metaFile);
            if (f.is_open()) {
                json meta;
                f >> meta;
                sub.timeUsed = meta.value("time_used", 0);
                sub.memoryUsed = meta.value("memory_used", 0);
                sub.status = meta.value("signal", "null");
                if (sub.status == "null") {
                    sub.status = (meta.value("exit_code", 0) == 0) ? "accepted" : "runtime_error";
                }
            }
        }

        // 清理临时目录
        try {
            fs::remove_all(workDir);
        } catch (...) {}

        return sub;
    }

    // 获取测试数据
    json getTestCases(int problemId) {
        json problem = view(problemId);
        if (problem.is_null()) return json::array();
        return problem.value("test_cases", json::array());
    }

    // 添加测试数据
    bool addTestCase(int problemId, const TestCase& tc) {
        for (auto& item : data) {
            if (item.contains("problem") && item["problem"].contains("id") && item["problem"]["id"].get<int>() == problemId) {
                json testCase = {
                    {"id", tc.id},
                    {"input_data", tc.inputData},
                    {"output_data", tc.outputData},
                    {"input_file", tc.inputFile},
                    {"output_file", tc.outputFile},
                    {"score", tc.score},
                    {"time_limit", tc.timeLimit},
                    {"memory_limit", tc.memoryLimit},
                    {"sort_order", tc.sortOrder}
                };

                item["test_cases"].push_back(testCase);
                saveIndex();

                // 保存到文件
                ensureDataDir();
                std::ofstream f(getProblemPath(problemId));
                if (f.is_open()) {
                    f << item.dump(2);
                }
                return true;
            }
        }
        return false;
    }

    // 删除测试数据
    bool deleteTestCase(int problemId, int testCaseId) {
        for (auto& item : data) {
            if (item.contains("problem") && item["problem"].contains("id") && item["problem"]["id"].get<int>() == problemId) {
                auto& testCases = item["test_cases"];
                for (auto it = testCases.begin(); it != testCases.end(); ++it) {
                    if (it->contains("id") && (*it)["id"].get<int>() == testCaseId) {
                        testCases.erase(it);
                        saveIndex();

                        // 保存到文件
                        ensureDataDir();
                        std::ofstream f(getProblemPath(problemId));
                        if (f.is_open()) {
                            f << item.dump(2);
                        }
                        return true;
                    }
                }
                return false;
            }
        }
        return false;
    }

    // 设置所有测试数据的默认值
    bool setAllTestCaseDefaults(int problemId, int timeLimit = -1, int memoryLimit = -1, int score = -1) {
        for (auto& item : data) {
            if (item.contains("problem") && item["problem"].contains("id") && item["problem"]["id"].get<int>() == problemId) {
                auto& testCases = item["test_cases"];
                for (auto& tc : testCases) {
                    if (timeLimit >= 0) tc["time_limit"] = timeLimit;
                    if (memoryLimit >= 0) tc["memory_limit"] = memoryLimit;
                    if (score >= 0) tc["score"] = score;
                }
                saveIndex();

                // 保存到文件
                ensureDataDir();
                std::ofstream f(getProblemPath(problemId));
                if (f.is_open()) {
                    f << item.dump(2);
                }
                return true;
            }
        }
        return false;
    }

    // 导出题目到JSON
    json exportProblem(int problemId) {
        return view(problemId);
    }

    // 从JSON导入题目（支持 NoldOJ 和 JudgeLite 格式）
    int importProblem(const json& problemData) {
        if (!problemData.contains("problem") || !problemData.contains("test_cases")) {
            return -1;
        }

        int id = getNextId();
        json imported;
        
        // 处理 NoldOJ 格式（带 version 字段）
        if (problemData.contains("version")) {
            imported["problem"] = problemData["problem"];
            imported["test_cases"] = problemData["test_cases"];
        } else {
            // JudgeLite 旧格式
            imported = problemData;
        }
        
        imported["problem"]["id"] = id;

        data.push_back(imported);
        saveIndex();

        // 保存到文件
        ensureDataDir();
        std::ofstream f(getProblemPath(id));
        if (f.is_open()) {
            f << imported.dump(2);
        }

        return id;
    }

private:
    std::string getCurrentTime() {
        time_t now = time(nullptr);
        char buf[64];
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return std::string(buf);
    }
};

// 子命令实现

// 辅助函数：读取文件内容
inline std::string readFileContent(const std::string& path) {
    if (path.empty()) return "";
    // 先检查文件是否存在
    if (!fs::exists(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    // 处理 UTF-8 BOM
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
    }
    // 处理 UTF-16 LE BOM
    if (content.size() >= 2 &&
        (unsigned char)content[0] == 0xFF &&
        (unsigned char)content[1] == 0xFE) {
        // 简单转换：跳过 BOM，按 UTF-8 处理后续字节
        content = content.substr(2);
    }
    return content;
}

// 获取ISO格式当前时间
inline std::string getCurrentTimeISO() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return std::string(buf);
}

inline int cmdCount(const std::string& dataDir) {
    ProblemStore store(dataDir);
    std::cout << store.count() << std::endl;
    return 0;
}

inline int cmdCreate(const std::string& dataDir, const std::string& title,
                     const std::string& background = "",
                     const std::string& describe = "",
                     const std::string& exampleIn = "",
                     const std::string& exampleOut = "",
                     const std::string& instyle = "",
                     const std::string& outstyle = "",
                     const std::string& compareMode = "",
                     const std::string& spjCode = "",
                     const std::string& spjExe = "",
                     double floatAbsTol = 0.0,
                     double floatRelTol = 0.0) {
    ProblemStore store(dataDir);
    int id = store.create(title);

    // 应用可选参数
    json updates;
    if (!background.empty()) {
        updates["description"] = readFileContent(background);
    }
    if (!describe.empty()) {
        updates["description"] = readFileContent(describe);
    }
    if (!exampleIn.empty()) {
        updates["sample_input"] = readFileContent(exampleIn);
    }
    if (!exampleOut.empty()) {
        updates["sample_output"] = readFileContent(exampleOut);
    }
    if (!instyle.empty()) {
        updates["input_desc"] = readFileContent(instyle);
    }
    if (!outstyle.empty()) {
        updates["output_desc"] = readFileContent(outstyle);
    }
    if (!compareMode.empty()) {
        updates["compare_mode"] = compareMode;
        if (compareMode != "text_strict") {
            updates["problem_type"] = "traditional";
        }
    }
    if (!spjCode.empty()) {
        updates["spj_code"] = readFileContent(spjCode);
    }
    if (!spjExe.empty()) {
        updates["special_judge_exe"] = spjExe;
    }
    if (floatAbsTol > 0.0) {
        updates["float_abs_tolerance"] = floatAbsTol;
    }
    if (floatRelTol > 0.0) {
        updates["float_rel_tolerance"] = floatRelTol;
    }

    if (!updates.empty()) {
        store.edit(id, updates);
    }

    std::cout << "题目已创建，编号: " << id << std::endl;
    return 0;
}

inline int cmdDelete(const std::string& dataDir, int id) {
    ProblemStore store(dataDir);
    if (store.deleteProblem(id)) {
        std::cout << "题目 " << id << " 已删除。" << std::endl;
        return 0;
    } else {
        std::cerr << "题目 " << id << " 未找到。" << std::endl;
        return 1;
    }
}

inline int cmdView(const std::string& dataDir, int id) {
    ProblemStore store(dataDir);
    json problem = store.view(id);
    if (problem.is_null()) {
        std::cerr << "题目 " << id << " 未找到。" << std::endl;
        return 1;
    }

    const auto& p = problem["problem"];

    // 格式化显示题面
    std::cout << "=== 题目 " << p.value("id", 0) << " ===" << std::endl;
    std::cout << "标题: " << p.value("title", "") << std::endl;
    std::cout << std::endl;

    std::string desc = p.value("description", "");
    if (!desc.empty()) {
        std::cout << "## 题目描述" << std::endl;
        std::cout << desc << std::endl;
        std::cout << std::endl;
    }

    std::string inputDesc = p.value("input_desc", "");
    if (!inputDesc.empty()) {
        std::cout << "## 输入格式" << std::endl;
        std::cout << inputDesc << std::endl;
        std::cout << std::endl;
    }

    std::string outputDesc = p.value("output_desc", "");
    if (!outputDesc.empty()) {
        std::cout << "## 输出格式" << std::endl;
        std::cout << outputDesc << std::endl;
        std::cout << std::endl;
    }

    std::string sampleIn = p.value("sample_input", "");
    std::string sampleOut = p.value("sample_output", "");
    if (!sampleIn.empty() || !sampleOut.empty()) {
        std::cout << "## 样例输入/输出" << std::endl;
        if (!sampleIn.empty()) {
            std::cout << "输入:" << std::endl;
            std::cout << "```" << std::endl;
            std::cout << sampleIn << std::endl;
            std::cout << "```" << std::endl;
        }
        if (!sampleOut.empty()) {
            std::cout << "输出:" << std::endl;
            std::cout << "```" << std::endl;
            std::cout << sampleOut << std::endl;
            std::cout << "```" << std::endl;
        }
        std::cout << std::endl;
    }

    std::string hint = p.value("hint", "");
    if (!hint.empty()) {
        std::cout << "## 提示" << std::endl;
        std::cout << hint << std::endl;
        std::cout << std::endl;
    }

    std::cout << "## 限制" << std::endl;
    std::cout << "时间限制: " << p.value("time_limit", 1000) << " 毫秒" << std::endl;
    std::cout << "内存限制: " << p.value("memory_limit", 256) << " MB" << std::endl;
    std::cout << "公开: " << (p.value("is_public", true) ? "是" : "否") << std::endl;
    std::cout << "隐藏: " << (p.value("is_hidden", false) ? "是" : "否") << std::endl;

    // Special Judge 相关信息
    std::string problemType = p.value("problem_type", "traditional");
    std::string compareMode = p.value("compare_mode", "text_strict");
    
    if (problemType != "traditional" || compareMode != "text_strict") {
        std::cout << std::endl;
        std::cout << "## 特殊评测" << std::endl;
        std::cout << "题目类型: " << problemType << std::endl;
        std::cout << "比较模式: " << compareMode << std::endl;
        
        if (compareMode == "float_abs" || compareMode == "float_rel" || compareMode == "float_all") {
            std::cout << "浮点绝对误差: " << p.value("float_abs_tolerance", 0.0) << std::endl;
            std::cout << "浮点相对误差: " << p.value("float_rel_tolerance", 0.0) << std::endl;
        }
        
        std::string specialJudgeExe = p.value("special_judge_exe", "");
        if (!specialJudgeExe.empty()) {
            std::cout << "特殊评测程序: " << specialJudgeExe << std::endl;
        }
        
        const auto& allowedLangs = p.value("allowed_languages", json::array());
        if (!allowedLangs.empty()) {
            std::cout << "允许的语言: ";
            for (size_t i = 0; i < allowedLangs.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << allowedLangs[i].get<std::string>();
            }
            std::cout << std::endl;
        }
    }

    return 0;
}

inline int cmdEdit(const std::string& dataDir, int id,
                   const std::string& title = "",
                   const std::string& background = "",
                   const std::string& describe = "",
                   const std::string& exampleIn = "",
                   const std::string& exampleOut = "",
                   const std::string& instyle = "",
                   const std::string& outstyle = "",
                   const std::string& compareMode = "",
                   const std::string& spjCode = "",
                   const std::string& spjExe = "",
                   double floatAbsTol = 0.0,
                   double floatRelTol = 0.0) {
    ProblemStore store(dataDir);

    json updates;
    if (!title.empty()) {
        updates["title"] = title;
    }
    if (!background.empty()) {
        updates["description"] = readFileContent(background);
    }
    if (!describe.empty()) {
        updates["description"] = readFileContent(describe);
    }
    if (!exampleIn.empty()) {
        updates["sample_input"] = readFileContent(exampleIn);
    }
    if (!exampleOut.empty()) {
        updates["sample_output"] = readFileContent(exampleOut);
    }
    if (!instyle.empty()) {
        updates["input_desc"] = readFileContent(instyle);
    }
    if (!outstyle.empty()) {
        updates["output_desc"] = readFileContent(outstyle);
    }
    if (!compareMode.empty()) {
        updates["compare_mode"] = compareMode;
        if (compareMode != "text_strict") {
            updates["problem_type"] = "traditional";
        }
    }
    if (!spjCode.empty()) {
        updates["spj_code"] = readFileContent(spjCode);
    }
    if (!spjExe.empty()) {
        updates["special_judge_exe"] = spjExe;
    }
    if (floatAbsTol > 0.0) {
        updates["float_abs_tolerance"] = floatAbsTol;
    }
    if (floatRelTol > 0.0) {
        updates["float_rel_tolerance"] = floatRelTol;
    }

    if (updates.empty()) {
        std::cerr << "未指定更新内容。" << std::endl;
        return 1;
    }

    if (store.edit(id, updates)) {
        std::cout << "题目 " << id << " 已更新。" << std::endl;
        return 0;
    } else {
        std::cerr << "题目 " << id << " 未找到。" << std::endl;
        return 1;
    }
}

inline int cmdList(const std::string& dataDir, int left = 1, int right = 50) {
    ProblemStore store(dataDir);
    json problems = store.list(left, right);
    std::cout << problems.dump(2) << std::endl;
    return 0;
}

inline int cmdSubmit(const std::string& dataDir, int problemId, const std::string& filePath,
                     const std::string& username = "") {
    ProblemStore store(dataDir);
    auto submission = store.submit(problemId, filePath);

    judgelite::submit::addSubmission(dataDir, problemId, "", filePath,
                                     submission.status, submission.score,
                                     submission.timeUsed, submission.memoryUsed,
                                     username);

    std::cout << "提交结果:" << std::endl;
    std::cout << "  状态: " << submission.status << std::endl;
    std::cout << "  时间: " << submission.timeUsed << " 毫秒" << std::endl;
    std::cout << "  内存: " << submission.memoryUsed << " KB" << std::endl;
    std::cout << "  用户: " << (username.empty() ? "unknown" : username) << std::endl;

    return (submission.status == "accepted") ? 0 : 1;
}

inline int cmdTestDataList(const std::string& dataDir, int problemId) {
    ProblemStore store(dataDir);
    json testCases = store.getTestCases(problemId);
    std::cout << testCases.dump(2) << std::endl;
    return 0;
}

inline int cmdTestDataCreate(const std::string& dataDir, int problemId,
                             const std::string& inputData, const std::string& outputData,
                             int timeLimit = -1, int memoryLimit = -1, int score = 25) {
    ProblemStore store(dataDir);
    json testCases = store.getTestCases(problemId);
    int nextId = testCases.empty() ? 1 : testCases.back().value("id", 0) + 1;

    // 读取文件内容
    std::string inContent = readFileContent(inputData);
    if (inContent.empty()) {
        std::cerr << "无法读取输入文件: " << inputData << std::endl;
        return 1;
    }
    std::string outContent = readFileContent(outputData);
    if (outContent.empty()) {
        std::cerr << "无法读取输出文件: " << outputData << std::endl;
        return 1;
    }
    TestCase tc;
    tc.id = nextId;
    tc.inputData = inContent;
    tc.outputData = outContent;
    tc.inputFile = "";
    tc.outputFile = "";
    tc.score = score;
    tc.timeLimit = timeLimit;
    tc.memoryLimit = memoryLimit;
    tc.sortOrder = nextId;

    if (store.addTestCase(problemId, tc)) {
        std::cout << "测试点已创建，编号: " << tc.id << std::endl;
        return 0;
    } else {
        std::cerr << "创建测试点失败。" << std::endl;
        return 1;
    }
}

inline int cmdTestDataDelete(const std::string& dataDir, int problemId, int testCaseId) {
    ProblemStore store(dataDir);
    if (store.deleteTestCase(problemId, testCaseId)) {
        std::cout << "测试点 " << testCaseId << " 已删除。" << std::endl;
        return 0;
    } else {
        std::cerr << "测试点 " << testCaseId << " 未找到。" << std::endl;
        return 1;
    }
}

inline int cmdTestDataSetAll(const std::string& dataDir, int problemId,
                             int timeLimit = -1, int memoryLimit = -1, int score = -1) {
    ProblemStore store(dataDir);
    if (store.setAllTestCaseDefaults(problemId, timeLimit, memoryLimit, score)) {
        std::cout << "所有测试点已更新。" << std::endl;
        return 0;
    } else {
        std::cerr << "更新测试点失败。" << std::endl;
        return 1;
    }
}

inline int cmdExport(const std::string& dataDir, int problemId, const std::string& outputPath) {
    ProblemStore store(dataDir);
    json problem = store.exportProblem(problemId);
    if (problem.is_null()) {
        std::cerr << "题目 " << problemId << " 未找到。" << std::endl;
        return 1;
    }

    // 构建 NoldOJ 兼容的导出格式
    json exportData;
    exportData["version"] = 1;
    exportData["exported_at"] = getCurrentTimeISO();
    
    // 移除id字段，导入时重新分配
    json problemCopy = problem["problem"];
    problemCopy.erase("id");
    exportData["problem"] = problemCopy;
    
    // 处理测试用例
    json testCases = json::array();
    if (problem.contains("test_cases")) {
        for (const auto& tc : problem["test_cases"]) {
            json tcCopy = tc;
            tcCopy.erase("id");
            // 生成名称
            int sortOrder = tcCopy.value("sort_order", 0);
            tcCopy["name"] = "case_" + std::to_string(sortOrder);
            testCases.push_back(tcCopy);
        }
    }
    exportData["test_cases"] = testCases;

    // 检测输出格式
    bool isZip = false;
    if (outputPath.size() >= 4) {
        std::string ext = outputPath.substr(outputPath.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        isZip = (ext == ".zip");
    }

    if (isZip) {
        // ZIP 格式导出（需要 miniz 库支持）
        std::cerr << "ZIP导出需要miniz库支持，请使用.json格式。" << std::endl;
        return 1;
    }

    // JSON 格式导出
    std::ofstream f(outputPath);
    if (f.is_open()) {
        f << exportData.dump(2);
        f.close();
        std::cout << "题目已导出到: " << outputPath << std::endl;
        return 0;
    } else {
        std::cerr << "创建导出文件失败。" << std::endl;
        return 1;
    }
}

inline int cmdImport(const std::string& dataDir, const std::string& importPath) {
    if (!fs::exists(importPath)) {
        std::cerr << "导入文件未找到: " << importPath << std::endl;
        return 1;
    }

    // 检测是否为 ZIP 文件
    bool isZip = false;
    if (importPath.size() >= 4) {
        std::string ext = importPath.substr(importPath.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        isZip = (ext == ".zip");
    }

    if (isZip) {
        std::cerr << "ZIP导入需要miniz库支持，请使用.json格式。" << std::endl;
        return 1;
    }

    std::ifstream f(importPath);
    if (!f.is_open()) {
        std::cerr << "打开导入文件失败。" << std::endl;
        return 1;
    }

    json problemData;
    try {
        f >> problemData;
        f.close();
    } catch (const json::parse_error& e) {
        std::cerr << "解析导入文件失败: " << e.what() << std::endl;
        return 1;
    }

    // 支持 NoldOJ 兼容格式（带 version 字段）
    if (problemData.contains("version") && problemData.contains("problem")) {
        // NoldOJ 格式
        ProblemStore store(dataDir);
        int id = store.importProblem(problemData);
        if (id > 0) {
            std::cout << "题目已导入，编号: " << id << std::endl;
            return 0;
        } else {
            std::cerr << "导入题目失败。" << std::endl;
            return 1;
        }
    }
    // 支持旧格式（直接包含 problem 和 test_cases）
    else if (problemData.contains("problem") && problemData.contains("test_cases")) {
        ProblemStore store(dataDir);
        int id = store.importProblem(problemData);
        if (id > 0) {
            std::cout << "题目已导入，编号: " << id << std::endl;
            return 0;
        } else {
            std::cerr << "导入题目失败。" << std::endl;
            return 1;
        }
    }
    else {
        std::cerr << "无效的导入格式，期望NoldOJ或JudgeLite格式。" << std::endl;
        return 1;
    }
}

} // namespace problem
} // namespace judgelite

#endif // JUDGELITE_PROBLEM_H