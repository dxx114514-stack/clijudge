#ifndef CLIJUDGE_ENCODING_H
#define CLIJUDGE_ENCODING_H

// encoding.h
// 编码转换工具

#include <string>

#ifdef _WIN32

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

#else // ────────────────────────── Linux: iconv ──────────────────────────

#include <iconv.h>
#include <cstring>
#include <vector>

namespace clijudge {
namespace encoding {

namespace {

// 通用 iconv 转换
inline std::string iconvConvert(const char* to, const char* from, const std::string& in) {
    if (in.empty()) return "";
    iconv_t cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) return "";
    
    std::vector<char> out(in.size() * 4 + 16, '\0');
    char* inPtr = const_cast<char*>(in.data());
    size_t inLeft = in.size();
    char* outPtr = out.data();
    size_t outLeft = out.size();
    
    size_t rc = iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft);
    iconv_close(cd);
    if (rc == (size_t)-1 && outPtr == out.data()) return "";
    return std::string(out.data(), out.size() - outLeft);
}

} // namespace

// GBK 转 UTF-8
inline std::string gbkToUtf8(const std::string& gbk) {
    return iconvConvert("UTF-8//IGNORE", "GBK", gbk);
}

// UTF-8 转 GBK
inline std::string utf8ToGbk(const std::string& utf8) {
    return iconvConvert("GBK//IGNORE", "UTF-8", utf8);
}

} // namespace encoding
} // namespace clijudge

#endif // _WIN32

#endif // CLIJUDGE_ENCODING_H
