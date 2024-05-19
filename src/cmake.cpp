#include "cmake.hpp"

#include <algorithm>

#include <fmt/format.h>

#include "util.hpp"

cmake_option::cmake_option(const char *name, toml_raw_t *raw, bool force = false) : name(name), force(force)
{
  do {
    char *s;
    if (toml_rtos(*raw, &s) == 0) {
      this->value = std::string(s);
      delete s;
      break;
    }

    int boolean;
    if (toml_rtob(*raw, &boolean) == 0) {
      if (boolean) {
        this->value = "ON";
      } else {
        this->value = "OFF";
      }
      break;
    }

    int64_t integer;
    if (toml_rtoi(*raw, &integer) == 0) {
      this->value = std::to_string(integer);
      break;
    }

    double number;
    if (toml_rtod(*raw, &number) == 0) {
      this->value = std::to_string(number);
      break;
    }

    toml_timestamp_t ts;
    if (toml_rtots(*raw, &ts) == 0) {
      auto time_point = toml_timestamp_to_chrono<std::chrono::system_clock>(&ts);
      bool has_time = *ts.hour != 0 || *ts.minute != 0 || *ts.second != 0 || *ts.millisec != 0;

      if (has_time) {
        this->value = fmt::format("{0:%F}T{0:%T}.{1:03}Z", time_point, (*ts.millisec % 1000));
      } else {
        this->value = fmt::format("{:%F}", time_point);
      }
      break;
    }

    critical_error("unhandled option value type");
  } while (false);
}

std::string cmake_option::to_command(size_t indent) const
{
  return fmt::format("{: >{}}set({} \"{}\" CACHE INTERNAL \"\"{})\n", "", indent, name, value, force ? " FORCE" : "");
}

cmake_option_list::cmake_option_list(toml_table_t *table, bool force = false)
{
  size_t len = toml_table_nkval(table);
  options.reserve(len);
  for (size_t i = 0; i < len; i++) {
    const char *key = toml_key_in(table, i);
    toml_raw_t raw = toml_raw_in(table, key);
    options.emplace_back(cmake_option(key, &raw, force));
  }
}

std::string cmake_option_list::to_commands(size_t indent) const
{
  std::vector<std::string> option_strings;
  option_strings.reserve(options.size());
  std::transform(options.begin(), options.end(), std::back_inserter(option_strings), [&](const auto &o) {
    return o.to_command(indent);
  });
  return fmt::format("{}", fmt::join(option_strings, "\n"));
}