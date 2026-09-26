#ifndef CLIJUDGE_ARTICLE_H
#define CLIJUDGE_ARTICLE_H

// article.h
// CLIJudge 文章管理子命令
//
// 子命令:
//   count - 统计文章数量
//   create [标题] [md文件路径] - 创建文章
//   delete [编号] - 删除文章
//   list [L编号=1] [R编号=50] - 列出文章
//   view [编号] - 查看文章

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
namespace article {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 文章数据结构
struct Article {
    int id;
    std::string title;
    std::string content;  // Markdown内容
    std::string createdAt;
    std::string updatedAt;
};

// 数据存储类
class ArticleStore {
private:
    std::string dataDir;
    json data;

    void ensureDataDir() {
        fs::create_directories(dataDir + "/articles");
    }

    std::string getArticlePath(int id) {
        return dataDir + "/articles/article_" + std::to_string(id) + ".json";
    }

    void loadIndex() {
        std::string indexPath = dataDir + "/articles/articles.json";
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
        std::string indexPath = dataDir + "/articles/articles.json";
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
    ArticleStore(const std::string& dir) : dataDir(dir) {
        loadIndex();
    }

    // 统计文章数量
    int count() {
        return (int)data.size();
    }

    // 创建文章
    int create(const std::string& title, const std::string& mdFilePath = "", const std::string& content = "") {
        int id = getNextId();

        std::string articleContent = content;
        if (!mdFilePath.empty() && fs::exists(mdFilePath)) {
            std::ifstream f(mdFilePath);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                articleContent = ss.str();
            }
        }

        json article = {
            {"id", id},
            {"title", title},
            {"content", articleContent},
            {"created_at", getCurrentTime()},
            {"updated_at", getCurrentTime()}
        };

        data.push_back(article);
        saveIndex();

        // 保存文章内容到单独文件
        ensureDataDir();
        std::ofstream f(getArticlePath(id));
        if (f.is_open()) {
            f << article.dump(2);
        }

        return id;
    }

    // 删除文章
    bool deleteArticle(int id) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it->contains("id") && (*it)["id"].get<int>() == id) {
                data.erase(it);
                saveIndex();

                // 删除文章文件
                std::string path = getArticlePath(id);
                if (fs::exists(path)) {
                    fs::remove(path);
                }
                return true;
            }
        }
        return false;
    }

    // 查看文章
    json view(int id) {
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() == id) {
                // 加载完整内容
                std::string path = getArticlePath(id);
                if (fs::exists(path)) {
                    std::ifstream f(path);
                    if (f.is_open()) {
                        json fullArticle;
                        f >> fullArticle;
                        return fullArticle;
                    }
                }
                return item;
            }
        }
        return nullptr;
    }

    // 列出文章
    json list(int left = 1, int right = 50) {
        json result = json::array();
        int count = 0;
        for (const auto& item : data) {
            count++;
            if (count >= left && count <= right) {
                result.push_back(item);
            }
            if (count > right) break;
        }
        return result;
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
inline int cmdCount(const std::string& dataDir) {
    ArticleStore store(dataDir);
    std::cout << store.count() << std::endl;
    return 0;
}

inline int cmdCreate(const std::string& dataDir, const std::string& title, const std::string& mdPath = "") {
    ArticleStore store(dataDir);
    int id = store.create(title, mdPath);
    std::cout << "文章已创建，编号: " << id << std::endl;
    return 0;
}

inline int cmdDelete(const std::string& dataDir, int id) {
    ArticleStore store(dataDir);
    if (store.deleteArticle(id)) {
        std::cout << "文章 " << id << " 已删除。" << std::endl;
        return 0;
    } else {
        std::cerr << "文章 " << id << " 未找到。" << std::endl;
        return 1;
    }
}

inline int cmdList(const std::string& dataDir, int left = 1, int right = 50) {
    ArticleStore store(dataDir);
    json articles = store.list(left, right);
    std::cout << articles.dump(2) << std::endl;
    return 0;
}

inline int cmdView(const std::string& dataDir, int id) {
    ArticleStore store(dataDir);
    json article = store.view(id);
    if (article.is_null()) {
        std::cerr << "文章 " << id << " 未找到。" << std::endl;
        return 1;
    }
    std::cout << article.dump(2) << std::endl;
    return 0;
}

} // namespace article
} // namespace clijudge

#endif // CLIJUDGE_ARTICLE_H