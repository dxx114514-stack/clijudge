#ifndef CLIJUDGE_IDE_H
#define CLIJUDGE_IDE_H

// ide.h
// CLIJudge IDE子命令
//
// 子命令:
//   run [代码路径] [in文件路径] - 运行代码

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include "sandbox_runner.hpp"

namespace clijudge {
namespace ide {

namespace fs = std::filesystem;

// 运行配置
struct RunConfig {
    std::string codePath;
    std::string inputPath;
    std::string timeLimit = "1000";     // 默认1秒
    std::string memoryLimit = "256";    // 默认256MB
    std::string workDir;
    std::string metaFile;
};

// 根据文件扩展名获取编译/运行命令
std::string getRunCommand(const std::string& codePath, const std::string& workDir) {
    std::string ext = fs::path(codePath).extension().string();

    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
        // C++ 编译并运行
        std::string exePath = workDir + "\\output.exe";
        return "g++ -o \"" + exePath + "\" \"" + codePath + "\" && \"" + exePath + "\"";
    } else if (ext == ".c") {
        // C 编译并运行
        std::string exePath = workDir + "\\output.exe";
        return "gcc -o \"" + exePath + "\" \"" + codePath + "\" && \"" + exePath + "\"";
    } else if (ext == ".py") {
        // Python 运行
        return "python \"" + codePath + "\"";
    } else if (ext == ".java") {
        // Java 运行
        std::string className = fs::path(codePath).stem().string();
        return "javac \"" + codePath + "\" && java -cp \"" + fs::path(codePath).parent_path().string() + "\" " + className;
    } else if (ext == ".js") {
        // Node.js 运行
        return "node \"" + codePath + "\"";
    } else {
        // 尝试直接运行
        return "\"" + codePath + "\"";
    }
}

// 创建临时工作目录
std::string createWorkDir() {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);

    char guid[64];
    GUID g;
    CoCreateGuid(&g);
    sprintf_s(guid, "%08X%04X%04X", g.Data1, g.Data2, g.Data3);

    std::string workDir = std::string(tempPath) + "clijudge_ide_" + guid;
    fs::create_directories(workDir);
    return workDir;
}

// 清理临时目录
void cleanupWorkDir(const std::string& workDir) {
    try {
        if (fs::exists(workDir)) {
            fs::remove_all(workDir);
        }
    } catch (...) {
        // 忽略清理错误
    }
}

// 运行代码
int cmdRun(const std::string& codePath, const std::string& inputPath = "") {
    // 验证代码文件存在
    if (!fs::exists(codePath)) {
        std::cerr << "错误: 代码文件未找到: " << codePath << std::endl;
        return 1;
    }

    // 创建临时工作目录
    std::string workDir = createWorkDir();
    std::string metaFile = workDir + "\\_meta.json";

    // 获取运行命令
    std::string runCmd = getRunCommand(codePath, workDir);

    // 如果有输入文件，重定向输入
    if (!inputPath.empty() && fs::exists(inputPath)) {
        runCmd = runCmd + " < \"" + inputPath + "\"";
    }

    // 使用沙箱运行
    std::vector<std::string> args = {runCmd};
    auto result = clijudge::sandbox_run(
        10000,  // 10秒超时
        256,    // 256MB内存限制
        1,      // 单进程
        metaFile.c_str(),
        "cmd.exe",
        {"/c", runCmd},
        false
    );

    // 输出结果
    if (result.success) {
        std::cout << "退出码: " << result.exitCode << std::endl;
        std::cout << "用时: " << result.timeUsedMs << " 毫秒" << std::endl;
        std::cout << "内存: " << result.memoryUsedKB << " KB" << std::endl;
        if (strcmp(result.signal, "null") != 0) {
            std::cout << "信号: " << result.signal << std::endl;
        }
    } else {
        std::cerr << "沙箱运行代码失败。" << std::endl;
    }

    // 清理临时目录
    cleanupWorkDir(workDir);

    return result.success ? result.exitCode : 1;
}

// 显示帮助信息
void showHelp() {
    std::cout << "IDE 命令:" << std::endl;
    std::cout << "  run [代码路径] [输入文件路径] - 运行代码（可选输入文件）" << std::endl;
    std::cout << std::endl;
    std::cout << "支持的语言:" << std::endl;
    std::cout << "  C/C++ (.cpp, .cc, .cxx, .c)" << std::endl;
    std::cout << "  Python (.py)" << std::endl;
    std::cout << "  Java (.java)" << std::endl;
    std::cout << "  JavaScript (.js)" << std::endl;
}

} // namespace ide
} // namespace clijudge

#endif // CLIJUDGE_IDE_H