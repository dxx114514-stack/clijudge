# JudgeLite

轻量级命令行在线评测系统

## 功能特性

- **题目管理**: 创建、编辑、删除、导入导出题目
- **比赛管理**: 创建比赛、管理比赛题目
- **提交评测**: 使用沙箱安全执行代码
- **数据存储**: JSON格式存储，方便导入导出

## 子命令列表

```
├─article
│  ├─count
│  ├─create [标题] [md文件路径]
│  ├─delete [编号]
│  ├─list [L编号=1] [R编号=50]
│  └─view [编号]
├─contest
│  ├─create [标题] [开始时间] [结束时间] [题目1] [题目2]
│  ├─delete [编号]
│  ├─problem [编号] [题目在比赛中的编号]
│  │  ├─submit [文件地址]
│  │  └─view
│  └─view [编号]
├─ide
│  └─run [代码路径] [in文件路径]
├─problem
│  ├─count
│  ├─create [标题]
│  │  ├─-background [md路径]
│  │  ├─-describe [md路径]
│  │  ├─-exampleio [in路径] [out路径]
│  │  ├─-instyle [md路径]
│  │  └─-outstyle [md路径]
│  ├─delete [编号]
│  ├─edit [编号]
│  │  ├─-background [md路径]
│  │  ├─-describe [md路径]
│  │  ├─-exampleio [in路径] [out路径]
│  │  ├─-instyle [md路径]
│  │  ├─-outstyle [md路径]
│  │  └─-title [标题]
│  ├─export [zip路径]
│  ├─import [zip路径]
│  ├─list [L=1] [R=50]
│  ├─submit [编号] [程序文件路径]
│  ├─testdata [题目编号]
│  │  ├─-set-all
│  │  ├─-zip [zip路径]
│  │  ├─create [in] [out] [time] [mem] [pts]
│  │  ├─delete [编号]
│  │  └─list
│  └─view [编号]
└─submit
    ├─count
    └─list [L=1] [R=50]
```

## 编译

### 使用CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 使用g++直接编译

```bash
g++ -std=c++17 -O2 -o judgelite.exe src/main.cpp -lpsapi -luserenv -l ole32
```

## 使用方法

```bash
# 查看帮助
judgelite.exe help

# 创建题目
judgelite.exe problem create "A + B Problem"

# 查看题目
judgelite.exe problem view 1

# 列出所有题目
judgelite.exe problem list

# 创建文章
judgelite.exe article create "题目说明"

# 创建比赛
judgelite.exe contest create "比赛标题" "2026-01-01 10:00:00" "2026-01-01 12:00:00" 1 2
```

## 数据存储

数据默认存储在可执行文件同级目录的 `data/` 文件夹下，可通过环境变量 `JUDGELITE_DATA_DIR` 自定义。

## 系统要求

- Windows Vista 或更高版本
- 支持C++17的编译器

## 许可证

MIT License