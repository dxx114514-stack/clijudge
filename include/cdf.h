#ifndef JUDGELITE_CDF_H
#define JUDGELITE_CDF_H

// cdf.h
// LemonLime CDF (Contest Data Format) 导入导出支持
//
// CDF 格式版本: "1.0"
// MIME: application/x-lemon-contest
//
// 支持的 TaskType:
//   0 - Traditional (传统编译运行)
//   1 - AnswersOnly (仅提交答案)
//   2 - Interaction (交互题)
//   3 - Communication (通信题)
//   4 - CommunicationExec (通信题可执行)
//
// 支持的 ComparisonMode:
//   0 - LineByLineMode (逐行精确比较)
//   1 - IgnoreSpacesMode (忽略空格)
//   2 - ExternalToolMode (外部工具)
//   3 - RealNumberMode (实数比较)
//   4 - LemonSpecialJudgeMode (Lemon 特殊评测)
//   5 - TestlibSpecialJudgeMode (Testlib 特殊评测)

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "json.hpp"

namespace judgelite {
namespace cdf {

using json = nlohmann::json;
namespace fs = std::filesystem;

// CDF TaskType 枚举
enum class TaskType : int {
    Traditional = 0,
    AnswersOnly = 1,
    Interaction = 2,
    Communication = 3,
    CommunicationExec = 4
};

// CDF ComparisonMode 枚举
enum class ComparisonMode : int {
    LineByLine = 0,
    IgnoreSpaces = 1,
    ExternalTool = 2,
    RealNumber = 3,
    LemonSpecialJudge = 4,
    TestlibSpecialJudge = 5
};

// CDF 测试点
struct CdfTestCase {
    int fullScore;
    int timeLimit;
    int memoryLimit;
    std::vector<std::string> inputFiles;
    std::vector<std::string> outputFiles;
    std::vector<int> dependenceSubtask;
};

// CDF 题目
struct CdfTask {
    std::string problemTitle;
    std::string sourceFileName;
    std::string inputFileName;
    std::string outputFileName;
    bool standardInputCheck;
    bool standardOutputCheck;
    int taskType;
    bool subFolderCheck;
    int comparisonMode;
    std::string diffArguments;
    int realPrecision;
    std::string specialJudge;
    std::string answerFileExtension;
    json compilerConfiguration;
    std::vector<CdfTestCase> testCases;
    // Interaction / Communication 相关
    std::string interactor;
    std::string grader;
    std::string interactorName;
    std::vector<std::string> sourceFilesPath;
    std::vector<std::string> sourceFilesName;
    std::vector<std::string> graderFilesPath;
    std::vector<std::string> graderFilesName;
};

// CDF 比赛
struct CdfContest {
    std::string version;
    std::string contestTitle;
    std::vector<CdfTask> tasks;
};

// 读取文件内容
inline std::string readFileContent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 解析 CDF TestCase (依赖标记条目: "<子任务号>_lemon_SUbtaskDEPENDENCE_fLAg")
inline CdfTestCase parseTestCase(const json& j) {
    CdfTestCase tc;
    tc.fullScore = j.value("fullScore", 0);
    tc.timeLimit = j.value("timeLimit", 1000);
    tc.memoryLimit = j.value("memoryLimit", 256);
    tc.inputFiles = j.value("inputFiles", json::array()).get<std::vector<std::string>>();
    tc.outputFiles = j.value("outputFiles", json::array()).get<std::vector<std::string>>();

    std::string flag = "_lemon_SUbtaskDEPENDENCE_fLAg";
    std::vector<std::string> inputs = tc.inputFiles;
    tc.inputFiles.clear();
    for (const auto& s : inputs) {
        if (s.size() > flag.size() && s.compare(s.size() - flag.size(), flag.size(), flag) == 0) {
            std::string num = s.substr(0, s.size() - flag.size());
            try {
                tc.dependenceSubtask.push_back(std::stoi(num));
            } catch (...) {
            }
        } else {
            tc.inputFiles.push_back(s);
        }
    }

    return tc;
}

// 解析 CDF Task
inline CdfTask parseTask(const json& j) {
    CdfTask task;
    task.problemTitle = j.value("problemTitle", "");
    task.sourceFileName = j.value("sourceFileName", "");
    task.inputFileName = j.value("inputFileName", "");
    task.outputFileName = j.value("outputFileName", "");
    task.standardInputCheck = j.value("standardInputCheck", true);
    task.standardOutputCheck = j.value("standardOutputCheck", true);
    task.taskType = j.value("taskType", 0);
    task.subFolderCheck = j.value("subFolderCheck", false);
    task.comparisonMode = j.value("comparisonMode", 1);
    task.diffArguments = j.value("diffArguments", "");
    task.realPrecision = j.value("realPrecision", 3);
    task.specialJudge = j.value("specialJudge", "");
    task.answerFileExtension = j.value("answerFileExtension", "out");
    task.compilerConfiguration = j.value("compilerConfiguration", json::object());

    // 解析测试点
    if (j.contains("testCases")) {
        for (const auto& tc : j["testCases"]) {
            task.testCases.push_back(parseTestCase(tc));
        }
    }

    // Interaction / Communication 字段
    task.interactor = j.value("interactor", "");
    task.grader = j.value("grader", "");
    task.interactorName = j.value("interactorName", "");
    task.sourceFilesPath = j.value("sourceFilesPath", json::array()).get<std::vector<std::string>>();
    task.sourceFilesName = j.value("sourceFilesName", json::array()).get<std::vector<std::string>>();
    task.graderFilesPath = j.value("graderFilesPath", json::array()).get<std::vector<std::string>>();
    task.graderFilesName = j.value("graderFilesName", json::array()).get<std::vector<std::string>>();

    return task;
}

// 解析 CDF 文件
inline CdfContest parseCdf(const std::string& cdfPath) {
    CdfContest contest;
    std::ifstream f(cdfPath);
    if (!f.is_open()) {
        std::cerr << "无法打开 CDF 文件: " << cdfPath << std::endl;
        return contest;
    }

    json data;
    try {
        f >> data;
    } catch (const json::parse_error& e) {
        std::cerr << "解析 CDF 文件失败: " << e.what() << std::endl;
        return contest;
    }

    contest.version = data.value("version", "1.0");
    contest.contestTitle = data.value("contestTitle", "");

    if (data.contains("tasks")) {
        for (const auto& task : data["tasks"]) {
            contest.tasks.push_back(parseTask(task));
        }
    }

    return contest;
}

// CDF ComparisonMode 转 JudgeLite compare_mode
inline std::string comparisonModeToJudgeLite(int mode) {
    switch (mode) {
        case 0: return "text_strict";
        case 1: return "text_no_space";
        case 2: return "text_no_space";  // 外部工具近似为忽略空格
        case 3: return "float_all";
        case 4: return "spj_lemon";
        case 5: return "spj_testlib";
        default: return "text_strict";
    }
}

// JudgeLite compare_mode 转 CDF ComparisonMode (导出用)
inline int judgeLiteToComparisonMode(const std::string& mode) {
    if (mode == "text_no_space") return 1;
    if (mode == "text_line") return 0;
    if (mode == "float_abs" || mode == "float_rel" || mode == "float_all") return 3;
    if (mode == "spj_testlib") return 5;
    // spj / spj_lemon / 未知 → LemonSpecialJudge
    if (mode == "text_strict") return 0;
    return 4;
}

// CDF TaskType 转 JudgeLite problem_type
inline std::string taskTypeToJudgeLite(int type) {
    switch (type) {
        case 0: return "traditional";
        case 1: return "answers_only";
        case 2: return "interaction";
        case 3: return "communication";
        case 4: return "communication_exec";
        default: return "traditional";
    }
}

// JudgeLite problem_type 转 CDF TaskType (导出用)
inline int judgeLiteToTaskType(const std::string& type) {
    if (type == "answers_only") return 1;
    if (type == "interaction") return 2;
    if (type == "communication") return 3;
    if (type == "communication_exec") return 4;
    return 0;
}

// 生成 CDF TestCase JSON
inline json testCaseToJson(const CdfTestCase& tc) {
    json j;
    j["fullScore"] = tc.fullScore;
    j["timeLimit"] = tc.timeLimit;
    j["memoryLimit"] = tc.memoryLimit;
    j["inputFiles"] = tc.inputFiles;
    j["outputFiles"] = tc.outputFiles;
    return j;
}

// 生成 CDF Task JSON
inline json taskToJson(const CdfTask& task) {
    json j;
    j["problemTitle"] = task.problemTitle;
    j["sourceFileName"] = task.sourceFileName;
    j["inputFileName"] = task.inputFileName;
    j["outputFileName"] = task.outputFileName;
    j["standardInputCheck"] = task.standardInputCheck;
    j["standardOutputCheck"] = task.standardOutputCheck;
    j["taskType"] = task.taskType;
    j["subFolderCheck"] = task.subFolderCheck;
    j["comparisonMode"] = task.comparisonMode;
    j["diffArguments"] = task.diffArguments;
    j["realPrecision"] = task.realPrecision;
    j["specialJudge"] = task.specialJudge;
    j["answerFileExtension"] = task.answerFileExtension;
    j["compilerConfiguration"] = task.compilerConfiguration;

    json testCases = json::array();
    for (const auto& tc : task.testCases) {
        testCases.push_back(testCaseToJson(tc));
    }
    j["testCases"] = testCases;

    // Interaction / Communication 字段
    if (!task.interactor.empty()) j["interactor"] = task.interactor;
    if (!task.grader.empty()) j["grader"] = task.grader;
    if (!task.interactorName.empty()) j["interactorName"] = task.interactorName;
    if (!task.sourceFilesPath.empty()) j["sourceFilesPath"] = task.sourceFilesPath;
    if (!task.sourceFilesName.empty()) j["sourceFilesName"] = task.sourceFilesName;
    if (!task.graderFilesPath.empty()) j["graderFilesPath"] = task.graderFilesPath;
    if (!task.graderFilesName.empty()) j["graderFilesName"] = task.graderFilesName;

    return j;
}

} // namespace cdf
} // namespace judgelite

#endif // JUDGELITE_CDF_H
