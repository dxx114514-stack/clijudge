#ifndef JUDGELITE_SANDBOX_RUNNER_HPP
#define JUDGELITE_SANDBOX_RUNNER_HPP

// sandbox_runner.hpp
// JudgeLite 安全沙箱运行器 — 基于 Windows Job Object + 受限令牌
//
// 安全特性:
//   1. CREATE_SUSPENDED 创建进程，绑定 Job 后再 ResumeThread，杜绝竞态逃逸
//   2. JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE — Job 关闭则整棵进程树被系统秒杀
//   3. 禁用 BREAKAWAY — 子进程无法脱离沙箱
//   4. JOB_OBJECT_LIMIT_ACTIVE_PROCESS — 限制进程树最大进程数
//   5. CreateRestrictedToken — 剥离 SeDebugPrivilege / SeImpersonatePrivilege 等高危特权
//   6. 内存限制 — Job Object process_memory_limit + 轮询双重保障
//   7. CPU 时间限制 — Job Object per-job user time limit + 轮询
//   8. 低完整性级别 — 禁止向高完整性对象写入
//
// 编译: g++ -O2 -static -o judgelite.exe main.cpp -lpsapi -luserenv
// 用法: 通过 main.cpp 调用 sandbox_run() 函数

#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#define NTDDI_VERSION 0x06000000
#include <windows.h>
#include <psapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "userenv.lib")

namespace judgelite {

// ── 构造子进程最小白名单环境块 ─────────────────────────────
// 子进程不应继承父进程完整环境变量（可能含 API 密钥等敏感值）。
// 只保留运行解释器/编译产物所需的最小集合（PATH / SystemRoot / TEMP /
// TMP / COMPUTERNAME / USERNAME / OS），避免环境变量探测与密钥泄露。
// 返回 CreateProcess lpEnvironment 格式的内存块（VAR=value\0 ... \0\0）。
inline std::vector<char> buildMinEnvBlock() {
    const char* names[] = {
        "PATH", "SystemRoot", "TEMP", "TMP", "COMPUTERNAME",
        "USERNAME", "USERPROFILE", "OS", "PATHEXT", "HOMEDRIVE",
        "HOMEPATH", "NUMBER_OF_PROCESSORS", "PROCESSOR_ARCHITECTURE"
    };
    std::string block;
    for (const char* n : names) {
        const char* v = getenv(n);
        if (v && v[0]) {
            block += n;
            block += '=';
            block += v;
            block += '\0';
        }
    }
    block += '\0'; // 结束空串
    return std::vector<char>(block.begin(), block.end());
}

// ── Windows CRT 命令行参数转义（MSDN "Parsing C Command-Line Arguments"）──
// 规则：反斜杠序列在引号内是转义符——偶数个 \ 表示字面反斜杠，奇数个 \ 表示
// 转义后面的引号。因此进入/退出引号前必须按"需要多少字面反斜杠就翻倍"处理：
//   1) 对参数中每段连续反斜杠，若其后面紧跟 `"`，反斜杠数量翻倍；
//   2) 参数尾部的连续反斜杠翻倍（其后是收尾引号）。
// 否则路径如 C:\foo"bar 会被解析错乱，甚至被注入改变命令结构。
inline std::string quoteCmdArg(const char* arg) {
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char* p = arg; *p; p++) {
        if (*p == '\\') { backslashes++; continue; }
        if (*p == '"') {
            // 引号前的反斜杠全部翻倍（转义这些反斜杠，避免它们转义引号）
            out.append(backslashes * 2, '\\');
            out += "\\\""; // 转义引号本身
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out += *p;
            backslashes = 0;
        }
    }
    // 参数尾部反斜杠翻倍（其后是收尾引号）
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

// ── 打开当前进程令牌（完整权限）────────────────────────────
// 关键：必须带 TOKEN_ASSIGN_PRIMARY，否则派生出的受限令牌缺少该访问权，
// CreateProcessAsUser 会报 ERROR_ACCESS_DENIED。
inline HANDLE openSelfTokenFull() {
    HANDLE hTok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                          &hTok))
        return NULL;
    return hTok;
}

