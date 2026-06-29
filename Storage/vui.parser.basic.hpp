/**
 * @file vui.parser.basic.hpp
 * @author Haceau (haceau@qq.com)
 * @brief vui 解析器的通用基本类。
 * @version 0.2.0 (Modified for Vina syntax)
 * @date 2022-05-11
 * @copyright Copyright (c) 2022
 *
 */
#pragma once
#include <iterator>
#include <utility>
#ifndef VUI_PARSER_H_
#define VUI_PARSER_H_

#include <cctype>
#include <cinttypes>
#include <algorithm>
#include <any>
#include <string>
#include <unordered_map>
#include <locale>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

namespace vui::parser
{
    ///
    /// @class basic_value_pair <CharT>
    /// @brief 通用的 vui 键值对类。
    /// @param CharT [类型] 字符类型。
    ///
    template <typename CharT>
    class basic_value_pair
    {
    public:
        /// @brief 字符串类型。
        using string_type = std::basic_string<CharT>;
        /// @brief 原始类型。
        using raw_type = std::pair<string_type const, std::any>;

        /// @brief 初始化器。
        basic_value_pair(raw_type const& pair)
            : pair_(pair) { }

        /// @brief 获取数据。
        /// @param T [可选|类型] 获取的类型，默认为 `string_type`。
        /// @param result [返回] 获取到的数据。若获取失败，`result` 中的内容不改变。
        /// @return 成功返回 `true`，失败返回 `false`。
        template <typename T = string_type>
        bool get(T& result) const
        {
            std::any value{ pair_.second };
            if (value.type() != typeid(T) && value.type() != typeid(string_type))
                return false;
            result = std::any_cast<T>(value);
            return true;
        }

        /// @brief 获取名称。
        /// @return 名称。
        string_type name() const
        {
            return pair_.first;
        }

    private:
        raw_type pair_;
    };

    ///
    /// @class basic_object <CharT>
    /// @brief 通用的 vui 对象类。
    /// @param CharT 字符类型。
    ///
    template <typename CharT>
    class basic_object
    {
    public:
        /// @brief 对象使用的字符串类型。
        using string_type = std::basic_string<CharT>;
        /// @brief 原始对象类型。
        using object_type = std::unordered_map<string_type, std::any>;

        basic_object() {}
        basic_object(std::pair<string_type, basic_object<CharT>> const& pair)
            : obj_(pair.second.obj_), name_(pair.first)
        {
            name_.erase(std::remove(name_.begin(), name_.end(), '^'),
                name_.end());
        }
        basic_object(std::pair<string_type, basic_object<CharT>> const& pair, std::vector<string_type> const& order)
            : basic_object(pair) {
            order_ = order;
        }

        /// @brief 对象的迭代器。
        class iterator
        {
            friend class basic_object<CharT>;
        public:
            /// @brief 值类型。
            using value_type = basic_value_pair<CharT>;
            /// @brief 引用类型。
            using reference = value_type&;
            /// @brief 指针类型。
            using pointer = value_type*;

            iterator(object_type& objs, std::vector<string_type> const& order)
                : objs_(objs), order_(order) { }
            /// @brief 获取 vui 键值对。
            value_type operator*() { string_type name{ order_[pos_] }; return std::make_pair(name, objs_[name]); } const
                /// @brief 向前移动一位。
                iterator& operator++() { ++pos_; return *this; }
            /// @brief 向前移动一位，返回移动前的迭代器。
            iterator const& operator++(int) { auto it{ *this }; ++*this; return it; }
            /// @brief 向后移动一位。
            iterator& operator--() { --pos_; return *this; }
            /// @brief 向后移动一位，返回移动前的迭代器。
            iterator const& operator--(int) { auto it{ *this }; --*this; return *this; }
            /// @brief 判断两个迭代器是否相等。
            bool operator==(iterator const& other) { return objs_.begin() == other.objs_.begin() && objs_.end() == other.objs_.end() && pos_ == other.pos_; }
            /// @brief 判断两个迭代器是否不相等。
            bool operator!=(iterator const& other) { return !(*this == other); }
            /// @brief 访问对象。
            reference operator->() { return **this; }

        private:
            object_type& objs_;
            std::vector<string_type> const& order_;
            std::size_t pos_{ 0 };
        };

        /// @brief 获取对象的起始迭代器。
        /// @return 对象的起始迭代器。
        iterator begin()
        {
            return { obj_, order_ };
        }

