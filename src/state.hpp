#ifndef _DEPMGR_STATE_HPP_
#define _DEPMGR_STATE_HPP_

#include <filesystem>

struct execution_context
{
  std::filesystem::path self_path;
  std::filesystem::path work_dir;
  std::filesystem::path dependency_cache_dir;

  execution_context &get();
};

#endif /* _DEPMGR_STATE_HPP_ */
