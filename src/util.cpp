#include "util.hpp"

#include <vector>

#include "glob/glob.h"

namespace fs = std::filesystem;

void glob_copy(const std::string &source, const fs::path &target, const fs::path &base_path)
{
  std::string source_path = base_path / source;

  auto remaining_files = glob::rglob(source_path);
  std::vector<fs::path> visited;

  while (!remaining_files.empty()) {
    fs::path source_file = remaining_files.back();
    remaining_files.pop_back();

    fs::path parent = source_file;
    while (!parent.empty()) {
      for (auto &it : visited) {
        if (it == parent) { continue; }
      }
      parent = parent.parent_path();
    }

    // FIXME: LIKELY BROKEN
    if (fs::is_directory(source_file)) {
      fs::create_directories(target);
      for (const auto &entry : fs::recursive_directory_iterator(source_file)) {
        fs::path relative_path = fs::relative(entry.path(), source_file);
        fs::copy(entry.path(), target / relative_path);
      }
      visited.push_back(source_file);
    } else {
      fs::path target_dir = target.parent_path();
      if (!fs::exists(target_dir)) { fs::create_directories(target_dir); }
      fs::copy(source_file, target);
    }
  }
}
