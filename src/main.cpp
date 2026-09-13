// main.cpp
// JudgeLite 轻量级命令行在线评测系统主入口
//
// 用法: judgelite.exe <子命令> [参数...]
//
// 子命令列表:
//   article
//     count                          统计文章数量
//     create [标题] [md文件路径]     创建文章
//     delete [编号]                  删除文章
//     list [L编号=1] [R编号=50]      列出文章
//     view [编号]                    查看文章
//
//   contest
//     create [标题] [开始时间] [结束时间] [题目1] [题目2]  创建比赛
//     delete [编号]                  删除比赛
//     problem [编号] [题目在比赛中的编号]
//       submit [文件地址]            提交题目
//       view                         查看题目
//     view [编号]                    查看比赛
//
//   ide
//     run [代码路径] [in文件路径]    运行代码
//
//   problem
//     count                          统计题目数量
//     create [标题]                  创建题目
//     delete [编号]                  删除题目
//     edit [编号]                    编辑题目
//     export [zip路径]               导出题目
//     import [zip路径]               导入题目
//     list [L=1] [R=50]              列出题目
//     submit [编号] [程序文件路径]   提交题目
//     testdata [题目编号]
//       -set-all                     设置所有测试数据
//       -zip [zip路径]               从zip导入测试数据
//       create [in] [out] [time] [mem] [pts]  创建测试数据
//       delete [编号]                删除测试数据
//       list                         列出测试数据
//     view [编号]                    查看题目
//
//   submit
//     count                          统计提交数量
//     list [L=1] [R=50]              列出提交记录

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include "json.hpp"
#include "encoding.h"
#include "article.h"
#include "contest.h"
#include "ide.h"
#include "problem.h"
#include "submit.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// GBK 转 UTF-8 (用于处理命令行参数)
inline std::string toUtf8(const std::string& str) {
    return judgelite::encoding::gbkToUtf8(str);
}

// 默认数据目录
std::string getDataDir() {
    // 优先使用环境变量
    const char* dataDir = getenv("JUDGELITE_DATA_DIR");
    if (dataDir && dataDir[0]) {
        return std::string(dataDir);
    }

    // 使用可执行文件所在目录
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        std::string exeDir = fs::path(exePath).parent_path().string();
        return exeDir + "\\data";
    }

    return ".\\data";
}

// 显示帮助信息
void showHelp() {
    std::cout << "JudgeLite - 轻量级命令行在线评测系统" << std::endl;
    std::cout << std::endl;
    std::cout << "用法: judgelite.exe <命令> [参数...]" << std::endl;
    std::cout << std::endl;
    std::cout << "命令:" << std::endl;
    std::cout << "  article   - 文章管理" << std::endl;
    std::cout << "  contest   - 比赛管理" << std::endl;
    std::cout << "  ide       - 代码运行" << std::endl;
    std::cout << "  problem   - 题目管理" << std::endl;
    std::cout << "  submit    - 提交管理" << std::endl;
    std::cout << std::endl;
    std::cout << "使用 'judgelite.exe <命令> help' 查看命令详细帮助。" << std::endl;
}

