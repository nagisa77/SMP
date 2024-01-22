cmake_minimum_required(VERSION 3.10)
project(SMP)
cmake_policy(SET CMP0100 NEW)
set(CMAKE_CXX_STANDARD 14)

# # 启用 Clang-Tidy 的特定检查
# set(CMAKE_CXX_CLANG_TIDY 
#     clang-tidy;
#     -checks=-*,llvm-include-order,google-*-includes,modernize-*-includes;
#     -header-filter=.*)


find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

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
)

target_link_libraries(SMP
  spdlog::spdlog
  Boost::system
  Boost::thread
  nlohmann_json::nlohmann_json
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)
target_include_directories(SMP PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 添加测试可执行文件
add_executable(TestClientPull 
  test/test_client_pull.cc
  test/test_client_pull.hh
)
set_target_properties(TestClientPull PROPERTIES AUTOMOC ON)
target_link_libraries(TestClientPull
  spdlog::spdlog
  Boost::system
  Boost::thread
  Qt6::Core
  Qt6::Widgets
  nlohmann_json::nlohmann_json
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)
target_include_directories(TestClientPull PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(TestClientPush test/test_client_push.cc)
target_link_libraries(TestClientPush
  spdlog::spdlog
  Boost::system
  Boost::thread
  nlohmann_json::nlohmann_json
  ${AVFORMAT_LIBRARY}
  ${AVCODEC_LIBRARY}
  ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY}
)
target_include_directories(TestClientPush PRIVATE ${CMAKE_SOURCE_DIR}/include)
