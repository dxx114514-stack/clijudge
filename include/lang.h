#ifndef CLIJUDGE_LANG_H
#define CLIJUDGE_LANG_H

// lang.h
// CliJudge 语言管理模块
//
// 子命令:
//   list                  显示本地已有语言
//   list --online         显示仓库中可用的语言
//   switch [langname]     切换语言（若本地没有则下载）
//   delete [langname]     删除本地语言文件
//   pull [langname]       拉取语言文件（不切换）

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <winhttp.h>
#include "json.hpp"

#pragma comment(lib, "winhttp.lib")

namespace clijudge {
namespace lang {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 仓库信息
const std::string REPO_OWNER = "dxx114514-stack";
const std::string REPO_NAME = "clijudge";
const std::string LANGS_BRANCH = "languages";

// 获取 exe 所在目录
inline std::string getExeDir() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        return fs::path(exePath).parent_path().string();
    }
    return ".";
}

// 获取语言文件目录
inline std::string getLangsDir() {
    return getExeDir() + "\\data\\langs";
}

// 获取配置文件路径
inline std::string getConfigPath() {
    return getExeDir() + "\\data\\config.json";
}

// 确保目录存在
inline void ensureDir(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// 读取文件内容
inline std::string readFile(const std::string& path) {
    if (!fs::exists(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 写入文件内容
inline bool writeFile(const std::string& path, const std::string& content) {
    ensureDir(fs::path(path).parent_path().string());
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(content.data(), content.size());
    return f.good();
}

// 读取配置
inline json loadConfig() {
    std::string path = getConfigPath();
    std::string content = readFile(path);
    if (content.empty()) {
        return json{{"current_lang", ""}};
    }
    try {
        return json::parse(content);
    } catch (...) {
        return json{{"current_lang", ""}};
    }
}

// 保存配置
inline bool saveConfig(const json& config) {
    return writeFile(getConfigPath(), config.dump(2));
}

// 获取当前语言名
inline std::string getCurrentLang() {
    json config = loadConfig();
    return config.value("current_lang", "");
}

// 通过 HTTP GET 获取内容
inline std::string httpGet(const std::string& url) {
    // 解析 URL
    std::wstring wUrl(url.begin(), url.end());
    
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszHostName = new wchar_t[256];
    urlComp.lpszUrlPath = new wchar_t[1024];
    urlComp.dwHostNameLength = 256;
    urlComp.dwUrlPathLength = 1024;
    
    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.size(), 0, &urlComp)) {
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    
    // 建立连接
    HINTERNET hSession = WinHttpOpen(L"CliJudge/1.0", 
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                         urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    // 发送请求（忽略 SSL 证书验证）
    DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    
    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszHostName;
        delete[] urlComp.lpszUrlPath;
        return "";
    }
    
    // 读取响应
    std::string response;
    DWORD bytesRead = 0;
    char buffer[4096];
    
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
        bytesRead = 0;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    delete[] urlComp.lpszHostName;
    delete[] urlComp.lpszUrlPath;
    
    return response;
}

// 获取在线语言列表
inline json getOnlineLangs() {
    // 使用 GitHub API 获取 languages 目录下的文件列表
    std::string url = "https://api.github.com/repos/" + REPO_OWNER + "/" + REPO_NAME + 
                      "/contents/langs?ref=" + LANGS_BRANCH;
    
    std::string response = httpGet(url);
    if (response.empty()) {
        return json::array();
    }
    
    try {
        json data = json::parse(response);
        if (!data.is_array()) return json::array();
        
        json langs = json::array();
        for (const auto& item : data) {
            if (item.value("type", "") == "file") {
                std::string name = item.value("name", "");
                if (name.size() > 4 && name.substr(name.size() - 4) == ".cjl") {
                    langs.push_back(name.substr(0, name.size() - 4));
                }
            }
        }
        return langs;
    } catch (...) {
        return json::array();
    }
}

// 下载语言文件
inline bool downloadLang(const std::string& langName) {
    std::string url = "https://raw.githubusercontent.com/" + REPO_OWNER + "/" + REPO_NAME + 
                      "/" + LANGS_BRANCH + "/langs/" + langName + ".cjl";
    
    std::string content = httpGet(url);
    if (content.empty()) {
        std::cerr << "Failed to download language file: " << langName << std::endl;
        return false;
    }
    
    std::string path = getLangsDir() + "\\" + langName + ".cjl";
    if (!writeFile(path, content)) {
        std::cerr << "Failed to save language file: " << langName << std::endl;
        return false;
    }
    
    return true;
}

// 加载语言文件
inline json loadLang(const std::string& langName) {
    std::string path = getLangsDir() + "\\" + langName + ".cjl";
    std::string content = readFile(path);
    if (content.empty()) {
        return nullptr;
    }
    try {
        return json::parse(content);
    } catch (...) {
        return nullptr;
    }
}

// 获取本地语言列表
inline json getLocalLangs() {
    std::string langsDir = getLangsDir();
    json langs = json::array();
    
    if (!fs::exists(langsDir)) {
        return langs;
    }
    
    for (const auto& entry : fs::directory_iterator(langsDir)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.substr(name.size() - 4) == ".cjl") {
                std::string langName = name.substr(0, name.size() - 4);
                json langInfo = loadLang(langName);
                json item;
                item["name"] = langName;
                if (!langInfo.is_null() && langInfo.contains("meta")) {
                    item["display"] = langInfo["meta"].value("display", langName);
                    item["version"] = langInfo["meta"].value("version", "1.0.0");
                } else {
                    item["display"] = langName;
                    item["version"] = "unknown";
                }
                langs.push_back(item);
            }
        }
    }
    
    return langs;
}

// ── 子命令实现 ────────────────────────────────────────────────

// list - 显示本地/在线语言
inline int cmdList(bool online = false) {
    if (online) {
        std::cout << "Online languages:" << std::endl;
        json langs = getOnlineLangs();
        if (langs.empty()) {
            std::cout << "  (none or network error)" << std::endl;
            return 0;
        }
        std::string current = getCurrentLang();
        for (const auto& lang : langs) {
            std::string name = lang.get<std::string>();
            std::string marker = (name == current) ? " *" : "";
            std::cout << "  " << name << marker << std::endl;
        }
        std::cout << std::endl;
        std::cout << "* = currently active" << std::endl;
    } else {
        std::cout << "Local languages:" << std::endl;
        json langs = getLocalLangs();
        if (langs.empty()) {
            std::cout << "  (none)" << std::endl;
            std::cout << std::endl;
            std::cout << "Use 'clijudge displaylang list --online' to see available languages." << std::endl;
            std::cout << "Use 'clijudge displaylang switch [langname]' to download and activate." << std::endl;
            return 0;
        }
        std::string current = getCurrentLang();
        for (const auto& lang : langs) {
            std::string name = lang.value("name", "");
            std::string display = lang.value("display", name);
            std::string version = lang.value("version", "");
            std::string marker = (name == current) ? " *" : "";
            std::cout << "  " << name << " (" << display << ") v" << version << marker << std::endl;
        }
        std::cout << std::endl;
        std::cout << "* = currently active" << std::endl;
    }
    return 0;
}

// switch - 切换语言
inline int cmdSwitch(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << "Usage: clijudge displaylang switch [langname]" << std::endl;
        return 1;
    }
    
    // 检查本地是否已有
    std::string path = getLangsDir() + "\\" + langName + ".cjl";
    if (!fs::exists(path)) {
        std::cout << "Language '" << langName << "' not found locally. Downloading..." << std::endl;
        if (!downloadLang(langName)) {
            std::cerr << "Failed to download language: " << langName << std::endl;
            return 1;
        }
        std::cout << "Downloaded successfully." << std::endl;
    }
    
    // 验证文件有效
    json langData = loadLang(langName);
    if (langData.is_null()) {
        std::cerr << "Invalid language file: " << langName << std::endl;
        return 1;
    }
    
    // 保存配置
    json config = loadConfig();
    config["current_lang"] = langName;
    if (saveConfig(config)) {
        std::cout << "Switched to language: " << langName << std::endl;
    } else {
        std::cerr << "Failed to save configuration." << std::endl;
        return 1;
    }
    
    return 0;
}

