#ifndef CLIJUDGE_SUBMIT_H
#define CLIJUDGE_SUBMIT_H

// submit.h
// CLIJudge 提交管理子命令
//
// 子命令:
//   count - 统计提交数量
//   list [L=1] [R=50] - 列出提交记录

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include "json.hpp"
#include "platform.h"

namespace clijudge {
namespace submit {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 提交记录结构
struct Submission {
    int id;
    int problemId;
    std::string problemTitle;
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
class SubmitStore {
private:
    std::string dataDir;
    json data;

    void ensureDataDir() {
        fs::create_directories(dataDir + "/submissions");
    }

    void loadIndex() {
        std::string indexPath = dataDir + "/submissions/submissions.json";
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
        std::string indexPath = dataDir + "/submissions/submissions.json";
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
    SubmitStore(const std::string& dir) : dataDir(dir) {
        loadIndex();
    }

    // 添加提交记录
    int addSubmission(const Submission& sub) {
        int id = getNextId();

        json submission = {
            {"id", id},
            {"problem_id", sub.problemId},
            {"problem_title", sub.problemTitle},
            {"file_path", sub.filePath},
            {"submitted_at", sub.submittedAt},
            {"status", sub.status},
            {"score", sub.score},
            {"time_used", sub.timeUsed},
            {"memory_used", sub.memoryUsed},
            {"judge_times", 1},
            {"username", sub.username.empty() ? "unknown" : sub.username}
        };
        if (!sub.judgeDetail.empty()) {
            try {
                submission["judge_detail"] = json::parse(sub.judgeDetail);
            } catch (...) {
            }
        }

        data.push_back(submission);
        saveIndex();

        return id;
    }

    // 获取提交记录
    json getSubmission(int id) {
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() == id) {
                return item;
            }
        }
        return nullptr;
    }

    // 更新提交记录字段 (rejudge 用)
    bool updateSubmission(int id, const json& updates) {
        for (auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() == id) {
                for (auto it = updates.begin(); it != updates.end(); ++it) {
                    item[it.key()] = it.value();
                }
                saveIndex();
                return true;
            }
        }
        return false;
    }

    // 列出提交记录 (不含 judge_detail, 详情见 submissions.json / 比赛报告)
    json list(int left = 1, int right = 50) {
        json result = json::array();
        int count = 0;
        for (const auto& item : data) {
            count++;
            if (count >= left && count <= right) {
                json overview = item;
                overview.erase("judge_detail");
                result.push_back(overview);
            }
            if (count > right) break;
        }
        return result;
    }

    // 统计提交数量
    int count() {
        return (int)data.size();
    }

    // 获取题目提交统计
    json getProblemStats(int problemId) {
        int total = 0, accepted = 0, pending = 0, error = 0;
        for (const auto& item : data) {
            if (item.contains("problem_id") && item["problem_id"].get<int>() == problemId) {
                total++;
                std::string status = item.value("status", "pending");
                if (status == "accepted") accepted++;
                else if (status == "pending") pending++;
                else error++;
            }
        }

        return {
            {"total", total},
            {"accepted", accepted},
            {"pending", pending},
            {"error", error}
        };
    }
};

// 子命令实现
inline int cmdCount(const std::string& dataDir) {
    SubmitStore store(dataDir);
    std::cout << store.count() << std::endl;
    return 0;
}

inline int cmdList(const std::string& dataDir, int left = 1, int right = 50) {
    SubmitStore store(dataDir);
    json submissions = store.list(left, right);
    std::cout << submissions.dump(2) << std::endl;
    return 0;
}

// 辅助函数：获取当前时间
inline std::string getCurrentTime() {
    time_t now = time(nullptr);
    char buf[64];
    struct tm timeinfo;
    clijudge::platform::localTime(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return std::string(buf);
}

// 辅助函数：添加提交记录（供其他模块调用）
inline int addSubmission(const std::string& dataDir, int problemId, const std::string& problemTitle,
                         const std::string& filePath, const std::string& status,
                         int score = 0, int timeUsed = 0, int memoryUsed = 0,
                         const std::string& username = "",
                         const std::string& judgeDetail = "") {
    SubmitStore store(dataDir);

    Submission sub;
    sub.problemId = problemId;
    sub.problemTitle = problemTitle;
    sub.filePath = filePath;
    sub.submittedAt = getCurrentTime();
    sub.status = status;
    sub.score = score;
    sub.timeUsed = timeUsed;
    sub.memoryUsed = memoryUsed;
    sub.username = username;
    sub.judgeDetail = judgeDetail;

    return store.addSubmission(sub);
}

} // namespace submit
} // namespace clijudge

#endif // CLIJUDGE_SUBMIT_H