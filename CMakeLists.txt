cmake_minimum_required(VERSION 3.5)

project(
  depmgr
  VERSION 0.1.0
  LANGUAGES CXX C
  DESCRIPTION "A tool for processing dependencies"
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.\nRun `cmake -B <build_directory> [options...]` instead.")
endif()

add_subdirectory(thirdparty)

set(depmgr_sources
    src/cmake.cpp
    src/cmake.hpp
    src/main.cpp
    src/state.cpp
    src/state.hpp
    src/util.cpp
    src/util.hpp
)

add_executable(depmgr)
target_sources(depmgr PRIVATE ${depmgr_sources})
set_target_properties(
  depmgr
  PROPERTIES LANGUAGE CXX
             CXX_STANDARD 17
             CXX_EXTENSIONS FALSE
             OUTPUT_NAME "depmgr"
)
target_compile_options(depmgr PRIVATE "$<$<BOOL:${MSVC}>:/permissive->")
target_include_directories(depmgr PRIVATE ${THIRDPARTY_INCLUDE_DIRS})
target_link_libraries(depmgr PRIVATE ${THIRDPARTY_LIBS})