// 显示子命令帮助
void showCommandHelp(const std::string& command) {
    if (command == "article") {
        std::cout << "文章命令:" << std::endl;
        std::cout << "  count                          统计文章数量" << std::endl;
        std::cout << "  create [标题] [md文件路径]      创建文章" << std::endl;
        std::cout << "  delete [编号]                  删除文章" << std::endl;
        std::cout << "  list [L=1] [R=50]              列出文章" << std::endl;
        std::cout << "  view [编号]                    查看文章" << std::endl;
    } else if (command == "contest") {
        std::cout << "比赛命令:" << std::endl;
        std::cout << "  create [标题] [开始时间] [结束时间] [题目1] [题目2]  创建比赛" << std::endl;
        std::cout << "  delete [编号]                  删除比赛" << std::endl;
        std::cout << "  leaderboard [编号]             查看排行榜" << std::endl;
        std::cout << "  problem [编号] [题目索引]       比赛题目" << std::endl;
        std::cout << "    submit [文件] [--as 用户名]   提交解答" << std::endl;
        std::cout << "    view                         查看提交" << std::endl;
        std::cout << "  view [编号]                    查看比赛" << std::endl;
    } else if (command == "ide") {
        std::cout << "代码运行命令:" << std::endl;
        std::cout << "  run [代码路径] [输入文件路径]    运行代码" << std::endl;
    } else if (command == "problem") {
        std::cout << "题目命令:" << std::endl;
        std::cout << "  count                          统计题目数量" << std::endl;
        std::cout << "  create [标题]                  创建题目" << std::endl;
        std::cout << "  delete [编号]                  删除题目" << std::endl;
        std::cout << "  edit [编号]                    编辑题目" << std::endl;
        std::cout << "  export [zip路径]               导出题目" << std::endl;
        std::cout << "  import [zip路径]               导入题目" << std::endl;
        std::cout << "  list [L=1] [R=50]              列出题目" << std::endl;
        std::cout << "  submit [编号] [文件] [--as 用户名]  提交解答" << std::endl;
        std::cout << "  testdata [编号]                测试数据管理" << std::endl;
        std::cout << "    -set-all                     设置所有测试数据默认值" << std::endl;
        std::cout << "    -zip [zip路径]               从zip导入测试数据" << std::endl;
        std::cout << "    create [in] [out] [时间] [内存] [分值]  创建测试点" << std::endl;
        std::cout << "    delete [编号]                删除测试点" << std::endl;
        std::cout << "    list                         列出测试点" << std::endl;
        std::cout << "  view [编号]                    查看题目" << std::endl;
    } else if (command == "submit") {
        std::cout << "提交命令:" << std::endl;
        std::cout << "  count                          统计提交数量" << std::endl;
        std::cout << "  list [L=1] [R=50]              列出提交" << std::endl;
    } else {
        showHelp();
    }
}

// 解析整数参数
int parseInt(const char* str, int defaultValue = 0) {
    try {
        return std::stoi(str);
    } catch (...) {
        return defaultValue;
    }
}

