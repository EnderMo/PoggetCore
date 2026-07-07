// vina_storage.hpp
#ifndef VINA_STORAGE_HPP
#define VINA_STORAGE_HPP

#include "VinaBuilder.hpp" 
#include "vui.parser.hpp" 
#include <variant>
#include <string>
#include <memory> 
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <type_traits> 
#include <cstring> 
#include <any> 
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else

#endif




struct VinaStorageNestedObject;


using VinaStorageValue = std::variant<int, double, bool, std::wstring, std::shared_ptr<VinaStorageNestedObject>>;


using VinaStorageObjectMap = tsl::ordered_map<std::wstring, VinaStorageValue>;

struct VinaStorageNestedObject {
    VinaStorageObjectMap data;
};



#ifdef _WIN32

inline std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    // 计算需要的字节数
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) return std::string(); // 转换失败，防止崩溃
    std::string strTo(size_needed, 0);
    // 执行转换
    int result = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    if (result <= 0) return std::string(); // 转换失败，防止崩溃
    return strTo;
}

// 将 std::string (UTF-8) 转换为 std::wstring (UTF-16)
inline std::wstring UTF8ToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    // 计算需要的宽字符数
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed <= 0) return std::wstring(); // 转换失败，防止崩溃
    std::wstring wstrTo(size_needed, 0);
    // 执行转换
    int result = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    if (result <= 0) return std::wstring(); // 转换失败，防止崩溃
    return wstrTo;
}
#else
// 在非 Windows 平台，假设 char/std::string 是 UTF-8，wstring 是 UTF-32 (或平台依赖)
// 这里的实现将是错误的，但在您明确要求 Windows API 的前提下，我们只在 _WIN32 宏下使用。
// 如果您需要在跨平台项目中使用，请使用 ICU 或 C++20 的 std::string_view::to_wstring 等。
inline std::string WStringToUTF8(const std::wstring& wstr) {
    // 假设非 Windows 平台可以简单进行转换 (不推荐用于生产环境)
    std::string str(wstr.length(), ' ');
    std::transform(wstr.begin(), wstr.end(), str.begin(),
        [](wchar_t c) { return (char)c; });
    return str;
}

inline std::wstring UTF8ToWString(const std::string& str) {
    // 假设非 Windows 平台可以简单进行转换 (不推荐用于生产环境)
    std::wstring wstr(str.length(), L' ');
    std::transform(str.begin(), str.end(), wstr.begin(),
        [](char c) { return (wchar_t)c; });
    return wstr;
}
#endif

// --- VinaStorage 类定义 ---
class VinaStorage {
private:
    std::wstring filename_;
    std::wstring backup_path_;
    // 根对象列表也改为有序 Map，以保持多个根对象的顺序
    tsl::ordered_map<std::wstring, VinaStorageObjectMap> root_objects_;
    bool loaded_ = false; // Add a flag to track if file is loaded

    // ... (convertBasicObjectToMap, isEscapedPath, hasMultipleConsecutiveBackslashes, populateVinaObject 保持不变) ...
    // 为了保持您提供的代码的完整性和结构，我将这些函数体粘贴在下面

    // START: 保持不变的代码片段

