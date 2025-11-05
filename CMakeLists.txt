cmake_minimum_required(VERSION 3.10)

# 项目名称和语言
project(cpp_project LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g")

add_subdirectory(third_party/prometheus-cpp)

find_package(PkgConfig REQUIRED)

add_subdirectory(third_party/spdlog-1.10.0)
include_directories(third_party/spdlog-1.10.0/include)

# 使用 pkg-config 查找 FFmpeg 各组件
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavcodec
  libavformat
  libavutil
  libswresample
  libavfilter
)

# 添加源文件
add_executable(myapp ./main.cpp av_metrics.cc audio_afade.cc logger.cc)

target_link_libraries(myapp
  PRIVATE
    prometheus-cpp::core
    prometheus-cpp::pull
    PkgConfig::FFMPEG
)
