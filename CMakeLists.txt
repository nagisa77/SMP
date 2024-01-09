cmake_minimum_required(VERSION 3.10)
project(SMP)

set(CMAKE_CXX_STANDARD 14)

find_package(spdlog REQUIRED)

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)
find_library(SWRESAMPLE_LIBRARY swresample)

add_executable(SMP
  main.cc
  stream_server.cc
  stream_server.hh
  stream_interface.hh
)

target_link_libraries(SMP
  spdlog::spdlog
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)

# 添加测试可执行文件
add_executable(TestClientPull test/test_client_pull.cc)
target_link_libraries(TestClientPull
  spdlog::spdlog
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)
target_include_directories(TestClientPull PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(TestClientPush test/test_client_push.cc)
target_link_libraries(TestClientPush
  spdlog::spdlog
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)
target_include_directories(TestClientPush PRIVATE ${CMAKE_SOURCE_DIR}/include)