        /// @brief 获取对象的结束迭代器。
        /// @return 对象的结束迭代器。
        iterator end()
        {
            iterator it(obj_, order_);
            it.pos_ = order_.size();
            return it;
        }

        /// @brief 获取对象中键为 `key` 的值的个数。
        /// @param key 要获取的键。
        auto count(string_type const& key) const
        {
            return obj_.count(key);
        }

        /// @brief 获取对象名。
        string_type name() const { return name_; };

        /// @brief 访问对象。
        /// @param key 要访问的键。
        std::any const& operator[](string_type const& key) const
        {
            return obj_[key];
        }

        /// @brief 访问对象。
        /// @param key 要访问的键。
        std::any& operator[](string_type const& key)
        {
            return obj_[key];
        }

        /// @brief 添加对象。
        void add(string_type const& key, std::any& value)
        {
            obj_[key] = value;
            order_.emplace_back(std::move(key));
        }

        /// @brief 添加对象（右值引用）。
        void add(string_type const& key, std::any&& value)
        {
            obj_[key] = std::move(value);
            order_.emplace_back(std::move(key));
        }

        /// @brief 获取数据顺序。
        std::vector<string_type> const& order() const
        {
            return order_;
        }

        /// @brief 判断两个对象是否相等。
        /// @param other 要判断的对象。
        /// @return 相等返回 `true`，不相等返回 `false`。
        bool operator==(object_type const& other) { return name_ == other.name_ && obj_ == other.obj_; }
        /// @brief 判断两个对象是否不相等。
        /// @param other 要判断的对象。
        /// @return 相等返回 `false`，不相等返回 `true`。
        bool operator!=(object_type const& other) { return !(*this == other); }

    private:
        string_type name_;
        object_type obj_;
        std::vector<string_type> order_;

    public:
        /// @brief 初始化器。
        basic_object(object_type&& obj)
            : obj_(obj) { }
    };

    ///
    /// @class basic_parser <StreamT, ArgT, CharT> 
    /// @brief 通用的 vui 格式解析类。
    /// @param StreamT 流类型。
    /// @param CharT 字符类型。
    ///
    template <typename StreamT, typename CharT>
    class basic_parser
    {
    public:

        /// @brief 解析器使用的字符串类型。
        using string_type = std::basic_string<CharT>;
        /// @brief 解析器使用的对象类型。
        using object_type = basic_object<CharT>;
        /// @brief 对象表。
        using objects_type = std::unordered_map<string_type, object_type>;

        /// @brief 初始化器。
    /// @param s 要解析的流。
        template<typename T>
        basic_parser(T const& s) noexcept // 保留旧的 const& 构造函数
            : stream_(s) { } // 对于可拷贝的类型，这仍然是有效的

        /// @brief 初始化器。
        /// @param s 要解析的流。
        template<typename T>
        basic_parser(T&& s) noexcept // 新增移动构造函数
            : stream_(std::forward<T>(s)) { } // 使用 std::forward 处理左值和右值

        /// @brief 初始化器。
        /// @param s 要解析的流。
        /// @param region 要解析的 region。
        template<typename T>
        basic_parser(T const& s, string_type const& region) noexcept
            : stream_(s)
            , region_(region) { }

        /// @brief 初始化器。
        /// @param s 要解析的流。
        /// @param region 要解析的 region。
        template<typename T>
        basic_parser(T&& s, string_type const& region) noexcept
            : stream_(std::forward<T>(s))
            , region_(region) { }

        /// @brief 设置 region。
        /// @param region 要解析的 region。
        void set_region(string_type const& region) noexcept { region_ = region; }
        /// @brief 获取 region。
        /// @return 要解析的的 region。
        string_type const& region() const noexcept { return region_; }

