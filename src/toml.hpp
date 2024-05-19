#ifndef _DEPMGR_TOML_HPP_
#define _DEPMGR_TOML_HPP_

#include <chrono>
#include <ctime>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <vector>

#include "toml.h"

template<class T, typename Alloc = std::allocator<T>> struct is_vector
{
  using element = void;
  using alloc = void;
  static bool const value = false;
};

template<class T, typename Alloc> struct is_vector<std::vector<T, Alloc>>
{
  using element = T;
  using alloc = Alloc;
  static bool const value = true;
};

template<class Clock> struct is_time_point
{
  using clock = void;
  static bool const value = false;
};

template<class Clock> struct is_time_point<std::chrono::time_point<Clock>>
{
  using clock = Clock;
  static bool const value = true;
};

inline bool toml_has_key(const toml_table_t *table, const char *key)
{
  if (toml_raw_in(table, key) != nullptr) {
    return true;
  } else if (toml_array_in(table, key) != nullptr) {
    return true;
  } else if (toml_table_in(table, key) != nullptr) {
    return true;
  }
  return false;
}

template<typename Clock> inline std::chrono::time_point<Clock> toml_timestamp_to_chrono(const toml_timestamp_t *ts)
{
  std::tm tm = {
    /* .tm_sec  = */ *ts->second,
    /* .tm_min  = */ *ts->minute,
    /* .tm_hour = */ *ts->hour,
    /* .tm_mday = */ *ts->day,
    /* .tm_mon  = */ *ts->month - 1,
    /* .tm_year = */ *ts->year - 1900,
  };
  tm.tm_isdst = -1;// Use DST value from local time zone
  auto result = Clock::from_time_t(std::mktime(&tm));
  return result + std::chrono::milliseconds(*ts->millisec);
}

template<typename Item> inline std::optional<std::vector<Item>> toml_array_to_vec(const toml_array_t *array)
{
  if (array == nullptr) return std::nullopt;
  std::vector<Item> result;
  size_t len = toml_array_nelem(array);
  result.reserve(len);
  for (size_t i = 0; i < len; i++) {
    auto raw = toml_raw_at(array, i);
    Item it;
    if constexpr (std::is_same_v<Item, std::string> || std::is_same_v<Item, std::filesystem::path>) {
      char *value;
      if (toml_rtos(raw, &value) == 0) { it = Item(std::string(value)); }
      delete value;
    } else if constexpr (std::is_same_v<Item, char *>) {
      char *value;
      if (toml_rtos(raw, &value) == 0) { it = value; }
      delete value;
    } else if constexpr (std::is_same_v<Item, bool>) {
      int value;
      if (toml_rtob(raw, &value) == 0) { it = bool(value); }
    } else if constexpr (std::is_integral_v<Item>) {
      int64_t value;
      if (toml_rtoi(raw, &value) == 0) { it = Item(value); }
    } else if constexpr (std::is_floating_point_v<Item>) {
      double value;
      if (toml_rtod(raw, &value) == 0) { it = Item(value); }
    } else if constexpr (std::is_same_v<Item, std::chrono::time_point<typename Item::clock>>) {
      toml_timestamp_t value;
      if (toml_rtots(raw, &value) == 0) { it = toml_timestamp_to_chrono<typename Item::clock>(&value); };
    } else {
      static_assert(false, "unsupported type");
    }
    result.emplace_back(it);
  }

  return result;
}

template<typename T> std::optional<T> toml_table_get(const toml_table_t *table, const char *key)
{
  if (table == nullptr) return std::nullopt;
  if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::filesystem::path>) {
    auto value = toml_string_in(table, key);
    if (value.ok) return T(std::string(value.u.s));
  } else if constexpr (std::is_same_v<T, char *>) {
    auto value = toml_string_in(table, key);
    if (value.ok) return value.u.s;
  } else if constexpr (std::is_same_v<T, bool>) {
    auto value = toml_bool_in(table, key);
    if (value.ok) return value.u.b;
  } else if constexpr (std::is_integral_v<T>) {
    auto value = toml_int_in(table, key);
    if (value.ok) return T(value.u.i);
  } else if constexpr (std::is_floating_point_v<T>) {
    auto value = toml_double_in(table, key);
    if (value.ok) return T(value.u.d);
  } else if constexpr (is_vector<T>::value) {
    auto array = toml_array_in(table, key);
    if (array != nullptr) return toml_array_to_vec<typename is_vector<T>::element>(array);
  } else if constexpr (is_time_point<T>::value) {
    auto value = toml_timestamp_in(table, key);
    if (value.ok) return toml_timestamp_to_chrono<typename is_time_point<T>::clock>(value.u.ts);
  } else {
    static_assert(false, "unsupported type");
  }
  return std::nullopt;
}

#endif /* _DEPMGR_TOML_HPP_ */
