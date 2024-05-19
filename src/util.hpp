#ifndef _DEPMGR_UTIL_HPP_
#define _DEPMGR_UTIL_HPP_

#include <filesystem>

#include <fmt/format.h>

template<typename... T> FMT_NORETURN inline void critical_error(fmt::format_string<T...> fmt, T &&...args)
{
  fmt::println(stderr, "-- ERROR: {}", fmt::format(fmt, std::forward<T>(args)...));
  std::terminate();
}

template<typename... T> inline void status(fmt::format_string<T...> fmt, T &&...args)
{
  fmt::println(stdout, "-- {}", fmt::format(fmt, std::forward<T>(args)...));
}

void glob_copy(const std::string &source,
  const std::filesystem::path &target,
  const std::filesystem::path &base_path = "");

#endif /* _DEPMGR_UTIL_HPP_ */
