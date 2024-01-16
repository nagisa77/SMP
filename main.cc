#include "stream_server.hh"
#include <spdlog/spdlog.h>

std::atomic<bool> keep_running(true);

void on_timer(boost::asio::steady_timer* timer,
              const boost::system::error_code& /*e*/) {
  if (keep_running) {
    // 重新设置定时器
    timer->expires_after(std::chrono::seconds(1));
    timer->async_wait(std::bind(on_timer, timer, std::placeholders::_1));
  } else {
    // 清理并允许io_context.run返回
    delete timer;
  }
}

int main() {
  try {
    spdlog::info("Starting Streaming Server");
    boost::asio::io_context io_context;
    short stream_port = 10086;

    // 创建并启动循环定时器
    auto timer =
        new boost::asio::steady_timer(io_context, std::chrono::seconds(1));
    timer->async_wait(std::bind(on_timer, timer, std::placeholders::_1));

    StreamingServer server(io_context, stream_port);
    io_context.run();

  } catch (const std::exception& e) {
    spdlog::error("Exception: {}", e.what());
  }
  return 0;
}
