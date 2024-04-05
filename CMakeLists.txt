cmake_minimum_required(VERSION 3.0)
project(sylar-simple)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

include (cmake/utils.cmake)

# 编译flag
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -fPIC -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations -Wno-error=unused-result")
set(CMAKE_C_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -fPIC -ggdb -std=c11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations")
set(CMAKE_BUILD_TYPE "Debug")

# include flag
include_directories(.)
include_directories(/usr/include/jsoncpp)

# 是否开启测试标志
option(BUILD_TEST "ON for complile test" ON)

find_package(Boost REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

set(LIB_SRC
    sylar/util.cc
    sylar/env.cc
    sylar/config.cc
    sylar/fd_manager.cc
    sylar/fiber.cc
    sylar/hook.cc
    sylar/iomanager.cc
    sylar/log.cc
    sylar/mutex.cc
    sylar/scheduler.cc
    sylar/timer.cc
    sylar/thread.cc
)

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
        sylar
        pthread
        yaml-cpp
)

# test file
if(BUILD_TEST)
sylar_add_executable(test_log "tests/test_log.cc" sylar "${LIBS}")
sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
sylar_add_executable(test_iomanager "tests/test_iomanager.cc" sylar "${LIBS}")
endif()

# 设置bin和library的输出路径
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)