        /// @brief 获取数据。
        /// @param key 要获取的数据的名称。
        /// @param result [返回] 获取到的数据。若获取失败，`result` 中的内容不改变。
        /// @param name [可选] 要获取数据的对象的名称。默认为解析的第一个对象。
        /// @return 成功返回 `true`，失败返回 `false`。
        ///
        /// 若在 `get` 前未进行过 `parse`，将会自动执行一次 `parse`。
        /// 同名对象可使用 `same_name_object` 函数进行名称处理。
        template <typename T = string_type>
        bool get(string_type const& key, T& result, std::optional<string_type> const& name = std::nullopt) noexcept
        {
            if (!objs_.has_value())
            {
                parse();
            }
            auto& objs{ objs_.value() };
            object_type obj;
            if (!name.has_value())
                obj = objs.begin()->second;
            else if (!objs.count(name.value()))
                return false;
            else
                obj = objs[name.value()];

            if (!obj.count(key))
                return false;
            std::any value{ obj[key] };
            if (value.type() != typeid(T))
                return false;
            result = std::any_cast<T>(value);
            return true;
        }
        template <typename T = string_type>
        bool get2(string_type const& key, T& result, std::optional<string_type> const& name = std::nullopt) noexcept
        {
            auto& objs{ objs_.value() };
            object_type obj;
            if (!name.has_value())
                obj = objs.begin()->second;
            else if (!objs.count(name.value()))
                return false;
            else
                obj = objs[name.value()];

            if (!obj.count(key))
                return false;
            std::any value{ obj[key] };
            if (value.type() != typeid(T))
                return false;
            result = std::any_cast<T>(value);
            return true;
        }
        /// @brief 解析流中的数据。
        /// @return 成功返回 `true`，失败返回 `false`。
        /// 
        /// 只能 `parse` 一次，多次 `parse` 将返回 `false`。
        /// 对象名禁用 「^」。
        bool parse() noexcept
        {
            objs_ = std::unordered_map<string_type, object_type>{};
            CharT c{};
            stream_ >> std::noskipws;
            while ((!stream_.eof()) && (stream_ >> c))
            {
                switch (c)
                {
                case '#': return parse_preprocessor();
                default: {
                    if (!parse_object(c)) return false;
                    while ((c = skip_whitespace()) && !(stream_.eof()))
                    {
                        if (!parse_object(c)) return false;
                    }
                    return true;
                }
                }
            }
            return false;
        }

        /// @brief 对象的迭代器。
        class iterator
        {
            friend class basic_parser<StreamT, CharT>;
        public:
            /// @brief 值类型。
            using value_type = object_type;
            /// @brief 引用类型。
            using reference = value_type&;
            /// @brief 指针类型。
            using pointer = value_type*;

            /// @brief 获取 vui 对象。
            value_type operator*() { string_type name{ order_[pos_] }; return { std::make_pair(name, objs_[name]), objs_[name].order() }; } const
                /// @brief 向前移动一位。
                iterator& operator++() { ++pos_; return *this; }
            /// @brief 向前移动一位，返回移动前的迭代器。
            iterator const& operator++(int) { auto it{ *this }; ++*this; return it; }
            /// @brief 向后移动一位。
            iterator& operator--() { --pos_; return *this; }
            /// @brief 向后移动一位，返回移动前的迭代器。
            iterator const& operator--(int) { auto it{ *this }; --*this; return *this; }
            /// @brief 判断两个迭代器是否相等。
            bool operator==(iterator const& other) { return objs_.begin() == other.objs_.begin() && objs_.end() == other.objs_.end() && pos_ == other.pos_; }
            /// @brief 判断两个迭代器是否不相等。
            bool operator!=(iterator const& other) { return !(*this == other); }
            /// @brief 访问对象。
            reference operator->() { return **this; }

            iterator(std::unordered_map<string_type, object_type>& objs, std::vector<string_type> const& order)
                : objs_(objs), order_(order) { }

        private:
            std::unordered_map<string_type, object_type>& objs_;
            std::vector<string_type> const& order_;
            std::size_t pos_{ 0 };

            bool end() { return pos_ >= order_.size(); }
        };

        /// @brief 获取对象的起始迭代器。
        /// @return 对象的起始迭代器。
        iterator begin()
        {
            return { objs_.value(), order_ };
        }

        /// @brief 获取对象的结束迭代器。
        /// @return 对象的结束迭代器。
        iterator end()
        {
            iterator it(objs_.value(), order_);
            it.pos_ = it.order_.size();
            return it;
        }

    protected:
        StreamT stream_;
        std::optional<objects_type> objs_;
        std::vector<string_type> order_;
        string_type region_;

