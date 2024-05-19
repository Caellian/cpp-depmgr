#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "cmake.hpp"
#include "glob/glob.h"
#include "toml.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

/*
fs::path git_path_cache = "";

const fs::path &git_path()
{
  if (!git_path_cache.empty()) { return git_path_cache; }

  fs::path result;
  auto path = std::string(getenv("PATH"));
  if (path.empty()) { critical_error("PATH environment variable missing."); }

  size_t pos;
  while ((pos = path.find(':')) != std::string::npos) {
    fs::path it = path.substr(0, pos);
    fs::path candidate = it / GIT_BINARY;
    if (fs::exists(candidate)) {
      git_path_cache = candidate;
      break;
    }
    path.erase(0, pos + 1);
  }

  if (git_path_cache.empty()) { critical_error("Could not find git in PATH"); }
  return git_path_cache;
}

void git_clone(const std::string &url, const std::string &tag, const fs::path &dest)
{
  const fs::path &git = git_path();

  std::string cmd = fmt::format("{} clone {} --recurse-submodules {}", git, url, dest);
  fmt::println("Cloning: {}", cmd);

  if (system(cmd.c_str()) != 0) { critical_error("Can't clone {}", url); }
}
*/

enum class remote_kind { LOCAL, SVN, GIT, HG, CVS, URL };

std::optional<remote_kind> infer_kind(const toml_table_t *config)
{
  if (toml_has_key(config, "path")) return remote_kind::LOCAL;
  if (toml_has_key(config, "svn")) return remote_kind::SVN;
  if (toml_has_key(config, "git")) return remote_kind::GIT;
  if (toml_has_key(config, "hg")) return remote_kind::HG;
  if (toml_has_key(config, "cvs")) return remote_kind::CVS;
  if (toml_has_key(config, "url")) return remote_kind::URL;
  return std::nullopt;
};

class package
{
protected:
  std::string name;
  const toml_table_t *config;

  remote_kind kind;

  std::optional<fs::path> configure;
  std::optional<fs::path> cmake_lists;

  std::optional<cmake_option_list> options;
  std::optional<std::vector<std::string>> advanced_variables;

  bool vendor;

  package(const char *name, const toml_table_t *config) : name(name), config(config)
  {
    this->configure = toml_table_get<fs::path>(config, "configure");
    this->cmake_lists = toml_table_get<fs::path>(config, "cmake-lists");

    if (toml_table_t *options = toml_table_in(config, "options")) { this->options = cmake_option_list(options); }
    this->advanced_variables = toml_table_get<std::vector<std::string>>(config, "advanced-variables");

    this->vendor = toml_table_get<bool>(config, "vendor").value_or(false);
  }

public:
  virtual void write_fetch_rules(FILE *stream) = 0;

  virtual std::string is_downloaded_var() { return fmt::format("{}_POPULATED", name); }

  virtual std::string fetch_advanced_variables()
  {
    std::string upper_name = name;
    std::transform(name.begin(), name.end(), upper_name.begin(), ::toupper);
    return fmt::format(
      "mark_as_advanced(FETCHCONTENT_SOURCE_DIR_{0} FETCHCONTENT_UPDATES_DISCONNECTED_{0})\n", upper_name);
  }

  void write_configure_rules(FILE *stream)
  {

    std::string special_configure;
    if (configure.has_value()) {
      // TODO: relative path?
      special_configure = fmt::format(
        "  file(READ \"${{CMAKE_CURRENT_SOURCE_DIR}}/{0}\" DEPMGR_{1}_USER_CONFIGURATION)\n"
        "  cmake_language(EVAL CODE \"${{DEPMGR_{1}_USER_CONFIGURATION}}\")\n"
        "  unset(DEPMGR_{1}_USER_CONFIGURATION)\n",
        configure.value().string(),
        name);
    }

    std::string copy_makelists;
    if (cmake_lists.has_value()) {
      // TODO: relative path?
      // TODO: Not source_dir -> move to work dir
      copy_makelists = fmt::format(
        "  configure_file(\"${{CMAKE_CURRENT_SOURCE_DIR}}/{}\" \"${{{package}_SOURCE_DIR}}/CMakeLists.txt\" @ONLY)\n",
        cmake_lists.value().string(),
        name);
    }

    std::string actual_source_dir = "\"${{{package}_SOURCE_DIR}}\"";// TODO: work dir.
    if (vendor) {
      // TODO: vendor handling
    }

    std::string set_options;
    if (options.has_value()) { set_options = options.value().to_commands(2); }

    std::string mark_advanced;
    if (advanced_variables.has_value()) {
      mark_advanced = fmt::format("  mark_as_advanced({})\n", fmt::join(advanced_variables.value(), " "));
    }

    std::string fetch_advanced_vars = this->fetch_advanced_variables();

    fmt::print(stream,
      "\n"
      "set({package}_CONFIGURED TRUE)\n"
      "set({package}_WORK_DIR TRUE)\n"

      // TODO: populate_work_dir
      "fetchcontent_getproperties({package})\n"
      "if(NOT {is_downloaded})\n"
      "  fetchcontent_populate({package})\n"
      "  set({package}_CONFIGURED FALSE)\n"
      "endif()\n"
      "message(STATUS \"Dependency ready: {package}\")\n"

      "if(NOT {package}_CONFIGURED)\n"
      "  message(STATUS \"Configuring dependency: {package}\")\n"
      "  patch({package})\n"
      "{copy_makelists}"
      "{special_configure}"
      "  message(STATUS \"Configuring dependency: {package} - Done\")\n"
      "endif()\n"

      "block(SCOPE_FOR VARIABLES)\n"
      "{set_options}"
      "  add_subdirectory({actual_sources} \"${{{package}_BINARY_DIR}}\")\n"
      "{mark_advanced}"
      "endblock()\n"
      "{fetch_advanced_vars}"

      "unset({package}_CONFIGURED)\n"
      "unset({package}_WORK_DIR)\n"
      "\n",
      fmt::arg("package", name),
      fmt::arg("is_downloaded", this->is_downloaded_var()),
      fmt::arg("special_configure", special_configure),
      fmt::arg("copy_makelists", copy_makelists),
      fmt::arg("actual_sources", actual_source_dir),
      fmt::arg("set_options", set_options),
      fmt::arg("mark_advanced", mark_advanced),
      fmt::arg("fetch_advanced_vars", fetch_advanced_vars));
  }
};

