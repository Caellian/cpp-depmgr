#ifndef _DEPMGR_CMAKE_HPP_
#define _DEPMGR_CMAKE_HPP_

#include <string>
#include <vector>

#include "toml.hpp"

struct cmake_option
{
  std::string name;
  std::string value;
  bool force;

  cmake_option(const char *name, toml_raw_t *raw, bool force = false);
  std::string to_command(size_t indent = 0) const;
};

struct cmake_option_list
{
  std::vector<cmake_option> options;

  cmake_option_list(toml_table_t *table, bool force = false);
  std::string to_commands(size_t indent = 0) const;
};

#endif /* _DEPMGR_CMAKE_HPP_ */
