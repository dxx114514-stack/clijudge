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
#include <map>
#include <utility>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include "json.hpp"
#include "platform.h"
#include "sandbox_runner.hpp"
#include "submit.h"
#include "judge.h"
#include "miniz/miniz.h"

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
    int subtaskId;      // 子任务编号
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
    std::string judgeDetail;  // resultToJson 序列化 (评测详情)
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
                {"answer_file_extension", "out"},
                {"source_file_name", ""},
                {"subtask_dependence", json::object()},
                {"interactor_code", ""},
                {"interactor_data", ""},
                {"interactor_name", ""},
                {"grader_files", json::object()},
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

    // 提交题目（使用 judge 模块评判）
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

        // 使用 judge 模块评判
        auto judgeResult = clijudge::judge::judgeSubmission(problem, filePath);

        // 转换结果
        sub.status = clijudge::judge::statusToAbbr(judgeResult.status);
        sub.score = judgeResult.totalScore;
        sub.timeUsed = judgeResult.totalTimeMs;
        sub.memoryUsed = judgeResult.maxMemoryKB;
        try {
            sub.judgeDetail = clijudge::judge::resultToJson(judgeResult).dump();
        } catch (...) {
        }

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
                    {"sort_order", tc.sortOrder},
                    {"subtask_id", tc.subtaskId}
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
        clijudge::platform::localTime(&now, &timeinfo);
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
    clijudge::platform::localTime(&now, &timeinfo);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return std::string(buf);
}

// ── ZIP 辅助 ─────────────────────────────────────────────────

// 从 ZIP 中读取指定条目内容
inline bool zipReadEntry(const std::string& zipPath, const std::string& entryName, std::string& out) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return false;
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, entryName.c_str(), &size, 0);
    mz_zip_reader_end(&zip);
    if (!data) return false;
    out.assign(static_cast<char*>(data), size);
    mz_free(data);
    return true;
}

// 列出 ZIP 中所有条目名
inline std::vector<std::string> zipListEntries(const std::string& zipPath) {
    std::vector<std::string> entries;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return entries;
    mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_is_directory) continue;
        entries.push_back(st.m_filename);
    }
    mz_zip_reader_end(&zip);
    return entries;
}

// 创建 ZIP 并写入多个条目（内存中的 name -> content 映射）
inline bool zipCreate(const std::string& zipPath,
                      const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, zipPath.c_str(), 0)) return false;
    bool ok = true;
    for (const auto& [name, content] : files) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            ok = false;
            break;
        }
    }
    if (ok) ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok) {
        std::error_code ec;
        fs::remove(zipPath, ec);
    }
    return ok;
}

inline int cmdCount(const std::string& dataDir) {
    ProblemStore store(dataDir);
    std::cout << store.count() << std::endl;
    return 0;
}

// 应用题型相关可选参数 (-type/-subtask-mode/-answer-ext/-source-name/
// -dependence/-interactor/-grader); 出错时打印并返回 false
inline bool applyTypeOptions(json& updates,
                             const std::string& problemType,
                             const std::string& answerExt,
                             const std::string& sourceName,
                             const std::string& subtaskMode,
                             const std::string& dependenceJson,
                             const std::string& interactorFile,
                             const std::string& graderDir) {
    if (!problemType.empty()) updates["problem_type"] = problemType;
    if (!answerExt.empty()) updates["answer_file_extension"] = answerExt;
    if (!sourceName.empty()) updates["source_file_name"] = sourceName;
    if (!subtaskMode.empty()) updates["subtask_mode"] = subtaskMode;
    if (!dependenceJson.empty()) {
        try {
            updates["subtask_dependence"] = json::parse(dependenceJson);
        } catch (...) {
            std::cerr << "Invalid dependence JSON: " << dependenceJson << std::endl;
            return false;
        }
    }
    if (!interactorFile.empty()) {
        if (!fs::exists(interactorFile)) {
            std::cerr << "Interactor file not found: " << interactorFile << std::endl;
            return false;
        }
        std::string ext = fs::path(interactorFile).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return (char)std::tolower(ch); });
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
            updates["interactor_code"] = readFileContent(interactorFile);
            if (updates["interactor_code"].get<std::string>().empty()) {
                std::cerr << "Failed to read interactor file: " << interactorFile << std::endl;
                return false;
            }
        } else {
            updates["interactor_data"] = fs::absolute(interactorFile).string();
        }
    }
    if (!graderDir.empty()) {
        json gf = json::object();
        std::error_code ec;
        fs::recursive_directory_iterator it(graderDir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            std::error_code rec;
            fs::path rel = fs::relative(it->path(), graderDir, rec);
            if (rec) continue;
            std::string key = rel.generic_string();
            gf[key] = readFileContent(it->path().string());
        }
        if (ec || gf.empty()) {
            std::cerr << "No grader files found in: " << graderDir << std::endl;
            return false;
        }
        updates["grader_files"] = gf;
    }
    return true;
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
                     double floatRelTol = 0.0,
                     const std::string& problemType = "",
                     const std::string& answerExt = "",
                     const std::string& sourceName = "",
                     const std::string& subtaskMode = "",
                     const std::string& dependenceJson = "",
                     const std::string& interactorFile = "",
                     const std::string& graderDir = "") {
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
    // 题型相关选项 (在 compare 之后应用, -type 优先于 -compare 的强制回退)
    if (!applyTypeOptions(updates, problemType, answerExt, sourceName, subtaskMode,
                          dependenceJson, interactorFile, graderDir)) {
        store.deleteProblem(id);
        return 1;
    }

    if (!updates.empty()) {
        store.edit(id, updates);
    }

    std::cout << "Problem created with ID: " << id << std::endl;
    return 0;
}