// ── 从指定基令牌创建受限令牌: 禁用特权组 + 剥离高危特权 ────
inline HANDLE createRestrictedTokenFrom(HANDLE hCurrentToken) {
    if (!hCurrentToken) return NULL;

    std::vector<LUID> denyLuids;
    const char* privNames[] = {
        SE_DEBUG_NAME, SE_IMPERSONATE_NAME, SE_ASSIGNPRIMARYTOKEN_NAME,
        SE_TCB_NAME, SE_LOAD_DRIVER_NAME, SE_BACKUP_NAME,
        SE_RESTORE_NAME, SE_SECURITY_NAME, SE_TAKE_OWNERSHIP_NAME,
        SE_MANAGE_VOLUME_NAME, SE_CREATE_PAGEFILE_NAME,
        SE_SHUTDOWN_NAME, SE_SYSTEM_ENVIRONMENT_NAME, SE_UNDOCK_NAME,
        SE_PROF_SINGLE_PROCESS_NAME, SE_INCREASE_QUOTA_NAME,
        SE_INC_BASE_PRIORITY_NAME, SE_CREATE_SYMBOLIC_LINK_NAME,
    };
    for (const char* name : privNames) {
        LUID luid;
        if (LookupPrivilegeValueA(NULL, name, &luid))
            denyLuids.push_back(luid);
    }

    std::vector<LUID_AND_ATTRIBUTES> privsToDelete;
    for (const auto& luid : denyLuids)
        privsToDelete.push_back({ luid, 0 });

    // 禁用特权组：Administrators / Backup Operators / Replicate / Print Operators
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID groupSids[4] = { NULL, NULL, NULL, NULL };
    DWORD groupAuth[4][2] = {
        { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS },
        { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_BACKUP_OPS },
        { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REPLICATOR },
        { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_PRINT_OPS },
    };
    std::vector<SID_AND_ATTRIBUTES> groupsToDisable;
    for (int i = 0; i < 4; i++) {
        if (AllocateAndInitializeSid(&nt, 2, groupAuth[i][0], groupAuth[i][1],
                                     0, 0, 0, 0, 0, 0, &groupSids[i]))
            groupsToDisable.push_back({ groupSids[i], 0 });
    }

    HANDLE hRestricted = NULL;
    BOOL ok = CreateRestrictedToken(
        hCurrentToken, 0,
        (DWORD)groupsToDisable.size(), groupsToDisable.empty() ? NULL : groupsToDisable.data(),
        (DWORD)privsToDelete.size(), privsToDelete.data(),
        0, NULL,
        &hRestricted
    );
    for (int i = 0; i < 4; i++)
        if (groupSids[i]) LocalFree(groupSids[i]);
    return ok ? hRestricted : NULL;
}

inline HANDLE createRestrictedToken() {
    HANDLE hTok = openSelfTokenFull();
    HANDLE hRes = createRestrictedTokenFrom(hTok);
    if (hTok) CloseHandle(hTok);
    return hRes;
}

// ── 启用当前进程特权（CreateProcessAsUser 所需）────────────
// CreateProcessAsUser 要求调用进程持有 SeAssignPrimaryTokenPrivilege
// 和 SeIncreaseQuotaPrivilege 特权。普通用户令牌中这两个特权通常不存在
//（甚至无法启用），只有提权/服务环境下才可能可用。
inline void enablePrivilege(const char* name) {
    HANDLE hTok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok))
        return;
    LUID luid;
    if (!LookupPrivilegeValueA(NULL, name, &luid)) { CloseHandle(hTok); return; }
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hTok, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(hTok);
}

