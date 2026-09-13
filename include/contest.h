#ifndef JUDGELITE_CONTEST_H
#define JUDGELITE_CONTEST_H

// contest.h
// JudgeLite 比赛管理子命令
//
// 子命令:
//   create [标题] [开始时间] [结束时间] [题目1] [题目2] - 创建比赛
//   delete [编号] - 删除比赛
//   problem [编号] [题目在比赛中的编号]
//     submit [文件地址] - 提交题目
//     view - 查看题目
//   view [编号] - 查看比赛

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "json.hpp"
#include "cdf.h"
#include "problem.h"

namespace judgelite {
namespace contest {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 比赛数据结构
struct Contest {
    int id;
    std::string title;
    std::string startTime;
    std::string endTime;
    std::vector<int> problemIds;  // 题目ID列表
    std::string createdAt;
};

// 比赛题目提交记录
struct ContestSubmission {
    int contestId;
    int problemIndex;  // 题目在比赛中的编号（从1开始）
    int problemId;     // 实际题目ID
    std::string filePath;
    std::string submittedAt;
    std::string result;
};

// 数据存储类
class ContestStore {
private:
    std::string dataDir;
    json data;

    void ensureDataDir() {
        if (!fs::exists(dataDir)) {
            fs::create_directories(dataDir);
        }
    }

    std::string getContestPath(int id) {
        return dataDir + "/contest_" + std::to_string(id) + ".json";
    }

    void loadIndex() {
        std::string indexPath = dataDir + "/contests.json";
        if (fs::exists(indexPath)) {
            std::ifstream f(indexPath);
            if (f.is_open()) {
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
        std::string indexPath = dataDir + "/contests.json";
        std::ofstream f(indexPath);
        if (f.is_open()) {
            f << data.dump(2);
        }
    }

    int getNextId() {
        int maxId = 0;
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() > maxId) {
                maxId = item["id"].get<int>();
            }
        }
        return maxId + 1;
    }

public:
    ContestStore(const std::string& dir) : dataDir(dir) {
        loadIndex();
    }

    // 创建比赛
    int create(const std::string& title, const std::string& startTime,
               const std::string& endTime, const std::vector<int>& problemIds) {
        int id = getNextId();

        json contest = {
            {"id", id},
            {"title", title},
            {"start_time", startTime},
            {"end_time", endTime},
            {"problem_ids", problemIds},
            {"created_at", getCurrentTime()}
        };

        data.push_back(contest);
        saveIndex();

        // 保存比赛详情到单独文件
        ensureDataDir();
        std::ofstream f(getContestPath(id));
        if (f.is_open()) {
            f << contest.dump(2);
        }

        return id;
    }