inline int cmdDelete(const std::string& dataDir, int id) {
    ProblemStore store(dataDir);
    if (store.deleteProblem(id)) {
        std::cout << "Problem " << id << " deleted." << std::endl;
        return 0;
    } else {
        std::cerr << "Problem " << id << " not found." << std::endl;
        return 1;
    }
}

inline int cmdView(const std::string& dataDir, int id) {
    ProblemStore store(dataDir);
    json problem = store.view(id);
    if (problem.is_null()) {
        std::cerr << "Problem " << id << " not found." << std::endl;
        return 1;
    }

    const auto& p = problem["problem"];

    // 格式化显示题面
    std::cout << "=== Problem " << p.value("id", 0) << " ===" << std::endl;
    std::cout << "Title: " << p.value("title", "") << std::endl;
    std::cout << std::endl;

    std::string desc = p.value("description", "");
    if (!desc.empty()) {
        std::cout << "## Description" << std::endl;
        std::cout << desc << std::endl;
        std::cout << std::endl;
    }

    std::string inputDesc = p.value("input_desc", "");
    if (!inputDesc.empty()) {
        std::cout << "## Input" << std::endl;
        std::cout << inputDesc << std::endl;
        std::cout << std::endl;
    }

    std::string outputDesc = p.value("output_desc", "");
    if (!outputDesc.empty()) {
        std::cout << "## Output" << std::endl;
        std::cout << outputDesc << std::endl;
        std::cout << std::endl;
    }

    std::string sampleIn = p.value("sample_input", "");
    std::string sampleOut = p.value("sample_output", "");
    if (!sampleIn.empty() || !sampleOut.empty()) {
        std::cout << "## Sample Input/Output" << std::endl;
        if (!sampleIn.empty()) {
            std::cout << "Input:" << std::endl;
            std::cout << "```" << std::endl;
            std::cout << sampleIn << std::endl;
            std::cout << "```" << std::endl;
        }
        if (!sampleOut.empty()) {
            std::cout << "Output:" << std::endl;
            std::cout << "```" << std::endl;
            std::cout << sampleOut << std::endl;
            std::cout << "```" << std::endl;
        }
        std::cout << std::endl;
    }

    std::string hint = p.value("hint", "");
    if (!hint.empty()) {
        std::cout << "## Hint" << std::endl;
        std::cout << hint << std::endl;
        std::cout << std::endl;
    }

    std::cout << "## Limits" << std::endl;
    std::cout << "Time Limit: " << p.value("time_limit", 1000) << " ms" << std::endl;
    std::cout << "Memory Limit: " << p.value("memory_limit", 256) << " MB" << std::endl;
    std::cout << "Public: " << (p.value("is_public", true) ? "Yes" : "No") << std::endl;
    std::cout << "Hidden: " << (p.value("is_hidden", false) ? "Yes" : "No") << std::endl;

    // Special Judge 相关信息
    std::string problemType = p.value("problem_type", "traditional");
    std::string compareMode = p.value("compare_mode", "text_strict");
    
    if (problemType != "traditional" || compareMode != "text_strict") {
        std::cout << std::endl;
        std::cout << "## Special Judge" << std::endl;
        std::cout << "Problem Type: " << problemType << std::endl;
        std::cout << "Compare Mode: " << compareMode << std::endl;
        
        if (compareMode == "float_abs" || compareMode == "float_rel" || compareMode == "float_all") {
            std::cout << "Float Abs Tolerance: " << p.value("float_abs_tolerance", 0.0) << std::endl;
            std::cout << "Float Rel Tolerance: " << p.value("float_rel_tolerance", 0.0) << std::endl;
        }
        
        std::string specialJudgeExe = p.value("special_judge_exe", "");
        if (!specialJudgeExe.empty()) {
            std::cout << "Special Judge Exe: " << specialJudgeExe << std::endl;
        }
        
        const auto& allowedLangs = p.value("allowed_languages", json::array());
        if (!allowedLangs.empty()) {
            std::cout << "Allowed Languages: ";
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
                    double floatRelTol = 0.0,
                    const std::string& problemType = "",
                    const std::string& answerExt = "",
                    const std::string& sourceName = "",
                    const std::string& subtaskMode = "",
                    const std::string& dependenceJson = "",
                    const std::string& interactorFile = "",
                    const std::string& graderDir = "") {
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
    // 题型相关选项 (在 compare 之后应用, -type 优先于 -compare 的强制回退)
    if (!applyTypeOptions(updates, problemType, answerExt, sourceName, subtaskMode,
                          dependenceJson, interactorFile, graderDir)) {
        return 1;
    }

    if (updates.empty()) {
        std::cerr << "No updates specified." << std::endl;
        return 1;
    }

    if (store.edit(id, updates)) {
        std::cout << "Problem " << id << " updated." << std::endl;
        return 0;
    } else {
        std::cerr << "Problem " << id << " not found." << std::endl;
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

    std::string pTitle;
    json pj = store.view(problemId);
    if (!pj.is_null() && pj.contains("problem")) pTitle = pj["problem"].value("title", "");

    clijudge::submit::addSubmission(dataDir, problemId, pTitle, filePath,
                                     submission.status, submission.score,
                                     submission.timeUsed, submission.memoryUsed,
                                     username, submission.judgeDetail);

    std::cout << "=== Submission Result ===" << std::endl;
    std::cout << "Status: " << submission.status << std::endl;
    std::cout << "Score: " << submission.score << std::endl;
    std::cout << "Time: " << submission.timeUsed << " ms" << std::endl;
    std::cout << "Memory: " << submission.memoryUsed << " KB" << std::endl;
    std::cout << "User: " << (username.empty() ? "unknown" : username) << std::endl;

    return (submission.status == "AC") ? 0 : 1;
}

// 重新评判提交 (上限 settings.max_rejudge_times)
inline int cmdRejudge(const std::string& dataDir, int submissionId) {
    clijudge::submit::SubmitStore sstore(dataDir);
    json sub = sstore.getSubmission(submissionId);
    if (sub.is_null()) {
        std::cerr << "Submission " << submissionId << " not found." << std::endl;
        return 1;
    }
    int problemId = sub.value("problem_id", 0);
    std::string filePath = sub.value("file_path", "");
    int judgeTimes = sub.value("judge_times", 1);
    int maxTimes = clijudge::settings::getMaxRejudgeTimes();
    if (judgeTimes > maxTimes) {
        std::cerr << "Max rejudge times reached (" << maxTimes << ")." << std::endl;
        return 1;
    }

    ProblemStore store(dataDir);
    json problem = store.view(problemId);
    if (problem.is_null()) {
        std::cerr << "Problem " << problemId << " not found." << std::endl;
        return 1;
    }
    if (!fs::exists(filePath)) {
        std::cerr << "Submission file not found: " << filePath << std::endl;
        return 1;
    }

    auto submission = store.submit(problemId, filePath);

    json upd;
    upd["status"] = submission.status;
    upd["score"] = submission.score;
    upd["time_used"] = submission.timeUsed;
    upd["memory_used"] = submission.memoryUsed;
    upd["judge_times"] = judgeTimes + 1;
    upd["judged_at"] = clijudge::submit::getCurrentTime();
    if (!submission.judgeDetail.empty()) {
        try {
            upd["judge_detail"] = json::parse(submission.judgeDetail);
        } catch (...) {
        }
    }
    sstore.updateSubmission(submissionId, upd);

    std::cout << "=== Rejudge Result ===" << std::endl;
    std::cout << "Submission: " << submissionId << std::endl;
    std::cout << "Status: " << submission.status << std::endl;
    std::cout << "Score: " << submission.score << std::endl;
    std::cout << "Time: " << submission.timeUsed << " ms" << std::endl;
    std::cout << "Memory: " << submission.memoryUsed << " KB" << std::endl;
    std::cout << "Judge times: " << (judgeTimes + 1) << std::endl;
    return (submission.status == "AC") ? 0 : 1;
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
        std::cerr << "Failed to read input file: " << inputData << std::endl;
        return 1;
    }
    std::string outContent = readFileContent(outputData);
    if (outContent.empty()) {
        std::cerr << "Failed to read output file: " << outputData << std::endl;
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
    tc.subtaskId = 1;  // 默认子任务为1

    if (store.addTestCase(problemId, tc)) {
        std::cout << "Test case created with ID: " << tc.id << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to create test case." << std::endl;
        return 1;
    }
}

// 从 ZIP 导入测试数据（配对 *.in 与 *.out/*.ans）
inline int cmdTestDataImportZip(const std::string& dataDir, int problemId,
                                const std::string& zipPath,
                                int timeLimit = -1, int memoryLimit = -1, int score = 25) {
    if (!fs::exists(zipPath)) {
        std::cerr << "Zip file not found: " << zipPath << std::endl;
        return 1;
    }

    std::vector<std::string> entries = zipListEntries(zipPath);
    if (entries.empty()) {
        std::cerr << "No entries found in zip: " << zipPath << std::endl;
        return 1;
    }

    // 收集 .in 文件，配对同名 .out/.ans
    std::map<std::string, std::string> inFiles;   // basename -> entry
    std::map<std::string, std::string> outFiles;  // basename -> entry
    for (const auto& entry : entries) {
        std::string name = fs::path(entry).filename().string();
        std::string stem = fs::path(name).stem().string();
        std::string ext = fs::path(name).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".in") {
            inFiles[stem] = entry;
        } else if (ext == ".out" || ext == ".ans") {
            outFiles[stem] = entry;
        }
    }

    if (inFiles.empty()) {
        std::cerr << "No .in files found in zip." << std::endl;
        return 1;
    }

    ProblemStore store(dataDir);
    json testCases = store.getTestCases(problemId);
    int nextId = testCases.empty() ? 1 : testCases.back().value("id", 0) + 1;
    int sortOrder = nextId;
    int imported = 0;

    for (const auto& [stem, inEntry] : inFiles) {
        auto outIt = outFiles.find(stem);
        if (outIt == outFiles.end()) {
            std::cerr << "Warning: no matching .out/.ans for " << stem << ", skipped." << std::endl;
            continue;
        }

        std::string inContent, outContent;
        if (!zipReadEntry(zipPath, inEntry, inContent) ||
            !zipReadEntry(zipPath, outIt->second, outContent)) {
            std::cerr << "Warning: failed to extract " << stem << ", skipped." << std::endl;
            continue;
        }

        TestCase tc;
        tc.id = nextId++;
        tc.inputData = inContent;
        tc.outputData = outContent;
        tc.inputFile = "";
        tc.outputFile = "";
        tc.score = score;
        tc.timeLimit = timeLimit;
        tc.memoryLimit = memoryLimit;
        tc.sortOrder = sortOrder++;
        tc.subtaskId = 1;

        if (store.addTestCase(problemId, tc)) {
            std::cout << "Test case imported: " << stem << " (ID: " << tc.id << ")" << std::endl;
            imported++;
        }
    }

    if (imported == 0) {
        std::cerr << "No test cases imported." << std::endl;
        return 1;
    }
    std::cout << "Imported " << imported << " test case(s)." << std::endl;
    return 0;
}

inline int cmdTestDataDelete(const std::string& dataDir, int problemId, int testCaseId) {
    ProblemStore store(dataDir);
    if (store.deleteTestCase(problemId, testCaseId)) {
        std::cout << "Test case " << testCaseId << " deleted." << std::endl;
        return 0;
    } else {
        std::cerr << "Test case " << testCaseId << " not found." << std::endl;
        return 1;
    }
}

inline int cmdTestDataSetAll(const std::string& dataDir, int problemId,
                             int timeLimit = -1, int memoryLimit = -1, int score = -1) {
    ProblemStore store(dataDir);
    if (store.setAllTestCaseDefaults(problemId, timeLimit, memoryLimit, score)) {
        std::cout << "All test cases updated." << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to update test cases." << std::endl;
        return 1;
    }
}

inline int cmdExport(const std::string& dataDir, int problemId, const std::string& outputPath) {
    ProblemStore store(dataDir);
    json problem = store.exportProblem(problemId);
    if (problem.is_null()) {
        std::cerr << "Problem " << problemId << " not found." << std::endl;
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
        // ZIP 格式导出：problem.json + testdata/*.in|*.out
        json zipData = exportData;
        std::vector<std::pair<std::string, std::string>> files;

        json zipTestCases = json::array();
        for (const auto& tc : zipData["test_cases"]) {
            json tcCopy = tc;
            std::string name = tcCopy.value("name", "case");
            std::string inData = tcCopy.value("input_data", "");
            std::string outData = tcCopy.value("output_data", "");
            tcCopy.erase("input_data");
            tcCopy.erase("output_data");
            tcCopy["input_file"] = "testdata/" + name + ".in";
            tcCopy["output_file"] = "testdata/" + name + ".out";
            zipTestCases.push_back(tcCopy);
            files.emplace_back("testdata/" + name + ".in", inData);
            files.emplace_back("testdata/" + name + ".out", outData);
        }
        zipData["test_cases"] = zipTestCases;

        std::string jsonContent = zipData.dump(2);
        files.insert(files.begin(), {"problem.json", jsonContent});

        if (zipCreate(outputPath, files)) {
            std::cout << "Problem exported to: " << outputPath << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to create export file: " << outputPath << std::endl;
            return 1;
        }
    }

    // JSON 格式导出
    std::ofstream f(outputPath);
    if (f.is_open()) {
        f << exportData.dump(2);
        f.close();
        std::cout << "Problem exported to: " << outputPath << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to create export file." << std::endl;
        return 1;
    }
}

inline int cmdImport(const std::string& dataDir, const std::string& importPath) {
    if (!fs::exists(importPath)) {
        std::cerr << "Import file not found: " << importPath << std::endl;
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
        // ZIP 格式导入：读取 problem.json 并解析 testdata 引用
        std::string jsonContent;
        if (!zipReadEntry(importPath, "problem.json", jsonContent)) {
            std::cerr << "No problem.json found in zip: " << importPath << std::endl;
            return 1;
        }

        json problemData;
        try {
            problemData = json::parse(jsonContent);
        } catch (const json::parse_error& e) {
            std::cerr << "Failed to parse problem.json in zip: " << e.what() << std::endl;
            return 1;
        }

        // 解析 input_file/output_file 引用
        if (problemData.contains("test_cases")) {
            for (auto& tc : problemData["test_cases"]) {
                std::string inData = tc.value("input_data", "");
                std::string outData = tc.value("output_data", "");
                if (inData.empty() && tc.contains("input_file")) {
                    std::string entry = tc["input_file"].get<std::string>();
                    std::string content;
                    if (zipReadEntry(importPath, entry, content)) {
                        inData = content;
                    } else {
                        std::cerr << "Missing zip entry: " << entry << std::endl;
                        return 1;
                    }
                }
                if (outData.empty() && tc.contains("output_file")) {
                    std::string entry = tc["output_file"].get<std::string>();
                    std::string content;
                    if (zipReadEntry(importPath, entry, content)) {
                        outData = content;
                    } else {
                        std::cerr << "Missing zip entry: " << entry << std::endl;
                        return 1;
                    }
                }
                tc["input_data"] = inData;
                tc["output_data"] = outData;
                tc.erase("input_file");
                tc.erase("output_file");
            }
        }

        ProblemStore store(dataDir);
        int id = store.importProblem(problemData);
        if (id > 0) {
            std::cout << "Problem imported with ID: " << id << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to import problem." << std::endl;
            return 1;
        }
    }

    std::ifstream f(importPath);
    if (!f.is_open()) {
        std::cerr << "Failed to open import file." << std::endl;
        return 1;
    }

    json problemData;
    try {
        f >> problemData;
        f.close();
    } catch (const json::parse_error& e) {
        std::cerr << "Failed to parse import file: " << e.what() << std::endl;
        return 1;
    }

    // 支持 NoldOJ 兼容格式（带 version 字段）
    if (problemData.contains("version") && problemData.contains("problem")) {
        // NoldOJ 格式
        ProblemStore store(dataDir);
        int id = store.importProblem(problemData);
        if (id > 0) {
            std::cout << "Problem imported with ID: " << id << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to import problem." << std::endl;
            return 1;
        }
    }
    // 支持旧格式（直接包含 problem 和 test_cases）
    else if (problemData.contains("problem") && problemData.contains("test_cases")) {
        ProblemStore store(dataDir);
        int id = store.importProblem(problemData);
        if (id > 0) {
            std::cout << "Problem imported with ID: " << id << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to import problem." << std::endl;
            return 1;
        }
    }
    else {
        std::cerr << "Invalid import format. Expected NoldOJ or JudgeLite format." << std::endl;
        return 1;
    }
}

} // namespace problem
} // namespace judgelite

#endif // JUDGELITE_PROBLEM_H