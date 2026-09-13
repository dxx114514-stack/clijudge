#ifndef CLIJUDGE_ENCODING_H
#define CLIJUDGE_ENCODING_H

// encoding.h
// 编码转换工具

#include <string>
#include <windows.h>

namespace clijudge {
namespace encoding {

// GBK 转 UTF-8
inline std::string gbkToUtf8(const std::string& gbk) {
    if (gbk.empty()) return "";
    
    // GBK -> UTF-16
    int wideLen = MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), (int)gbk.size(), nullptr, 0);
    if (wideLen <= 0) return "";
    
    std::wstring wideStr(wideLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), (int)gbk.size(), &wideStr[0], wideLen);
    
    // UTF-16 -> UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), wideLen, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    
    std::string utf8Str(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), wideLen, &utf8Str[0], utf8Len, nullptr, nullptr);
    
    return utf8Str;
}

// UTF-8 转 GBK
inline std::string utf8ToGbk(const std::string& utf8) {
    if (utf8.empty()) return "";
    
    // UTF-8 -> UTF-16
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (wideLen <= 0) return "";
    
    std::wstring wideStr(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wideStr[0], wideLen);
    
    // UTF-16 -> GBK
    int gbkLen = WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), wideLen, nullptr, 0, nullptr, nullptr);
    if (gbkLen <= 0) return "";
    
    std::string gbkStr(gbkLen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), wideLen, &gbkStr[0], gbkLen, nullptr, nullptr);
    
    return gbkStr;
}

} // namespace encoding
} // namespace clijudge

#endif // CLIJUDGE_ENCODING_H