        bool parse_preprocessor() noexcept
        {
            if (region_.empty()) return true;

            CharT c{};
            bool is_region = false;
            while (!is_region)
            {
                if (!(stream_ >> c) || c != '#') return false;
                bool conti = false;
                for (auto const& reg : region_)
                    if (!(stream_ >> c) || c != reg)
                    {
                        skip_to('#');
                        for (std::size_t i = 0; i < 3; ++i)
                            if (!(stream_ >> c) || c != '#') return false;
                        conti = true;
                        break;
                    }
                if (conti) continue;
                is_region = true;
            }
            c = skip_whitespace();
            while (c != '#' && (!stream_.eof()))
            {
                parse_object(c);
                stream_ >> c;
            }
            if (stream_.eof()) return false;
            if (!(stream_ >> c) || c != '#') return false;
            if (!(stream_ >> c) || c != '#') return false;
            return true;
        }

        bool parse_members(object_type& obj) noexcept
        {
            CharT c = skip_whitespace();
            while (c != '}' && !stream_.eof())
            {
                /// 逗号处理
                if (c == ',') {
                    c = skip_whitespace();
                    if (c == '}') break;
                }

                /// 读取 Key
                string_type key;
                /// 读取直到遇到分隔符 (、:、{ 或空白
                while (c != '(' && c != ':' && c != '{' && !isspace(c) && c != ',' && c != '}') {
                    key += c;
                    stream_ >> c;
                }

                /// 跳过键名和分隔符之间的空白
                while (isspace(c)) stream_ >> c;

                CharT separator = c;
                std::any value;

                if (separator == '(') {
                    if (!read_value(value, c, [](CharT x) { return x == ')'; })) return false;
                    c = skip_whitespace();
                }
                else if (separator == ':') {
                    if (!read_value(value, c, [](CharT x) { return x == ',' || x == '}'; })) return false;
                }
                else if (separator == '{') {
                    object_type nested_obj;
                    if (!parse_members(nested_obj)) return false;
                    value = nested_obj;
                    c = skip_whitespace();
                }
                else {
                    return false;
                }

                obj.add(key, std::move(value));
            }
            return true;
        }

        bool parse_object(CharT c) noexcept
        {
            string_type name{ c };
            if (!read_to('{', name)) return false;

            object_type obj;
            if (!parse_members(obj)) return false;

            while (objs_->count(name)) name += '^';
            objs_.value()[name] = obj;
            if (!is_virtual_object(name))
                order_.emplace_back(std::move(name));
            return true;
        }

        CharT skip_whitespace() noexcept
        {
            CharT c{};
            while ((stream_ >> c) && c <= 255 && isspace(c));
            return c;
        }

        void skip_to(CharT end) noexcept
        {
            CharT c{};
            while ((stream_ >> c) && c != end);
        }