// 主函数
int main(int argc, char* argv[]) {
    // 检查参数数量
    if (argc < 2) {
        showHelp();
        return 1;
    }

    std::string command = argv[1];
    std::string dataDir = getDataDir();

    // 确保数据目录存在
    if (!fs::exists(dataDir)) {
        fs::create_directories(dataDir);
    }

    // 帮助命令
    if (command == "help") {
        if (argc >= 3) {
            showCommandHelp(argv[2]);
        } else {
            showHelp();
        }
        return 0;
    }

    // 文章管理
    if (command == "article") {
        if (argc < 3) {
            showCommandHelp("article");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("article");
            return 0;
        } else if (subCmd == "count") {
            return judgelite::article::cmdCount(dataDir);
        } else if (subCmd == "create") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe article create [标题] [md文件路径]" << std::endl;
                return 1;
            }
            std::string title = toUtf8(argv[3]);
            std::string mdFile = (argc >= 5) ? argv[4] : "";
            return judgelite::article::cmdCreate(dataDir, title, mdFile);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe article delete [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::article::cmdDelete(dataDir, id);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return judgelite::article::cmdList(dataDir, L, R);
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe article view [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::article::cmdView(dataDir, id);
        } else {
            std::cerr << "未知的文章命令: " << subCmd << std::endl;
            showCommandHelp("article");
            return 1;
        }
    }

    // 比赛管理
    if (command == "contest") {
        if (argc < 3) {
            showCommandHelp("contest");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("contest");
            return 0;
        } else if (subCmd == "create") {
            if (argc < 7) {
                std::cerr << "用法: judgelite.exe contest create [标题] [开始时间] [结束时间] [题目1] [题目2]" << std::endl;
                return 1;
            }
            std::string title = toUtf8(argv[3]);
            std::string startTime = argv[4];
            std::string endTime = argv[5];
            std::vector<int> problemIds;
            for (int i = 6; i < argc; i++) {
                problemIds.push_back(parseInt(argv[i]));
            }
            return judgelite::contest::cmdCreate(dataDir, title, startTime, endTime, problemIds);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe contest delete [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::contest::cmdDelete(dataDir, id);
        } else if (subCmd == "problem") {
            if (argc < 5) {
                std::cerr << "用法: judgelite.exe contest problem [编号] [题目索引] [submit|view]" << std::endl;
                return 1;
            }
            int contestId = parseInt(argv[3]);
            int probIndex = parseInt(argv[4]);
            if (argc >= 6) {
                std::string action = argv[5];
                if (action == "submit") {
                    if (argc < 7) {
                        std::cerr << "用法: judgelite.exe contest problem [编号] [题目索引] submit [文件] [--as 用户名]" << std::endl;
                        return 1;
                    }
                    std::string filePath = argv[6];
                    std::string username;
                    for (int i = 7; i < argc; i++) {
                        if (strcmp(argv[i], "--as") == 0 && i + 1 < argc) {
                            username = argv[++i];
                        }
                    }
                    return judgelite::contest::cmdProblemSubmit(dataDir, contestId, probIndex, filePath, username);
                } else if (action == "view") {
                    return judgelite::contest::cmdProblemView(dataDir, contestId, probIndex);
                }
            }
            std::cerr << "未知的比赛题目操作。" << std::endl;
            return 1;
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe contest view [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::contest::cmdView(dataDir, id);
        } else if (subCmd == "list") {
            return judgelite::contest::cmdList(dataDir);
        } else if (subCmd == "leaderboard") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe contest leaderboard [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::contest::cmdLeaderboard(dataDir, id);
        } else {
            std::cerr << "未知的比赛命令: " << subCmd << std::endl;
            showCommandHelp("contest");
            return 1;
        }
    }

    // IDE
    if (command == "ide") {
        if (argc < 3) {
            showCommandHelp("ide");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("ide");
            return 0;
        } else if (subCmd == "run") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe ide run [代码路径] [输入文件路径]" << std::endl;
                return 1;
            }
            std::string codePath = argv[3];
            std::string inputPath = (argc >= 5) ? argv[4] : "";
            return judgelite::ide::cmdRun(codePath, inputPath);
        } else {
            std::cerr << "未知的代码运行命令: " << subCmd << std::endl;
            showCommandHelp("ide");
            return 1;
        }
    }

    // 题目管理
    if (command == "problem") {
        if (argc < 3) {
            showCommandHelp("problem");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("problem");
            return 0;
        } else if (subCmd == "count") {
            return judgelite::problem::cmdCount(dataDir);
        } else if (subCmd == "create") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem create [标题] [-background md] [-describe md] [-exampleio in out] [-instyle md] [-outstyle md] [-compare mode] [-spj-code md] [-spj-exe path] [-float-abs tol] [-float-rel tol]" << std::endl;
                return 1;
            }
            std::string title = toUtf8(argv[3]);
            std::string background, describe, exampleIn, exampleOut, instyle, outstyle;
            std::string compareMode, spjCode, spjExe;
            double floatAbsTol = 0.0, floatRelTol = 0.0;

            // 解析可选参数
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "-background") == 0 && i + 1 < argc) {
                    background = argv[++i];
                } else if (strcmp(argv[i], "-describe") == 0 && i + 1 < argc) {
                    describe = argv[++i];
                } else if (strcmp(argv[i], "-exampleio") == 0 && i + 2 < argc) {
                    exampleIn = argv[++i];
                    exampleOut = argv[++i];
                } else if (strcmp(argv[i], "-instyle") == 0 && i + 1 < argc) {
                    instyle = argv[++i];
                } else if (strcmp(argv[i], "-outstyle") == 0 && i + 1 < argc) {
                    outstyle = argv[++i];
                } else if (strcmp(argv[i], "-compare") == 0 && i + 1 < argc) {
                    compareMode = argv[++i];
                } else if (strcmp(argv[i], "-spj-code") == 0 && i + 1 < argc) {
                    spjCode = argv[++i];
                } else if (strcmp(argv[i], "-spj-exe") == 0 && i + 1 < argc) {
                    spjExe = argv[++i];
                } else if (strcmp(argv[i], "-float-abs") == 0 && i + 1 < argc) {
                    floatAbsTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-float-rel") == 0 && i + 1 < argc) {
                    floatRelTol = std::stod(argv[++i]);
                }
            }

            return judgelite::problem::cmdCreate(dataDir, title, background, describe,
                                                  exampleIn, exampleOut, instyle, outstyle,
                                                  compareMode, spjCode, spjExe,
                                                  floatAbsTol, floatRelTol);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem delete [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::problem::cmdDelete(dataDir, id);
        } else if (subCmd == "edit") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem edit [编号] [-title 标题] [-background md] [-describe md] [-exampleio in out] [-instyle md] [-outstyle md] [-compare mode] [-spj-code md] [-spj-exe path] [-float-abs tol] [-float-rel tol]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string title, background, describe, exampleIn, exampleOut, instyle, outstyle;
            std::string compareMode, spjCode, spjExe;
            double floatAbsTol = 0.0, floatRelTol = 0.0;

            // 解析可选参数
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "-title") == 0 && i + 1 < argc) {
                    title = toUtf8(argv[++i]);
                } else if (strcmp(argv[i], "-background") == 0 && i + 1 < argc) {
                    background = argv[++i];
                } else if (strcmp(argv[i], "-describe") == 0 && i + 1 < argc) {
                    describe = argv[++i];
                } else if (strcmp(argv[i], "-exampleio") == 0 && i + 2 < argc) {
                    exampleIn = argv[++i];
                    exampleOut = argv[++i];
                } else if (strcmp(argv[i], "-instyle") == 0 && i + 1 < argc) {
                    instyle = argv[++i];
                } else if (strcmp(argv[i], "-outstyle") == 0 && i + 1 < argc) {
                    outstyle = argv[++i];
                } else if (strcmp(argv[i], "-compare") == 0 && i + 1 < argc) {
                    compareMode = argv[++i];
                } else if (strcmp(argv[i], "-spj-code") == 0 && i + 1 < argc) {
                    spjCode = argv[++i];
                } else if (strcmp(argv[i], "-spj-exe") == 0 && i + 1 < argc) {
                    spjExe = argv[++i];
                } else if (strcmp(argv[i], "-float-abs") == 0 && i + 1 < argc) {
                    floatAbsTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-float-rel") == 0 && i + 1 < argc) {
                    floatRelTol = std::stod(argv[++i]);
                }
            }

            return judgelite::problem::cmdEdit(dataDir, id, title, background, describe,
                                               exampleIn, exampleOut, instyle, outstyle,
                                               compareMode, spjCode, spjExe,
                                               floatAbsTol, floatRelTol);
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem view [编号]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return judgelite::problem::cmdView(dataDir, id);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return judgelite::problem::cmdList(dataDir, L, R);
        } else if (subCmd == "submit") {
            if (argc < 5) {
                std::cerr << "用法: judgelite.exe problem submit [编号] [文件] [--as 用户名]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string filePath = argv[4];
            std::string username;
            for (int i = 5; i < argc; i++) {
                if (strcmp(argv[i], "--as") == 0 && i + 1 < argc) {
                    username = argv[++i];
                }
            }
            return judgelite::problem::cmdSubmit(dataDir, id, filePath, username);
        } else if (subCmd == "export") {
            if (argc < 5) {
                std::cerr << "用法: judgelite.exe problem export [编号] [zip路径]" << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string zipPath = argv[4];
            return judgelite::problem::cmdExport(dataDir, id, zipPath);
        } else if (subCmd == "import") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem import [zip路径]" << std::endl;
                return 1;
            }
            std::string zipPath = argv[3];
            return judgelite::problem::cmdImport(dataDir, zipPath);
        } else if (subCmd == "testdata") {
            if (argc < 4) {
                std::cerr << "用法: judgelite.exe problem testdata [编号] [命令]" << std::endl;
                return 1;
            }
            int problemId = parseInt(argv[3]);
            if (argc < 5) {
                return judgelite::problem::cmdTestDataList(dataDir, problemId);
            }
            std::string tcCmd = argv[4];
            if (tcCmd == "list") {
                return judgelite::problem::cmdTestDataList(dataDir, problemId);
            } else if (tcCmd == "create") {
                if (argc < 7) {
                    std::cerr << "用法: judgelite.exe problem testdata [编号] create [in] [out] [时间] [内存] [分值]" << std::endl;
                    return 1;
                }
                std::string inputData = argv[5];
                std::string outputData = argv[6];
                int timeLimit = (argc >= 8) ? parseInt(argv[7], -1) : -1;
                int memoryLimit = (argc >= 9) ? parseInt(argv[8], -1) : -1;
                int score = (argc >= 10) ? parseInt(argv[9], 25) : 25;
                return judgelite::problem::cmdTestDataCreate(dataDir, problemId, inputData, outputData, timeLimit, memoryLimit, score);
            } else if (tcCmd == "delete") {
                if (argc < 6) {
                    std::cerr << "用法: judgelite.exe problem testdata [编号] delete [测试点编号]" << std::endl;
                    return 1;
                }
                int tcId = parseInt(argv[5]);
                return judgelite::problem::cmdTestDataDelete(dataDir, problemId, tcId);
            } else if (tcCmd == "-set-all") {
                int timeLimit = -1, memoryLimit = -1, score = -1;
                for (int i = 5; i < argc; i++) {
                    if (strcmp(argv[i], "-time") == 0 && i + 1 < argc) {
                        timeLimit = parseInt(argv[++i]);
                    } else if (strcmp(argv[i], "-mem") == 0 && i + 1 < argc) {
                        memoryLimit = parseInt(argv[++i]);
                    } else if (strcmp(argv[i], "-pts") == 0 && i + 1 < argc) {
                        score = parseInt(argv[++i]);
                    }
                }
                return judgelite::problem::cmdTestDataSetAll(dataDir, problemId, timeLimit, memoryLimit, score);
            } else if (tcCmd == "-zip") {
                if (argc < 6) {
                    std::cerr << "用法: judgelite.exe problem testdata [编号] -zip [zip路径]" << std::endl;
                    return 1;
                }
                std::string zipPath = argv[5];
                // TODO: 实现从zip导入测试数据
                std::cerr << "ZIP导入功能尚未实现。" << std::endl;
                return 1;
            } else {
                std::cerr << "未知的测试数据命令: " << tcCmd << std::endl;
                return 1;
            }
        } else {
            std::cerr << "未知的题目命令: " << subCmd << std::endl;
            showCommandHelp("problem");
            return 1;
        }
    }

    // 提交管理
    if (command == "submit") {
        if (argc < 3) {
            showCommandHelp("submit");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("submit");
            return 0;
        } else if (subCmd == "count") {
            return judgelite::submit::cmdCount(dataDir);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return judgelite::submit::cmdList(dataDir, L, R);
        } else {
            std::cerr << "未知的提交命令: " << subCmd << std::endl;
            showCommandHelp("submit");
            return 1;
        }
    }

    // 未知命令
    std::cerr << "未知命令: " << command << std::endl;
    showHelp();
    return 1;
}