    // 删除比赛
    bool deleteContest(int id) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it->contains("id") && (*it)["id"].get<int>() == id) {
                data.erase(it);
                saveIndex();

                // 删除比赛文件
                std::string path = getContestPath(id);
                if (fs::exists(path)) {
                    fs::remove(path);
                }
                return true;
            }
        }
        return false;
    }

    // 查看比赛
    json view(int id) {
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() == id) {
                // 加载完整详情
                std::string path = getContestPath(id);
                if (fs::exists(path)) {
                    std::ifstream f(path);
                    if (f.is_open()) {
                        json fullContest;
                        f >> fullContest;
                        return fullContest;
                    }
                }
                return item;
            }
        }
        return nullptr;
    }

    // 获取比赛中的题目ID
    int getContestProblemId(int contestId, int problemIndex) {
        json contest = view(contestId);
        if (contest.is_null()) return -1;

        if (contest.contains("problem_ids")) {
            const auto& problemIds = contest["problem_ids"];
            if (problemIndex >= 1 && problemIndex <= (int)problemIds.size()) {
                return problemIds[problemIndex - 1].get<int>();
            }
        }
        return -1;
    }

    // 提交比赛题目
    bool submitProblem(int contestId, int problemIndex, const std::string& filePath,
                       const std::string& username = "") {
        int problemId = getContestProblemId(contestId, problemIndex);
        if (problemId < 0) return false;

        // 加载题目数据进行评判
        std::string problemPath = dataDir + "/problem_" + std::to_string(problemId) + ".json";
        json problemData;
        if (fs::exists(problemPath)) {
            std::ifstream pf(problemPath);
            if (pf.is_open()) {
                pf >> problemData;
            }
        }
        
        // 评判
        std::string result = "pending";
        int score = 0;
        int timeUsed = 0;
        int memoryUsed = 0;
        
        if (!problemData.is_null() && fs::exists(filePath)) {
            auto judgeResult = clijudge::judge::judgeSubmission(problemData, filePath);
            result = clijudge::judge::statusToAbbr(judgeResult.status);
            score = judgeResult.totalScore;
            timeUsed = judgeResult.totalTimeMs;
            memoryUsed = judgeResult.maxMemoryKB;
        }

        // 保存提交记录
        json submission = {
            {"contest_id", contestId},
            {"problem_index", problemIndex},
            {"problem_id", problemId},
            {"file_path", filePath},
            {"submitted_at", getCurrentTime()},
            {"result", result},
            {"score", score},
            {"time_used", timeUsed},
            {"memory_used", memoryUsed},
            {"username", username.empty() ? "unknown" : username}
        };

        ensureDataDir();
        std::string submissionPath = dataDir + "/contest_" + std::to_string(contestId) +
                                     "_problem_" + std::to_string(problemIndex) + "_submissions.json";

        json submissions = json::array();
        if (fs::exists(submissionPath)) {
            std::ifstream f(submissionPath);
            if (f.is_open()) {
                f >> submissions;
            }
        }
        submissions.push_back(submission);

        std::ofstream f(submissionPath);
        if (f.is_open()) {
            f << submissions.dump(2);
        }

        return true;
    }

    // 查看比赛题目提交记录
    json viewProblemSubmissions(int contestId, int problemIndex) {
        std::string submissionPath = dataDir + "/contest_" + std::to_string(contestId) +
                                     "_problem_" + std::to_string(problemIndex) + "_submissions.json";

        if (fs::exists(submissionPath)) {
            std::ifstream f(submissionPath);
            if (f.is_open()) {
                json submissions;
                f >> submissions;
                return submissions;
            }
        }
        return json::array();
    }

    // 列出所有比赛
    json list() {
        return data;
    }

    // 导入 CDF 格式比赛
    int importCdf(const std::string& cdfPath, const std::string& dataDir) {
        auto cdf = judgelite::cdf::parseCdf(cdfPath);
        if (cdf.tasks.empty()) {
            std::cerr << "CDF 文件中没有题目。" << std::endl;
            return -1;
        }

        // 获取 CDF 数据目录（cdf 文件所在目录下的 data/ 子目录）
        fs::path cdfDir = fs::path(cdfPath).parent_path();
        fs::path cdfDataDir = cdfDir / "data";

        // 创建 ProblemStore 来导入题目
        judgelite::problem::ProblemStore problemStore(dataDir);
        std::vector<int> problemIds;

        for (const auto& task : cdf.tasks) {
            // 构建题目 JSON
            json problemJson;
            problemJson["problem"]["title"] = task.problemTitle;
            problemJson["problem"]["time_limit"] = task.testCases.empty() ? 1000 : task.testCases[0].timeLimit;
            problemJson["problem"]["memory_limit"] = task.testCases.empty() ? 256 : task.testCases[0].memoryLimit;
            problemJson["problem"]["compare_mode"] = judgelite::cdf::comparisonModeToJudgeLite(task.comparisonMode);
            problemJson["problem"]["problem_type"] = judgelite::cdf::taskTypeToJudgeLite(task.taskType);
            problemJson["problem"]["is_public"] = true;
            problemJson["problem"]["is_hidden"] = false;
            problemJson["problem"]["spj_code"] = "";
            problemJson["problem"]["special_judge_exe"] = task.specialJudge;
            problemJson["problem"]["allowed_languages"] = json::array();
            problemJson["problem"]["subtask_mode"] = "simple";
            problemJson["problem"]["description"] = "";
            problemJson["problem"]["input_desc"] = "";
            problemJson["problem"]["output_desc"] = "";
            problemJson["problem"]["hint"] = "";
            problemJson["problem"]["sample_input"] = "";
            problemJson["problem"]["sample_output"] = "";

            if (task.comparisonMode == 3) {
                problemJson["problem"]["float_abs_tolerance"] = 0.0;
                problemJson["problem"]["float_rel_tolerance"] = 0.0;
            }

            // 构建测试用例
            json testCases = json::array();
            for (size_t i = 0; i < task.testCases.size(); i++) {
                const auto& tc = task.testCases[i];
                json tcJson;
                tcJson["id"] = (int)i + 1;
                tcJson["score"] = tc.fullScore;
                tcJson["time_limit"] = -1;
                tcJson["memory_limit"] = -1;
                tcJson["sort_order"] = (int)i + 1;
                tcJson["input_file"] = "";
                tcJson["output_file"] = "";

                // 读取输入文件
                std::string inputData;
                for (const auto& inputFile : tc.inputFiles) {
                    fs::path inputPath = cdfDataDir / inputFile;
                    if (fs::exists(inputPath)) {
                        inputData += judgelite::cdf::readFileContent(inputPath.string());
                    }
                }
                tcJson["input_data"] = inputData;

                // 读取输出文件
                std::string outputData;
                for (const auto& outputFile : tc.outputFiles) {
                    fs::path outputPath = cdfDataDir / outputFile;
                    if (fs::exists(outputPath)) {
                        outputData += judgelite::cdf::readFileContent(outputPath.string());
                    }
                }
                tcJson["output_data"] = outputData;

                testCases.push_back(tcJson);
            }
            problemJson["test_cases"] = testCases;

            // 导入题目
            int problemId = problemStore.importProblem(problemJson);
            if (problemId > 0) {
                problemIds.push_back(problemId);
                std::cout << "  导入题目: " << task.problemTitle << " (ID: " << problemId << ")" << std::endl;
            } else {
                std::cerr << "  导入题目失败: " << task.problemTitle << std::endl;
            }
        }

        // 创建比赛
        if (problemIds.empty()) {
            std::cerr << "没有成功导入任何题目。" << std::endl;
            return -1;
        }

        int contestId = create(cdf.contestTitle, "", "", problemIds);
        std::cout << "比赛已导入: " << cdf.contestTitle << " (ID: " << contestId << ")" << std::endl;
        return contestId;
    }

    // 导出为 CDF 格式
    json exportCdf(int contestId) {
        json contest = view(contestId);
        if (contest.is_null()) return nullptr;

        judgelite::problem::ProblemStore problemStore(dataDir);

        json cdf;
        cdf["version"] = "1.0";
        cdf["contestTitle"] = contest.value("title", "");

        json tasks = json::array();
        if (contest.contains("problem_ids")) {
            int idx = 1;
            for (const auto& pid : contest["problem_ids"]) {
                int problemId = pid.get<int>();
                json problem = problemStore.view(problemId);
                if (problem.is_null()) continue;

                const auto& p = problem["problem"];
                const auto& testCases = problem.value("test_cases", json::array());

                json task;
                task["problemTitle"] = p.value("title", "");
                task["sourceFileName"] = p.value("title", "solution");
                task["inputFileName"] = p.value("title", "input") + ".in";
                task["outputFileName"] = p.value("title", "output") + ".out";
                task["standardInputCheck"] = true;
                task["standardOutputCheck"] = true;
                task["taskType"] = 0;
                task["subFolderCheck"] = false;

                // 映射 comparisonMode
                std::string compareMode = p.value("compare_mode", "text_strict");
                if (compareMode == "text_strict") task["comparisonMode"] = 0;
                else if (compareMode == "text_no_space") task["comparisonMode"] = 1;
                else if (compareMode == "float_all" || compareMode == "float_abs" || compareMode == "float_rel") task["comparisonMode"] = 3;
                else task["comparisonMode"] = 4;

                task["diffArguments"] = "--ignore-space-change --text --brief";
                task["realPrecision"] = 3;
                task["specialJudge"] = p.value("special_judge_exe", "");
                task["answerFileExtension"] = "out";
                task["compilerConfiguration"] = json::object();

                json cdfTestCases = json::array();
                int caseIdx = 1;
                for (const auto& tc : testCases) {
                    json cdfTc;
                    cdfTc["fullScore"] = tc.value("score", 0);
                    cdfTc["timeLimit"] = p.value("time_limit", 1000);
                    cdfTc["memoryLimit"] = p.value("memory_limit", 256);
                    cdfTc["inputFiles"] = json::array({task["sourceFileName"].get<std::string>() + "/" + std::to_string(caseIdx) + ".in"});
                    cdfTc["outputFiles"] = json::array({task["sourceFileName"].get<std::string>() + "/" + std::to_string(caseIdx) + ".ans"});
                    cdfTestCases.push_back(cdfTc);
                    caseIdx++;
                }
                task["testCases"] = cdfTestCases;

                tasks.push_back(task);
                idx++;
            }
        }
        cdf["tasks"] = tasks;
        cdf["contestants"] = json::array();

        return cdf;
    }

    // 获取比赛排行榜
    json leaderboard(int contestId) {
        json contest = view(contestId);
        if (contest.is_null()) return nullptr;

        json rankings = json::array();

        if (contest.contains("problem_ids")) {
            const auto& problemIds = contest["problem_ids"];
            int numProblems = (int)problemIds.size();

            // 收集所有提交
            std::map<std::string, json> userStats;
            for (int i = 1; i <= numProblems; i++) {
                json submissions = viewProblemSubmissions(contestId, i);
                if (!submissions.is_array()) continue;
                for (const auto& sub : submissions) {
                    std::string user = sub.value("username", "unknown");
                    if (userStats.find(user) == userStats.end()) {
                        userStats[user] = {
                            {"username", user},
                            {"score", 0},
                            {"accepted", 0},
                            {"total", 0}
                        };
                    }
                    userStats[user]["total"] = userStats[user]["total"].get<int>() + 1;
                    std::string result = sub.value("result", "");
                    int score = sub.value("score", 0);
                    if (result == "AC" || result == "accepted") {
                        userStats[user]["score"] = userStats[user]["score"].get<int>() + score;
                        userStats[user]["accepted"] = userStats[user]["accepted"].get<int>() + 1;
                    } else {
                        userStats[user]["score"] = userStats[user]["score"].get<int>() + score;
                    }
                }
            }

            // 转为数组并排序
            for (auto& [key, val] : userStats) {
                rankings.push_back(val);
            }
            std::sort(rankings.begin(), rankings.end(),
                [](const json& a, const json& b) {
                    return a["score"].get<int>() > b["score"].get<int>();
                });
        }

        return rankings;
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
inline int cmdCreate(const std::string& dataDir, const std::string& title,
                     const std::string& startTime, const std::string& endTime,
                     const std::vector<int>& problemIds) {
    ContestStore store(dataDir);
    int id = store.create(title, startTime, endTime, problemIds);
    std::cout << "Contest created with ID: " << id << std::endl;
    return 0;
}

inline int cmdDelete(const std::string& dataDir, int id) {
    ContestStore store(dataDir);
    if (store.deleteContest(id)) {
        std::cout << "Contest " << id << " deleted." << std::endl;
        return 0;
    } else {
        std::cerr << "Contest " << id << " not found." << std::endl;
        return 1;
    }
}

inline int cmdView(const std::string& dataDir, int id) {
    ContestStore store(dataDir);
    json contest = store.view(id);
    if (contest.is_null()) {
        std::cerr << "Contest " << id << " not found." << std::endl;
        return 1;
    }
    std::cout << contest.dump(2) << std::endl;
    return 0;
}

inline int cmdProblemSubmit(const std::string& dataDir, int contestId,
                            int problemIndex, const std::string& filePath,
                            const std::string& username = "") {
    ContestStore store(dataDir);
    if (store.submitProblem(contestId, problemIndex, filePath, username)) {
        std::cout << "Submission accepted for contest " << contestId
                  << " problem " << problemIndex << std::endl;
        std::cout << "  User: " << (username.empty() ? "unknown" : username) << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to submit: invalid contest or problem index." << std::endl;
        return 1;
    }
}

inline int cmdProblemView(const std::string& dataDir, int contestId, int problemIndex) {
    ContestStore store(dataDir);
    json submissions = store.viewProblemSubmissions(contestId, problemIndex);
    std::cout << submissions.dump(2) << std::endl;
    return 0;
}

inline int cmdList(const std::string& dataDir) {
    ContestStore store(dataDir);
    json contests = store.list();
    std::cout << contests.dump(2) << std::endl;
    return 0;
}

inline int cmdLeaderboard(const std::string& dataDir, int contestId) {
    ContestStore store(dataDir);
    json board = store.leaderboard(contestId);
    if (board.is_null()) {
        std::cerr << "Contest " << contestId << " not found." << std::endl;
        return 1;
    }
    std::cout << board.dump(2) << std::endl;
    return 0;
}

inline int cmdImportCdf(const std::string& dataDir, const std::string& cdfPath) {
    if (!fs::exists(cdfPath)) {
        std::cerr << "CDF 文件不存在: " << cdfPath << std::endl;
        return 1;
    }
    ContestStore store(dataDir);
    int contestId = store.importCdf(cdfPath, dataDir);
    if (contestId > 0) {
        return 0;
    }
    return 1;
}

inline int cmdExportCdf(const std::string& dataDir, int contestId, const std::string& cdfPath) {
    ContestStore store(dataDir);
    json cdf = store.exportCdf(contestId);
    if (cdf.is_null()) {
        std::cerr << "Contest " << contestId << " not found." << std::endl;
        return 1;
    }

    std::ofstream f(cdfPath);
    if (!f.is_open()) {
        std::cerr << "无法创建文件: " << cdfPath << std::endl;
        return 1;
    }
    f << cdf.dump(-1);  // 紧凑格式
    f.close();
    std::cout << "比赛已导出到: " << cdfPath << std::endl;
    return 0;
}

} // namespace contest
} // namespace judgelite

#endif // JUDGELITE_CONTEST_H