struct package_local : public package
{
  fs::path path;

  package_local(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto path = toml_table_get<fs::path>(config, "path");
    if (!path.has_value()) critical_error("path not specified for {}", name);
    this->path = *path;
  }

  void write_fetch_rules(FILE *stream) {}
};
struct package_svn : public package
{
  std::string repo;

  std::optional<std::string> revision;

  package_svn(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto repo = toml_table_get<std::string>(config, "svn");
    if (!repo.has_value()) critical_error("git repository not specified for {}", name);
    this->repo = *repo;

    this->revision = toml_table_get<std::string>(config, "rev");
  }

  void write_fetch_rules(FILE *stream)
  {
    std::string options;

    if (revision.has_value()) { options += fmt::format("  SVN_REVISION -r{}\n", *revision); }

    fmt::print(stream,
      "fetchcontent_declare(\n"
      "  {package}\n"
      "  SVN_REPOSITORY {repo}\n"
      "{options}"
      "  SVN_TRUST_CERT TRUE\n"
      ")\n",
      fmt::arg("package", name),
      fmt::arg("repo", repo),
      fmt::arg("options", options));
  }
};
struct package_git : public package
{
  std::string repo;

  std::optional<std::string> tag;
  std::optional<std::string> remote;

  std::optional<std::vector<std::string>> submodules;

  package_git(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto repo = toml_table_get<std::string>(config, "git");
    if (!repo.has_value()) critical_error("git repository not specified for {}", name);
    this->repo = *repo;

    this->tag = toml_table_get<std::string>(config, "tag");
    this->remote = toml_table_get<std::string>(config, "remote");
    this->submodules = toml_table_get<std::vector<std::string>>(config, "submodules");
  }

  void write_fetch_rules(FILE *stream)
  {
    std::string options;

    if (tag.has_value()) { options += fmt::format("  GIT_TAG {}\n", *tag); }
    if (remote.has_value()) { options += fmt::format("  GIT_REPOSITORY {}\n", *remote); }
    if (submodules.has_value()) { options += fmt::format("  GIT_SUBMODULES {}\n", fmt::join(*submodules, " ")); }

    fmt::print(stream,
      "fetchcontent_declare(\n"
      "  {package}\n"
      "  GIT_REPOSITORY {repo}\n"
      "{options}"
      "  GIT_SHALLOW TRUE\n"
      ")\n",
      fmt::arg("package", name),
      fmt::arg("repo", repo),
      fmt::arg("options", options));
  }
};
struct package_hg : public package
{
  std::string repo;

  std::optional<std::string> tag;

  package_hg(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto repo = toml_table_get<std::string>(config, "hg");
    if (!repo.has_value()) critical_error("hg repository not specified for {}", name);
    this->repo = *repo;

    this->tag = toml_table_get<std::string>(config, "tag");
  }

  void write_fetch_rules(FILE *stream)
  {
    std::string options;

    if (tag.has_value()) { options += fmt::format("  HG_TAG {}\n", *tag); }

    fmt::print(stream,
      "fetchcontent_declare(\n"
      "  {package}\n"
      "  HG_REPOSITORY {repo}\n"
      "{options}"
      "  HG_SHALLOW TRUE\n"
      ")\n",
      fmt::arg("package", name),
      fmt::arg("repo", repo),
      fmt::arg("options", options));
  }
};
struct package_cvs : public package
{
  std::string repo;

  std::optional<std::string> mod;
  std::optional<std::string> tag;

  package_cvs(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto repo = toml_table_get<std::string>(config, "cvs");
    if (!repo.has_value()) critical_error("cvs repository not specified for {}", name);
    this->repo = *repo;

    this->mod = toml_table_get<std::string>(config, "module");
    this->tag = toml_table_get<std::string>(config, "tag");
  }