// delete - 删除本地语言
inline int cmdDelete(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << "Usage: clijudge displaylang delete [langname]" << std::endl;
        return 1;
    }
    
    std::string path = getLangsDir() + "\\" + langName + ".cjl";
    if (!fs::exists(path)) {
        std::cerr << "Language file not found: " << langName << std::endl;
        return 1;
    }
    
    // 如果正在使用该语言，先取消
    std::string current = getCurrentLang();
    if (current == langName) {
        json config = loadConfig();
        config["current_lang"] = "";
        saveConfig(config);
        std::cout << "Deactivated current language." << std::endl;
    }
    
    if (fs::remove(path)) {
        std::cout << "Deleted language: " << langName << std::endl;
    } else {
        std::cerr << "Failed to delete language file." << std::endl;
        return 1;
    }
    
    return 0;
}

// pull - 拉取语言文件（不切换）
inline int cmdPull(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << "Usage: clijudge displaylang pull [langname]" << std::endl;
        return 1;
    }
    
    std::cout << "Downloading language: " << langName << "..." << std::endl;
    if (downloadLang(langName)) {
        std::cout << "Updated language: " << langName << std::endl;
    } else {
        std::cerr << "Failed to download language: " << langName << std::endl;
        return 1;
    }
    
    return 0;
}

// ── 本地化字符串获取 ──────────────────────────────────────────

// 全局语言数据缓存
inline json& getLangData() {
    static json langData = nullptr;
    static bool loaded = false;
    
    if (!loaded) {
        loaded = true;
        std::string current = getCurrentLang();
        if (!current.empty()) {
            langData = loadLang(current);
        }
    }
    
    return langData;
}

// 获取本地化字符串
inline std::string tr(const std::string& key, const std::string& fallback = "") {
    json& langData = getLangData();
    if (langData.is_null()) {
        return fallback.empty() ? key : fallback;
    }
    
    // 支持点分隔的键路径 (如 "help.title")
    json* node = &langData;
    std::istringstream ss(key);
    std::string segment;
    
    while (std::getline(ss, segment, '.')) {
        if (node->contains(segment) && (*node)[segment].is_object()) {
            node = &(*node)[segment];
        } else if (node->contains(segment)) {
            return (*node)[segment].get<std::string>();
        } else {
            return fallback.empty() ? key : fallback;
        }
    }
    
    return fallback.empty() ? key : fallback;
}

// 检查是否已配置语言
inline bool isLangConfigured() {
    std::string current = getCurrentLang();
    if (current.empty()) return false;
    json langData = loadLang(current);
    return !langData.is_null();
}

// 显示未配置语言的错误信息
inline void showNoLangError() {
    std::cerr << "Error: No display language configured." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Please run: clijudge displaylang switch [langname]" << std::endl;
    std::cerr << "Use 'clijudge displaylang list --online' to see available languages." << std::endl;
}

} // namespace lang
} // namespace clijudge

#endif // CLIJUDGE_LANG_H