        bool read_to(CharT end, string_type& out) noexcept
        {
            CharT c{ skip_whitespace() };
            while ((c != end) && (!stream_.eof()))
            {
                out += c;
                stream_ >> c;
            }
            return !stream_.eof();
        }
        /// 新的read_value
        bool read_value(std::any& out, CharT& c, std::function<bool(CharT)> is_end) noexcept
        {
            bool is_integer = true, is_decimal = true;

            stream_ >> c;
            while (isspace(c)) stream_ >> c;

            string_type s{};
            bool is_negative{ false };


            while (!is_end(c) && (!stream_.eof()))
            {
                // *** 修复：值终止检查（针对非引号包裹的值）***
                if (!s.empty() && isspace(c)) {
                    // 如果 s 中已有内容（说明值已经开始读取），且当前字符是空白，则认为值已经结束。
                    stream_ >> c;
                    while (isspace(c) && !stream_.eof()) {
                        stream_ >> c;
                    }
                    break; // 跳出主循环
                }
                // ****************************************

                if (!isdigit(c))
                {
                    if (c == '"')
                    {
                        // 判断这个 " 是否被前面的反斜杠转义
                        // 计算 s 末尾连续反斜杠的数量，偶数个表示 \ 自身被转义了，" 不是转义的
                        size_t trailing_backslashes = 0;
                        for (auto rit = s.rbegin(); rit != s.rend() && *rit == '\\'; ++rit) {
                            ++trailing_backslashes;
                        }

                        if (s.empty() || trailing_backslashes % 2 == 0)
                        {
                            is_integer = is_decimal = false; // 明确标记为字符串

                            s += c;
                            stream_ >> c;
                            // 修复：正确判断引号结束——计算 " 前连续反斜杠数量，偶数个表示 " 是真结束符
                            while (!stream_.eof()) {
                                if (c == '"') {
                                    // 计算当前 s 末尾连续反斜杠的数量
                                    size_t bs_count = 0;
                                    for (auto rit = s.rbegin(); rit != s.rend() && *rit == '\\'; ++rit) {
                                        ++bs_count;
                                    }
                                    if (bs_count % 2 == 0) {
                                        // 偶数个反斜杠：" 是真正的结束引号
                                        break;
                                    }
                                }
                                s += c;
                                stream_ >> c;
                            }
                            s += c;
                            stream_ >> c;
                            continue;
                        }
                        else {
                            s.back() = '"';
                            c = '\0';
                        }
                    }
                    else if (!is_negative && c == '-') is_negative = true;
                    else
                    {
                        is_integer = false;
                        if (c != '.') is_decimal = false;
                    }
                }
                s += c;
                stream_ >> c;
            }

            // --- 类型转换 ---
            if (s.empty()) out = "";
            // 如果是带引号的字符串，剥离引号并进行反转义。
            // 将 \\ -> \, \" -> " 还原为字面值。
            else if (s.front() == '"' && s.back() == '"') {
                if (s.length() >= 2) {
                    string_type raw = s.substr(1, s.length() - 2);
                    // 反转义处理
                    string_type unescaped;
                    unescaped.reserve(raw.length());
                    for (size_t i = 0; i < raw.length(); ++i) {
                        if (raw[i] == '\\' && i + 1 < raw.length()) {
                            CharT next = raw[i + 1];
                            if (next == '\\') {
                                unescaped += '\\';
                                ++i; // 跳过下一个字符
                            }
                            else if (next == '"') {
                                unescaped += '"';
                                ++i;
                            }
                            else {
                                // 未知转义序列，保留原样
                                unescaped += raw[i];
                            }
                        }
                        else {
                            unescaped += raw[i];
                        }
                    }
                    out = unescaped;
                }
                else out = "";
            }
            // 数字类型判断：如果 is_integer/is_decimal 仍为 true，则转换为 int/double
            else if (is_integer && !s.empty() && s != string_type{ L"-" }) out = std::stoi(s);
            else if (is_decimal && !s.empty() && s != string_type{ L"." }&& s != string_type{ L"-." }) out = std::stod(s);
            // 布尔值判断
            else if (s == string_type{ L"true" }) out = true;
            else if (s == string_type{ L"false" }) out = false;
            // 默认解析为字符串
            else out = s;

            return true;
        }

        /// 兼容旧read_string
        bool read_string(string_type& out, bool& flag) noexcept
        {
            CharT c{ };
            stream_ >> c;
            while (!stream_.eof())
            {
                if (c == '"') {
                    // 计算 out 末尾连续反斜杠的数量
                    size_t bs_count = 0;
                    for (auto rit = out.rbegin(); rit != out.rend() && *rit == '\\'; ++rit) {
                        ++bs_count;
                    }
                    if (out.empty() || bs_count % 2 == 0) {
                        // 偶数个反斜杠：" 是真正的结束引号
                        stream_ >> c;
                        if (c == ')') { flag = true; break; }
                        else {
                            // 不是 )，说明这个引号后面还有内容
                            // 保留原有行为
                            out += '"';
                            continue; // c 已经是下一个字符了
                        }
                    }
                    else {
                        // 奇数个反斜杠：" 是被转义的
                        out.back() = '"';
                        stream_ >> c;
                        continue;
                    }
                }
                else {
                    out += c;
                }
                stream_ >> c;
            }
            return !stream_.eof();
        }
    };

    /// @brief 为同名对象创建标识符。
    /// @param object_name 对象名。
    /// @param id 同名对象的唯一标识符。
    /// @param split 对象名与标识符的分隔符。默认为 `:`。
    template<typename C>
    std::basic_string<C> same_name_object(std::basic_string<C> object_name, std::basic_string<C> id, C split = ':')
    {
        return object_name + split + id;
    }


    /// @brief 判断是否为虚对象。
    /// @param C [类型] 字符类型。
    /// @param object_name 对象名。
    /// @return 是虚对象返回 `true`，不是返回 `false`。
    /// 
    /// 以「$」或「@」开头的对象为「虚对象」。
    template<typename C>
    bool is_virtual_object(std::basic_string<C> object_name)
    {
        return  object_name[0] == '@';
    }
}

#endif // VUI_PARSER_H_