  void write_fetch_rules(FILE *stream)
  {
    std::string options;

    if (mod.has_value()) { options += fmt::format("  CVS_MODULE {}\n", *mod); }
    if (tag.has_value()) { options += fmt::format("  CVS_TAG {}\n", *tag); }

    fmt::print(stream,
      "fetchcontent_declare(\n"
      "  {package}\n"
      "  CVS_REPOSITORY {repo}\n"
      "{options}"
      ")\n",
      fmt::arg("package", name),
      fmt::arg("repo", repo),
      fmt::arg("options", options));
  }
};
struct package_url : public package
{
  std::string remote;

  std::optional<std::string> hash;
  std::optional<std::string> download_name;

  std::optional<std::string> username;
  std::optional<std::string> password;
  std::optional<std::vector<std::string>> headers;

  std::optional<fs::path> ca_file;

  package_url(const char *name, const toml_table_t *config) : package(name, config)
  {
    auto remote = toml_table_get<std::string>(config, "url");
    if (!remote.has_value()) critical_error("url not specified for {}", name);
    this->remote = *remote;

    this->hash = toml_table_get<std::string>(config, "hash");
    this->download_name = toml_table_get<std::string>(config, "download_name");

    this->username = toml_table_get<std::string>(config, "username");
    this->password = toml_table_get<std::string>(config, "password");
    this->headers = toml_table_get<std::vector<std::string>>(config, "headers");

    this->ca_file = toml_table_get<fs::path>(config, "ca_file");
  }

  void write_fetch_rules(FILE *stream)
  {
    std::string options;

    if (hash.has_value()) { options += fmt::format("  URL_HASH {}\n", *hash); }
    if (download_name.has_value()) { options += fmt::format("  DOWNLOAD_NAME {}\n", *download_name); }

    if (username.has_value()) { options += fmt::format("  URL_USERNAME {}\n", *username); }
    if (password.has_value()) { options += fmt::format("  URL_PASSWORD {}\n", *password); }
    if (headers.has_value()) { options += fmt::format("  URL_HEADER {}\n", fmt::join(*headers, " ")); }

    if (ca_file.has_value()) { options += fmt::format("  URL_CAINFO {}\n", ca_file.value().string()); }

    fmt::print(stream,
      "fetchcontent_declare(\n"
      "  {package}\n"
      "  URL {remote}\n"
      "{options}"
      ")\n",
      fmt::arg("package", name),
      fmt::arg("remote", remote),
      fmt::arg("options", options));
  }
};

std::unique_ptr<package> parse_package(const char *name, const toml_table_t *config)
{
  auto kind = infer_kind(config);
  if (!kind.has_value()) { critical_error("unknown remote type for '{}'", name); }

  switch (kind.value()) {
  case remote_kind::LOCAL:
    return std::make_unique<package_local>(name, config);
  case remote_kind::SVN:
    return std::make_unique<package_svn>(name, config);
  case remote_kind::GIT:
    return std::make_unique<package_git>(name, config);
  case remote_kind::HG:
    return std::make_unique<package_hg>(name, config);
  case remote_kind::CVS:
    return std::make_unique<package_cvs>(name, config);
  case remote_kind::URL:
    return std::make_unique<package_url>(name, config);
  }

  critical_error("unhandled remote type for '{}'", name);
}

int main(int argc, char *argv[])
{
  if (argc < 3 || strcmp(argv[1], "--help") == 0) {
    fmt::println("Usage: {} <dependencies.toml> <command_output> [options]", argv[0]);
    return argc < 3 ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  FILE *fp = fopen(argv[1], "r");
  if (fp == NULL) { critical_error("can't open dependency file: {}", argv[1]); }

  toml_table_t *config;
  {
    char err[256];
    config = toml_parse_file(fp, err, sizeof(err));
    fclose(fp);
    if (config == nullptr) { critical_error("can't parse TOML file: {}", err); }
  }

  std::vector<std::unique_ptr<package>> packages;

  size_t dep_count = toml_table_nkval(config);
  packages.reserve(dep_count);

  size_t i = 0;
  const char *dep_name = toml_key_in(config, 0);
  do {
    toml_table_t *data = toml_table_in(config, dep_name);
    packages.emplace_back(parse_package(dep_name, data));
    dep_name = toml_key_in(config, ++i);
  } while (dep_name != nullptr);

  toml_free(config);

  auto output = fs::path(argv[2]);
  {
    auto output_parent = output.parent_path();
    if (!fs::exists(output_parent)) { fs::create_directories(output_parent); }
  }

  FILE *output_stream = fopen(output.c_str(), "w+");

  fmt::print(output_stream,
    "include(FetchContent)\n"
    "block(SCOPE_FOR VARIABLES POLICIES)\n");

  for (auto &package : packages) { package->write_fetch_rules(output_stream); }
  for (auto &package : packages) { package->write_configure_rules(output_stream); }

  fmt::print(output_stream, "endblock()\n");

  return EXIT_SUCCESS;
}