// ── 查询特权是否已启用 ─────────────────────────────────────
// 注意: GetTokenInformation 需要先查询长度再分配，否则特权较多时
// 会返回 ERROR_INSUFFICIENT_BUFFER。
inline bool hasPrivilege(const char* name) {
    HANDLE hTok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok))
        return false;
    LUID luid;
    if (!LookupPrivilegeValueA(NULL, name, &luid)) {
        CloseHandle(hTok);
        return false;
    }
    DWORD len = 0;
    GetTokenInformation(hTok, TokenPrivileges, NULL, 0, &len);
    if (!len) { CloseHandle(hTok); return false; }
    std::vector<char> buf(len);
    BOOL ok = GetTokenInformation(hTok, TokenPrivileges, buf.data(), len, &len);
    CloseHandle(hTok);
    if (!ok) return false;
    TOKEN_PRIVILEGES* tp = (TOKEN_PRIVILEGES*)buf.data();
    for (DWORD i = 0; i < tp->PrivilegeCount; i++)
        if (tp->Privileges[i].Luid.HighPart == luid.HighPart && tp->Privileges[i].Luid.LowPart == luid.LowPart &&
            (tp->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED))
            return true;
    return false;
}

// ── 路径辅助 ───────────────────────────────────────────────
inline std::wstring utf8ToWide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    std::wstring ws(n > 0 ? (size_t)n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], n);
    return ws;
}

inline std::wstring dirNameW(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    if (pos == 0) return p.substr(0, 1);
    return p.substr(0, pos);
}