    VinaStorageObjectMap convertBasicObjectToMap(vui::parser::basic_object<wchar_t>& basic_obj) {
        VinaStorageObjectMap converted_map;

        auto it = basic_obj.begin();
        auto end_it = basic_obj.end();

        for (; it != end_it; ++it) {
            auto& pair = *it;
            std::wstring key = pair.name();
            std::any value_any = basic_obj[key];

            if (value_any.type() == typeid(std::wstring)) {
                // --- 修复开始：处理字符串中的 bool 和换行符问题 ---
                std::wstring val_str = std::any_cast<std::wstring>(value_any);

                // 1. 去除首尾空白字符（包括换行符 \n, \r, 空格, 制表符）
                const std::wstring whitespace = L" \t\n\r";
                size_t start = val_str.find_first_not_of(whitespace);
                if (start != std::wstring::npos) {
                    size_t end = val_str.find_last_not_of(whitespace);
                    val_str = val_str.substr(start, end - start + 1);
                }
                else {
                    val_str = L""; // 全是空白
                }

                // 2. 尝试还原 bool 类型
                if (val_str == L"true") {
                    converted_map[key] = true;
                }
                else if (val_str == L"false") {
                    converted_map[key] = false;
                }
                else {
                    // --- 修复：尝试还原 int 类型 (解决 test : "0" 问题) ---
                    bool is_int_str = true;
                    if (val_str.empty() || val_str.find(L'.') != std::wstring::npos) { // 排除空字符串和浮点数
                        is_int_str = false;
                    }
                    else {
                        size_t idx = 0;
                        // 检查可选的负号
                        if (val_str[0] == L'-') {
                            if (val_str.length() == 1) { // 只有负号 "-"
                                is_int_str = false;
                            }
                            else {
                                idx = 1;
                            }
                        }

                        // 检查其余字符是否都是数字
                        if (is_int_str) {
                            // 必须至少有一个数字
                            if (val_str.length() <= idx) {
                                is_int_str = false;
                            }
                            else {
                                for (; idx < val_str.length(); ++idx) {
                                    if (val_str[idx] < L'0' || val_str[idx] > L'9') {
                                        is_int_str = false;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (is_int_str) {
                        try {
                            // 使用 std::stoi 将数字字符串转换为 int
                            converted_map[key] = std::stoi(val_str);
                        }
                        catch (...) {
                            // 转换失败（例如数字过大溢出），保留为字符串
                            converted_map[key] = val_str;
                        }
                    }
                    else {
                        // 不是 bool，不是纯整数，保留为字符串
                        converted_map[key] = val_str;
                    }
                    // --- 修复结束 ---
                }
                // --- 修复结束 ---
            }
            else if (value_any.type() == typeid(int)) {
                converted_map[key] = std::any_cast<int>(value_any);
            }
            else if (value_any.type() == typeid(double)) {
                converted_map[key] = std::any_cast<double>(value_any);
            }
            else if (value_any.type() == typeid(bool)) {
                converted_map[key] = std::any_cast<bool>(value_any);
            }
            else if (value_any.type() == typeid(vui::parser::basic_object<wchar_t>)) {
                auto nested_basic_obj = std::any_cast<vui::parser::basic_object<wchar_t>>(value_any);
                auto nested_map = convertBasicObjectToMap(nested_basic_obj);
                auto nested_obj_ptr = std::make_shared<VinaStorageNestedObject>();
                nested_obj_ptr->data = std::move(nested_map);
                converted_map[key] = nested_obj_ptr;
            }
            else {
                std::wcerr << L"Warning: Unknown type for key '" << key << L"' during conversion. Skipping." << std::endl;
            }
        }

        return converted_map;
    }

    // 检查字符串是否已经是转义过的路径格式 (例如 C:\\path\\to\\file)
    bool isEscapedPath(const std::wstring& str) {
        if (str.length() < 3) return false;

        // 检查是否是驱动器路径格式 C:\\ 或者网络路径 \\server
        if ((str[1] == L':' && str[2] == L'\\')) { // C:\ 格式
            // 检查后续是否有连续的反斜杠
            for (size_t i = 3; i < str.length(); ++i) {
                if (str[i] == L'\\') {
                    // 检查前一个字符是否也是反斜杠
                    if (i > 0 && str[i - 1] == L'\\') {
                        // 这是一个已转义的路径
                        continue;
                    }
                }
            }
            // 简单检查：如果字符串包含冒号后跟反斜杠，很可能是路径
            size_t colon_pos = str.find(L':');
            if (colon_pos != std::wstring::npos && colon_pos + 1 < str.length() && str[colon_pos + 1] == L'\\') {
                // 统计反斜杠的数量，如果是偶数个连续的反斜杠，可能是已转义的
                size_t backslash_count = 0;
                bool has_consecutive_backslashes = false;
                for (size_t i = 0; i < str.length(); ++i) {
                    if (str[i] == L'\\') {
                        backslash_count++;
                        if (i > 0 && str[i - 1] == L'\\') {
                            has_consecutive_backslashes = true;
                        }
                    }
                    else {
                        backslash_count = 0;
                    }
                }
                // 如果有连续的反斜杠，可能是已转义的
                return has_consecutive_backslashes;
            }
        }
        // 检查网络路径 \\server\share
        else if (str.length() >= 2 && str[0] == L'\\' && str[1] == L'\\') {
            size_t third_slash = str.find(L'\\', 2);
            if (third_slash != std::wstring::npos) {
                return true;
            }
        }

        return false;
    }

    // 检查字符串是否包含多个连续的反斜杠
    bool hasMultipleConsecutiveBackslashes(const std::wstring& str) {
        for (size_t i = 1; i < str.length(); ++i) {
            if (str[i] == L'\\' && str[i - 1] == L'\\') {
                // 检查是否是很多个连续的反斜杠
                size_t count = 2;
                size_t j = i + 1;
                while (j < str.length() && str[j] == L'\\') {
                    count++;
                    j++;
                }
                if (count >= 2) {
                    return true;
                }
                i = j - 1; // 跳过已检查的部分
            }
        }
        return false;
    }

    void populateVinaObject(VinaObject* vina_obj, const VinaStorageObjectMap& data_map) {
        // 由于 data_map 现在是 tsl::ordered_map，迭代顺序将严格按照文件中的顺序
        for (const auto& item : data_map) {
            const std::wstring& key = item.first;
            const VinaStorageValue& value_variant = item.second;

            std::visit([vina_obj, &key, this](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, int>) {
                    vina_obj->AddData(key, std::to_wstring(val));
                }
                else if constexpr (std::is_same_v<T, double>) {
                    // 使用足够精度保证 double 写入后能正确读回
                    std::wstringstream ws;
                    ws << val;
                    vina_obj->AddData(key, ws.str());
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    vina_obj->AddData(key, val ? L"true" : L"false");
                }
                else if constexpr (std::is_same_v<T, std::wstring>) {
                    std::wstring val_str = val; // 获取内存中的字面值

                    std::wstring escaped_val;

                    // 始终对字符串内容进行转义并添加引号
                    // 不再跳过含连续反斜杠的字符串，防止 Load->Save->Load 数据不一致
                    // 预留转义后的空间，至少 (length + 2 for quotes)
                    escaped_val.reserve(val_str.length() * 2 + 2);

                    for (wchar_t c : val_str) {
                        if (c == L'\\') {
                            escaped_val += L"\\\\"; // 遇到字面值 '\' 转换为 '\\' (写入文件时正确)
                        }
                        else if (c == L'"') { // 增加对双引号的转义
                            escaped_val += L"\\\"";
                        }
                        else {
                            escaped_val += c;
                        }
                    }

                    // 对于正常的字符串内容，加上引号并使用转义后的字符串
                    vina_obj->AddData(key, L"\"" + escaped_val + L"\"");

                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<VinaStorageNestedObject>>) {
                    if (val) {
                        VinaObject* nested_vina_obj = vina_obj->AddObject(key);
                        populateVinaObject(nested_vina_obj, val->data);
                    }
                }
                }, value_variant);
        }
    }

private:
    bool LoadInternal(const std::wstring& filename) {
#ifdef _WIN32
        std::ifstream file_stream(filename);
        if (!file_stream.is_open()) {
            return false;
        }

        std::stringstream buffer;
        buffer << file_stream.rdbuf();
        std::string utf8_content = buffer.str();
        std::wstring wcontent = UTF8ToWString(utf8_content);
        std::wstringstream wide_stream(wcontent);
        vui::parser::basic_parser<std::wstringstream, wchar_t> parser(std::move(wide_stream));
#else
        std::wfstream file_stream(filename, std::ios::in);
        if (!file_stream.is_open()) {
            return false;
        }
        vui::parser::basic_parser<std::wfstream, wchar_t> parser(std::move(file_stream));
#endif

        if (!parser.parse()) {
            return false;
        }

        root_objects_.clear();
        for (auto parsed_root_obj : parser) {
            std::wstring obj_name = parsed_root_obj.name();
            VinaStorageObjectMap converted_map = convertBasicObjectToMap(parsed_root_obj);
            root_objects_[obj_name] = converted_map;
        }

        return true;
    }


public:

    VinaStorage() : filename_(L""), loaded_(false) {}

    void SetBackupPath(const std::wstring& backup_path) {
        backup_path_ = backup_path;
    }

    void Load(const std::wstring& filename) {
        filename_ = filename;
        loaded_ = false;

        // Check if primary file exists
#ifdef _WIN32
        DWORD dwAttrs = GetFileAttributesW(filename.c_str());
        bool file_exists = (dwAttrs != INVALID_FILE_ATTRIBUTES && !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY));
#else
        std::wifstream file_check(filename);
        bool file_exists = file_check.good();
        file_check.close();
#endif

        if (!file_exists) {
            std::wcout << L"Info: File '" << filename << L"' does not exist. Creating new storage." << std::endl;
            root_objects_.clear();
            loaded_ = true;
            return;
        }

        // Try loading primary file
        if (LoadInternal(filename)) {
            loaded_ = true;
            std::wcout << L"VinaStorage loaded from '" << filename << L"' (" << root_objects_.size() << L" root objects)." << std::endl;
            return;
        }

        // If primary file failed to load, try backup file
        std::wstring backup_filename = backup_path_.empty() ? (filename + L".bak") : backup_path_;
#ifdef _WIN32
        DWORD dwBackupAttrs = GetFileAttributesW(backup_filename.c_str());
        bool backup_exists = (dwBackupAttrs != INVALID_FILE_ATTRIBUTES && !(dwBackupAttrs & FILE_ATTRIBUTE_DIRECTORY));
#else
        std::wifstream backup_check(backup_filename);
        bool backup_exists = backup_check.good();
        backup_check.close();
#endif

        if (backup_exists) {
            std::wcerr << L"Warning: Failed to load primary file '" << filename << L"'. Attempting to restore from backup '" << backup_filename << L"'." << std::endl;
            if (LoadInternal(backup_filename)) {
                loaded_ = true;
                std::wcout << L"VinaStorage successfully restored and loaded from backup." << std::endl;
                
                // Copy backup back to primary file to fix it
#ifdef _WIN32
                CopyFileW(backup_filename.c_str(), filename.c_str(), FALSE);
#else
                std::ifstream src(backup_filename, std::ios::binary);
                std::ofstream dst(filename, std::ios::binary);
                dst << src.rdbuf();
#endif
                return;
            }
        }

        // If both failed or backup doesn't exist, we rename the corrupted file to .corrupted,
        // and then load an empty configuration to prevent crash.
        std::wcerr << L"Error: Failed to load both primary and backup storage files for '" << filename << L"'. Resetting to empty configuration." << std::endl;
        
        std::wstring corrupted_filename = filename + L".corrupted";
#ifdef _WIN32
        MoveFileExW(filename.c_str(), corrupted_filename.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
        std::rename(WStringToUTF8(filename).c_str(), WStringToUTF8(corrupted_filename).c_str());
#endif

        root_objects_.clear();
        loaded_ = true;
    }

    // Check if a file is currently loaded
    bool IsLoaded() const {
        return loaded_;
    }

    // Save
    void Save() {
        if (!loaded_ || filename_.empty()) {
            throw std::runtime_error("Cannot save: No file is currently loaded or filename is not set.");
        }

        VinaBuilder builder;

        // Iteration over root_objects_ is now ordered as well
        for (const auto& [obj_name, obj_data] : root_objects_) {
            VinaObject* root_obj = builder.AddObject(obj_name);
            populateVinaObject(root_obj, obj_data);
        }

        std::wstring wcontent = builder.GetContent();

#ifdef _WIN32
        std::string utf8_content = WStringToUTF8(wcontent);

        std::wstring temp_filename = filename_ + L".tmp";
        std::wstring backup_filename = backup_path_.empty() ? (filename_ + L".bak") : backup_path_;

        // 1. Ensure parent directory of backup_filename exists
        {
            std::filesystem::path p(backup_filename);
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        // 2. Write content to temp file
        {
            std::ofstream file_stream(temp_filename, std::ios::out | std::ios::binary);
            if (!file_stream.is_open()) {
                std::string filename_utf8 = WStringToUTF8(temp_filename);
                throw std::runtime_error("Could not open temp file for writing: " + filename_utf8);
            }
            file_stream << utf8_content;
            file_stream.flush();
            if (!file_stream.good()) {
                std::string filename_utf8 = WStringToUTF8(temp_filename);
                throw std::runtime_error("Error writing to temp file: " + filename_utf8);
            }
        } // file_stream closed here

        // 3. Flush OS file buffers to physical storage to prevent data loss on power cut / forced shutdown
        HANDLE hFile = CreateFileW(
            temp_filename.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_WRITE_THROUGH,
            NULL
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
        }

        // 4. Atomically replace the original file with retry loop for anti-virus/sync locks
        DWORD dwAttrs = GetFileAttributesW(filename_.c_str());
        bool original_exists = (dwAttrs != INVALID_FILE_ATTRIBUTES && !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY));

        bool replace_success = false;
        int max_retries = 5;
        for (int i = 0; i < max_retries; ++i) {
            if (original_exists) {
                // Replace original file atomically and create backup
                if (ReplaceFileW(filename_.c_str(), temp_filename.c_str(), backup_filename.c_str(), REPLACEFILE_WRITE_THROUGH, NULL, NULL)) {
                    replace_success = true;
                    break;
                }
            } else {
                // Original file does not exist, just rename temp to original
                if (MoveFileExW(temp_filename.c_str(), filename_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    replace_success = true;
                    break;
                }
            }
            
            DWORD err = GetLastError();
            // If it's a sharing violation or access denied, sleep and retry
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
                Sleep(20 + i * 20); // Exponential backoff: 20ms, 40ms, 60ms, 80ms, 100ms
                continue;
            } else {
                break; // Other error, exit retry loop
            }
        }

        // 5. Fallback if ReplaceFileW / MoveFileExW failed
        if (!replace_success) {
            std::wcerr << L"Warning: Atomic save failed (error " << GetLastError() << L"). Falling back to manual backup copy." << std::endl;
            
            // Manual backup copy
            if (original_exists) {
                CopyFileW(filename_.c_str(), backup_filename.c_str(), FALSE);
            }
            
            // Move temp to original with retry loop
            bool move_success = false;
            for (int i = 0; i < max_retries; ++i) {
                if (MoveFileExW(temp_filename.c_str(), filename_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    move_success = true;
                    break;
                }
                DWORD err = GetLastError();
                if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
                    Sleep(20 + i * 20);
                    continue;
                } else {
                    break;
                }
            }

            if (!move_success) {
                DeleteFileW(temp_filename.c_str());
                std::string filename_utf8 = WStringToUTF8(filename_);
                throw std::runtime_error("Could not replace original file (MoveFileExW fallback): " + filename_utf8 + " (Error code: " + std::to_string(GetLastError()) + ")");
            }
        }
#else
        // Non-Windows platform fallback
        std::wstring temp_filename = filename_ + L".tmp";
        std::wstring backup_filename = backup_path_.empty() ? (filename_ + L".bak") : backup_path_;

        // Ensure parent directory exists
        {
            std::filesystem::path p(backup_filename);
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        if (!builder.SaveToFile(temp_filename)) {
            throw std::runtime_error("Could not save to temp file: " + std::string(temp_filename.begin(), temp_filename.end()));
        }

        // Create backup of old file if it exists
        std::wifstream check(filename_);
        if (check.good()) {
            check.close();
            std::rename(WStringToUTF8(filename_).c_str(), WStringToUTF8(backup_filename).c_str());
        }

        // Rename temp to original
        if (std::rename(WStringToUTF8(temp_filename).c_str(), WStringToUTF8(filename_).c_str()) != 0) {
            throw std::runtime_error("Could not rename temp file to original path.");
        }
#endif
        std::wcout << L"VinaStorage saved to '" << filename_ << L"'." << std::endl;
    }

    class Proxy {
    private:
        // map_ptr_ 指向当前 Proxy 应该操作的 Map (root_objects_ / nested_obj_ptr->data)
        VinaStorageObjectMap* map_ptr_;
        // current_key_ 是 map_ptr_ 中要操作的键（对于 root proxy, current_key_ 是空字符串）
        std::wstring current_key_;
        // 只有当 is_root_proxy 为 true 时，map_ptr_ 才直接指向 root_objects_ 中的一个元素
        bool is_root_proxy;

    public:
        // 迭代器返回 pair<const std::wstring, VinaStorageValue&>
        using MapIterator = VinaStorageObjectMap::iterator;
        using PairType = std::pair<const std::wstring, VinaStorageValue&>;

        class Iterator {
        private:
            MapIterator current_it_;
            bool is_null_;

        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = PairType;
            using difference_type = std::ptrdiff_t;
            using pointer = PairType*; // 无法直接使用指针，只能用引用的指针或代理
            using reference = PairType;// 返回 pair<const WString, VinaStorageValue&>

            // 构造函数
            Iterator(MapIterator it) : current_it_(it), is_null_(false) {}

            // 默认构造函数 (null iterator)
            Iterator() : is_null_(true) {}

            // 解引用操作符
            reference operator*() const {
                if (is_null_) {
                    throw std::runtime_error("Dereferencing null VinaStorage::Proxy::Iterator");
                }
                // 强制转换以匹配 PairType 的 VinaStorageValue&
                return { current_it_->first, const_cast<VinaStorageValue&>(current_it_->second) };
            }

            // 前置自增
            Iterator& operator++() {
                if (!is_null_) {
                    ++current_it_;
                }
                return *this;
            }

            // 后置自增
            Iterator operator++(int) {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            // 相等性比较
            bool operator==(const Iterator& other) const {
                if (is_null_ && other.is_null_) {
                    return true;
                }
                if (is_null_ || other.is_null_) {
                    return false;
                }
                return current_it_ == other.current_it_;
            }

            // 不相等性比较
            bool operator!=(const Iterator& other) const {
                return !(*this == other);
            }
        };

        // 用于范围 for 循环的 begin 方法 (新增)
        Iterator begin() {
            // 尝试获取当前 Proxy 所代表的 map
            VinaStorageObjectMap* target_map = nullptr;
            if (is_root_proxy) {
                target_map = map_ptr_;
            }
            else if (map_ptr_) {
                auto it = map_ptr_->find(current_key_);
                if (it != map_ptr_->end() && std::holds_alternative<std::shared_ptr<VinaStorageNestedObject>>(it->second)) {
                    auto nested_obj_ptr = std::get<std::shared_ptr<VinaStorageNestedObject>>(it->second);
                    if (nested_obj_ptr) {
                        target_map = &nested_obj_ptr->data;
                    }
                }
            }

            if (target_map) {
                return Iterator(target_map->begin());
            }
            // 如果不是嵌套对象或根对象，则返回一个空迭代器
            return Iterator();
        }

        // 用于范围 for 循环的 end 方法 (新增)
        Iterator end() {
            VinaStorageObjectMap* target_map = nullptr;
            if (is_root_proxy) {
                target_map = map_ptr_;
            }
            else if (map_ptr_) {
                auto it = map_ptr_->find(current_key_);
                if (it != map_ptr_->end() && std::holds_alternative<std::shared_ptr<VinaStorageNestedObject>>(it->second)) {
                    auto nested_obj_ptr = std::get<std::shared_ptr<VinaStorageNestedObject>>(it->second);
                    if (nested_obj_ptr) {
                        target_map = &nested_obj_ptr->data;
                    }
                }
            }

            if (target_map) {
                return Iterator(target_map->end());
            }
            return Iterator();
        }


        Proxy(VinaStorageObjectMap* map, const std::wstring& key, bool is_root = true)
            : map_ptr_(map), current_key_(key), is_root_proxy(is_root) {
        }

        Proxy(VinaStorageObjectMap* map, const std::wstring& key)
            : map_ptr_(map), current_key_(key), is_root_proxy(false) {
        }

        Proxy operator[](const std::wstring& key) {
            if (is_root_proxy) {
                return Proxy(map_ptr_, key, false);
            }
            else {
                if (!map_ptr_) {
                    // 防止空指针解引用崩溃
                    throw std::runtime_error("VinaStorage::Proxy: internal map pointer is null.");
                }
                // Nested proxy: 先找到 current_key_ 对应的 VinaStorageNestedObject
                auto it = map_ptr_->find(current_key_);
                if (it == map_ptr_->end()) {
                    auto new_nested_obj_ptr = std::make_shared<VinaStorageNestedObject>();
                    (*map_ptr_)[current_key_] = new_nested_obj_ptr;
                    it = map_ptr_->find(current_key_);
                }

                if (std::holds_alternative<std::shared_ptr<VinaStorageNestedObject>>(it->second)) {
                    auto nested_obj_ptr = std::get<std::shared_ptr<VinaStorageNestedObject>>(it->second);
                    if (!nested_obj_ptr) {
                        // 如果指针为空，创建一个新的
                        nested_obj_ptr = std::make_shared<VinaStorageNestedObject>();
                        (*map_ptr_)[current_key_] = nested_obj_ptr; // 更新 map_ptr_
                    }
                    // 返回一个指向 nested_obj_ptr->data 的 Proxy
                    return Proxy(&nested_obj_ptr->data, key, false);
                }
                else {
                    auto new_nested_obj_ptr = std::make_shared<VinaStorageNestedObject>();
                    (*map_ptr_)[current_key_] = new_nested_obj_ptr;
                    // 返回一个指向新嵌套对象->data 的 Proxy
                    return Proxy(&new_nested_obj_ptr->data, key, false);
                }
            }
        }

        template<typename T>
        void operator=(const T& value) {
            if (is_root_proxy) {
                std::wcerr << L"Warning: Attempting to assign value to a root proxy directly. Use storage[L\"obj\"][L\"key\"] = value; instead." << std::endl;
                return;
            }

            if (!map_ptr_) {
                std::wcerr << L"Error: Internal map pointer is null during assignment for key '" << current_key_ << L"'." << std::endl;
                return;
            }

            using DecayedT = std::decay_t<T>;
            // 宽字符串字面量和指针
            if constexpr (std::is_same_v<DecayedT, const wchar_t*>) {
                if (value) {
                    (*map_ptr_)[current_key_] = std::wstring(value);
                } else {
                    (*map_ptr_)[current_key_] = std::wstring();
                }
            }
            else if constexpr (std::is_same_v<DecayedT, wchar_t*>) {
                if (value) {
                    (*map_ptr_)[current_key_] = std::wstring(value);
                } else {
                    (*map_ptr_)[current_key_] = std::wstring();
                }
            }
            // 窄字符串字面量和指针 (转换为宽字符串)
            else if constexpr (std::is_same_v<DecayedT, const char*>) {
                // 假设值是 ASCII 或单字节编码，进行简单转换
                if (value) {
                    std::string s(value);
                    (*map_ptr_)[current_key_] = std::wstring(s.begin(), s.end());
                } else {
                    (*map_ptr_)[current_key_] = std::wstring();
                }
            }
            else if constexpr (std::is_same_v<DecayedT, char*>) {
                // 假设值是 ASCII 或单字节编码，进行简单转换
                if (value) {
                    std::string s(value);
                    (*map_ptr_)[current_key_] = std::wstring(s.begin(), s.end());
                } else {
                    (*map_ptr_)[current_key_] = std::wstring();
                }
            }
            // 其他可直接存入 VinaStorageValue 的类型
            else if constexpr (std::is_same_v<DecayedT, int> ||
                std::is_same_v<DecayedT, double> ||
                std::is_same_v<DecayedT, bool> ||
                std::is_same_v<DecayedT, std::wstring>) {
                (*map_ptr_)[current_key_] = value;
            }
            else {
                // 尝试将其他类型转换为 std::wstring 或抛出错误
                // 由于不知道 T 的具体类型，这里只处理上面列出的，其他情况可能需要额外的 to_wstring 或 cast
                std::wcerr << L"Warning: Unsupported type assignment for key '" << current_key_ << L"'. Skipping." << std::endl;
            }
        }

        template<typename T>
        T get(const T& default_value = T{}) const {
            if (is_root_proxy) {
                // 根代理不持有值，它只代表一个 Map
                std::wcerr << L"Warning: Attempting to get value from a root object proxy directly. Must use [] to access a key." << std::endl;
                return default_value;
            }

            if (!map_ptr_) {
                return default_value;
            }
            auto it = map_ptr_->find(current_key_);
            if (it == map_ptr_->end()) {
                return default_value;
            }
            try {
                // 尝试从 VinaStorageValue 中直接获取 T
                if constexpr (std::is_same_v<T, std::wstring>) {
                    return std::get<T>(it->second);
                }
                else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                    // 如果请求的是整数类型，尝试 int，如果失败则尝试 double
                    try {
                        return (T)std::get<int>(it->second);
                    }
                    catch (const std::bad_variant_access&) {
                        try {
                            // 允许从 double 隐式转换
                            return (T)std::get<double>(it->second);
                        }
                        catch (const std::bad_variant_access&) {
                            // 进一步：尝试从 string 转换 (可选)
                            return default_value;
                        }
                    }
                }
                else if constexpr (std::is_floating_point_v<T>) {
                    // 如果请求的是浮点数，尝试 double，如果失败则尝试 int
                    try {
                        return (T)std::get<double>(it->second);
                    }
                    catch (const std::bad_variant_access&) {
                        try {
                            // 允许从 int 隐式转换
                            return (T)std::get<int>(it->second);
                        }
                        catch (const std::bad_variant_access&) {
                            return default_value;
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    return std::get<T>(it->second);
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<VinaStorageNestedObject>>) {
                    return std::get<T>(it->second);
                }
                else {
                    // 默认的 std::get 行为
                    return std::get<T>(it->second);
                }
            }
            catch (const std::bad_variant_access&) {
                return default_value;
            }
        }

        bool remove() {
            if (is_root_proxy) {
                std::wcerr << L"Error: Cannot call remove() on a root object proxy directly. Use VinaStorage::RemoveRootObject() instead if needed." << std::endl;
                return false;
            }
            if (!map_ptr_) {
                return false;
            }
            size_t count = map_ptr_->erase(current_key_);
            return count > 0;
        }

        const VinaStorageObjectMap* GetMapPtr() const {
            if (is_root_proxy) {
                return map_ptr_;
            }
            return nullptr;
        }
    };

    Proxy operator[](const std::wstring& root_obj_name) {
        auto it = root_objects_.find(root_obj_name);
        if (it == root_objects_.end()) {
            // 如果不存在，插入一个空的根对象 Map
            root_objects_[root_obj_name] = VinaStorageObjectMap{};
            it = root_objects_.find(root_obj_name);
        }
        // Proxy(MapPtr, Key, is_root)
        // 对于根代理，map_ptr_ 指向根对象 map，current_key_ 是空字符串，is_root_proxy 为 true
        return Proxy(const_cast<VinaStorageObjectMap*>(&it->second), std::wstring(L""), true);
    }

    // 专门移除根对象的方法（Proxy::remove() 不允许移除根对象）
    bool RemoveRootObject(const std::wstring& root_obj_name) {
        return root_objects_.erase(root_obj_name) > 0;
    }

    template<typename T>
    T GetFromRoot(const std::wstring& root_obj_name, const std::wstring& key, const T& default_value = T{}) {
        if (!loaded_) {
            return default_value;
        }
        auto root_it = root_objects_.find(root_obj_name);
        if (root_it != root_objects_.end()) {
            auto& root_map = root_it->second;
            auto it = root_map.find(key);
            if (it != root_map.end()) {
                try {
                    return std::get<T>(it->second);
                }
                catch (const std::bad_variant_access&) {
                    return default_value;
                }
            }
        }
        return default_value;
    }

    const VinaStorageObjectMap& GetRootObjectMap(const std::wstring& root_obj_name) const {
        auto it = root_objects_.find(root_obj_name);
        if (it == root_objects_.end()) {
            throw std::out_of_range("Root object not found: " + std::string(root_obj_name.begin(), root_obj_name.end()));
        }
        return it->second;
    }

    // END: 保持不变的代码片段
};

inline void print_value(const VinaStorageValue& value) {
    std::visit([](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>)
            std::wcout << L" (int): " << arg;
        else if constexpr (std::is_same_v<T, double>)
            std::wcout << L" (double): " << arg;
        else if constexpr (std::is_same_v<T, bool>)
            std::wcout << L" (bool): " << (arg ? L"true" : L"false");
        else if constexpr (std::is_same_v<T, std::wstring>)
            std::wcout << L" (wstring): " << arg;
        else if constexpr (std::is_same_v<T, std::shared_ptr<VinaStorageNestedObject>>)
            std::wcout << L" (Nested Object) -> " << (arg ? arg->data.size() : 0) << L" sub-keys";
        else
            std::wcout << L" (Unknown Type)";
        }, value);
}

inline void traverse_object_map(const VinaStorageObjectMap& data_map, int depth = 0) {
    std::wstring indent(depth * 4, L' ');
    for (const auto& pair : data_map) {
        std::wcout << indent << L"Key: " << pair.first;
        print_value(pair.second);
        std::wcout << std::endl;

        if (std::holds_alternative<std::shared_ptr<VinaStorageNestedObject>>(pair.second)) {
            auto nested_obj_ptr = std::get<std::shared_ptr<VinaStorageNestedObject>>(pair.second);
            if (nested_obj_ptr) {
                traverse_object_map(nested_obj_ptr->data, depth + 1);
            }
        }
    }
}

#endif // VINA_STORAGE_HPP