/**
 * @file vui.parser.hpp
 * @author Haceau (haceau@qq.com)
 * @brief vui 解析器的特化类。
 * @version 0.1.0
 * @date 2022-02-20
 * @copyright Copyright (c) 2022
 * 
 */
#pragma once
#ifndef VUI_PARSER_HPP_
#define VUI_PARSER_HPP_

#include "vui.parser.basic.hpp"
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>

namespace vui::parser
{
  /// @brief 解析字符串流。
  using parser = basic_parser<std::stringstream, char>;
  /// @brief 解析宽字符串流。
  using wparser = basic_parser<std::wstringstream, wchar_t>;

  /// @brief 解析文件流。
  using fparser = basic_parser<std::fstream, char>;
  /// @brief 以宽字符解析文件流。
  using wfparser = basic_parser<std::wfstream, wchar_t>;
}

#endif // VUI_PARSER_HPP_