inline std::wstring baseNameW(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

// ── 写元数据 JSON 到文件 ──────────────────────────────────
// 递归删除目录树（用于清理被恶意创建的目录型 _meta.json）
inline void deleteTreeW(const wchar_t* root) {
    std::wstring pat = std::wstring(root) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pat.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring sub = std::wstring(root) + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                deleteTreeW(sub.c_str());
            else
                DeleteFileW(sub.c_str());
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryW(root);
}

// 写入元数据前确保 metaFile 不是目录。
// 恶意提交可在 workDir 内预创建名为 _meta.json 的目录，导致 fopen("w") 失败、
// 元数据丢失（executor 读不到 meta → 回退到进程退出码，判题信息失真）。
inline void ensureMetaPath(const char* path) {
    std::wstring wp = utf8ToWide(path);
    DWORD attr = GetFileAttributesW(wp.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        deleteTreeW(wp.c_str());
}

inline void writeMeta(const char* path, int exitCode, DWORD timeMs, SIZE_T memKB, const char* signal) {
    ensureMetaPath(path);
    FILE* f = fopen(path, "w");
    if (!f) return;
    // JSON 字符串转义：防止 signal 含双引号/反斜杠导致元数据被破坏
    std::string sig(signal ? signal : "");
    std::string esc;
    for (char c : sig) {
        if (c == '"' || c == '\\') esc += '\\';
        esc += c;
    }
    fprintf(f, "{\"exit_code\":%d,\"time_used\":%lu,\"memory_used\":%llu,\"signal\":\"%s\"}",
            exitCode, (unsigned long)timeMs, (unsigned long long)memKB, esc.c_str());
    fclose(f);
}

// ── 递归把 root 下所有文件/目录的强制完整性标签设为 LOW ──────
// 用于受限令牌 + Low-IL 路径：子进程以 Low IL 运行，若 workDir 仍是 Medium
// 标签，完整性强制策略（no-write-up）会拒绝其写入。
inline void setLowLabelOnPath(const wchar_t* path, DWORD inh) {
    PSID lowSid = NULL;
    SID_IDENTIFIER_AUTHORITY ia = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (!AllocateAndInitializeSid(&ia, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &lowSid)) return;
    DWORD sidLen = GetLengthSid(lowSid);
    DWORD aceSize = sizeof(SYSTEM_MANDATORY_LABEL_ACE) + sidLen - sizeof(DWORD);
    DWORD aclSize = sizeof(ACL) + aceSize;

    PACL pAcl = (PACL)LocalAlloc(LPTR, aclSize);
    bool ok = false;
    if (pAcl && InitializeAcl(pAcl, aclSize, ACL_REVISION)) {
        SYSTEM_MANDATORY_LABEL_ACE* mace = (SYSTEM_MANDATORY_LABEL_ACE*)LocalAlloc(LPTR, aceSize);
        if (mace) {
            mace->Header.AceType = SYSTEM_MANDATORY_LABEL_ACE_TYPE;
            mace->Header.AceFlags = (BYTE)inh;
            mace->Header.AceSize = (WORD)aceSize;
            mace->Mask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP;
            CopySid(sidLen, &mace->SidStart, lowSid);
            if (AddAce(pAcl, ACL_REVISION, MAXDWORD, mace, aceSize))
                ok = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
                                           LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, pAcl) == ERROR_SUCCESS;
            LocalFree(mace);
        }
    }
    if (pAcl) LocalFree(pAcl);
    LocalFree(lowSid);
}

inline void setLowLabelRecursive(const wchar_t* root) {
    setLowLabelOnPath(root, CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE);
    std::wstring pat = std::wstring(root) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pat.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring sub = std::wstring(root) + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                setLowLabelRecursive(sub.c_str());
            else
                setLowLabelOnPath(sub.c_str(), 0);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

// ── 创建 Job Object 并设置安全限制 ─────────────────────────
inline HANDLE createJob(DWORD timeLimitMs, SIZE_T memLimitBytes, DWORD maxProcs) {
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    if (!hJob) return NULL;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    // 绝对不设置 JOB_OBJECT_LIMIT_BREAKAWAY_OK / SILENT_BREAKAWAY_OK

    jeli.BasicLimitInformation.ActiveProcessLimit = maxProcs;

    if (timeLimitMs > 0)
        jeli.BasicLimitInformation.PerJobUserTimeLimit.QuadPart = (ULONGLONG)timeLimitMs * 10000;

    if (memLimitBytes > 0) {
        jeli.ProcessMemoryLimit = memLimitBytes;
        jeli.JobMemoryLimit = memLimitBytes;
        jeli.BasicLimitInformation.LimitFlags |=
            JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
    }

    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        CloseHandle(hJob);
        return NULL;
    }
    return hJob;
}

// ── 查询 Job 峰值内存 (KB) ─────────────────────────────────
inline SIZE_T getJobPeakMemKB(HANDLE hJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    DWORD retLen = 0;
    if (QueryInformationJobObject(hJob, JobObjectExtendedLimitInformation, &info, sizeof(info), &retLen))
        return info.PeakProcessMemoryUsed / 1024;
    return 0;
}

// ── 沙箱运行结果结构 ──────────────────────────────────────
struct SandboxResult {
    int exitCode;
    DWORD timeUsedMs;
    SIZE_T memoryUsedKB;
    const char* signal;
    bool success;
};

// ── 主运行函数 ─────────────────────────────────────────────
// timeLimitMs: 时间限制（毫秒）
// memLimitMB: 内存限制（MB）
// maxProcesses: 最大进程数
// metaFile: 元数据输出文件路径
// exePath: 要执行的程序路径
// args: 程序参数列表
// fileIoMode: 是否启用文件IO模式
inline SandboxResult sandbox_run(
    DWORD timeLimitMs,
    SIZE_T memLimitMB,
    DWORD maxProcesses,
    const char* metaFile,
    const char* exePath,
    const std::vector<std::string>& args = {},
    bool fileIoMode = false
) {
    SandboxResult result = { 0, 0, 0, "null", false };

    // 构建命令行
    std::string cmdLine = exePath;
    for (const auto& arg : args) {
        cmdLine += " ";
        cmdLine += quoteCmdArg(arg.c_str());
    }
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    SIZE_T memLimitBytes = memLimitMB * 1024 * 1024;

    // 1. 创建 Job Object
    HANDLE hJob = createJob(timeLimitMs, memLimitBytes, maxProcesses);
    if (!hJob) {
        writeMeta(metaFile, -1, 0, 0, "SYSTEM_ERROR");
        return result;
    }

    // 2. 尝试启用特权（提权/服务环境），据此选择隔离方案
    enablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    enablePrivilege(SE_INCREASE_QUOTA_NAME);

    // 2b. 构建受限令牌（禁用特权组 + 剥离高危特权）
    HANDLE hRestricted = createRestrictedToken();

    // 3. CREATE_SUSPENDED 创建子进程 (继承 stdio 句柄)
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    SECURITY_ATTRIBUTES sa = {};
    sa.bInheritHandle = TRUE;

    DWORD flags = CREATE_SUSPENDED;

    // 子进程使用最小白名单环境
    std::vector<char> envBlock = buildMinEnvBlock();
    LPCH lpEnv = envBlock.data();

    // 低完整性级别设置
    bool lowIl = true;
    {
        const char* env = getenv("WINOJ_NO_LOWIL");
        if (env && env[0] == '1') lowIl = false;
    }
    if (lowIl && hRestricted) {
        TOKEN_MANDATORY_LABEL low = {};
        SID_IDENTIFIER_AUTHORITY ia = SECURITY_MANDATORY_LABEL_AUTHORITY;
        if (AllocateAndInitializeSid(&ia, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &low.Label.Sid)) {
            low.Label.Attributes = SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED;
            SetTokenInformation(hRestricted, TokenIntegrityLevel, &low, sizeof(low));
            LocalFree(low.Label.Sid);
        }
    }

    BOOL ok = FALSE;

    // 受限令牌路径
    if (hRestricted) {
        ok = CreateProcessAsUserA(hRestricted, NULL, cmdBuf.data(), &sa, &sa, TRUE, flags, lpEnv, NULL, &si, &pi);
    }

    // 令牌路径全部失败时 fail-closed
    if (!ok) {
        DWORD err = GetLastError();
        writeMeta(metaFile, -1, 0, 0, "SYSTEM_ERROR");
        CloseHandle(hJob);
        if (hRestricted) CloseHandle(hRestricted);
        return result;
    }

    // 3b. Low-IL 路径把 workDir 完整性标签递归降为 LOW
    bool noRelabel = false;
    {
        const char* env = getenv("WINOJ_NO_RELABEL");
        if (env && env[0] == '1') noRelabel = true;
    }
    if (lowIl && !noRelabel) {
        std::wstring workDirW = dirNameW(utf8ToWide(metaFile));
        setLowLabelRecursive(workDirW.c_str());
    }

    // 4. 绑定到 Job Object (进程仍挂起)
    if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        writeMeta(metaFile, -1, 0, 0, "SYSTEM_ERROR");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(hJob);
        if (hRestricted) CloseHandle(hRestricted);
        return result;
    }

    // 5. 唤醒主线程 — 进程开始执行
    ResumeThread(pi.hThread);

    // 6. 轮询等待: 每 50ms 检查内存和时间
    DWORD startTime = GetTickCount();
    SIZE_T peakMemKB = 0;
    SIZE_T memLimitKB = memLimitBytes / 1024;
    bool oom = false, timeout = false;

    while (true) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
        if (waitResult == WAIT_OBJECT_0) break; // 进程已退出

        peakMemKB = getJobPeakMemKB(hJob);
        if (memLimitKB > 0 && peakMemKB > memLimitKB) {
            oom = true;
            break;
        }
        DWORD elapsed = GetTickCount() - startTime;
        if (timeLimitMs > 0 && elapsed >= timeLimitMs) {
            timeout = true;
            break;
        }
    }

    DWORD timeUsed = GetTickCount() - startTime;

    // 终止进程 (如果是超时或 OOM)
    if (oom || timeout) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 500);
    }

    // 获取最终峰值内存和退出码
    peakMemKB = getJobPeakMemKB(hJob);
    if (peakMemKB == 0) {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof(pmc)))
            peakMemKB = pmc.PeakWorkingSetSize / 1024;
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    const char* signal = "null";
    if (oom) signal = "MEMORY_LIMIT";
    else if (timeout) signal = "SIGKILL";

    // 7. 写元数据
    writeMeta(metaFile, (int)exitCode, timeUsed, peakMemKB, signal);

    // 8. 清理
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hJob);
    if (hRestricted) CloseHandle(hRestricted);

    result.exitCode = (int)exitCode;
    result.timeUsedMs = timeUsed;
    result.memoryUsedKB = peakMemKB;
    result.signal = signal;
    result.success = true;
    return result;
}

} // namespace judgelite

#endif // JUDGELITE_SANDBOX_RUNNER_HPP