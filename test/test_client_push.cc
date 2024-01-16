#include <boost/asio.hpp>
#include <httplib.h>
#include <iostream>
#include <spdlog/spdlog.h>

using boost::asio::ip::tcp;

int main() {
  spdlog::info("Starting client...");

  try {
    httplib::Client http_client(
        "http://127.0.0.1:2345"); // 替换为您的 HTTP 端口
    spdlog::info("Sending HTTP push request...");
    auto res = http_client.Post("/push", "stream_id=your_stream_id&enable=true",
                                "application/x-www-form-urlencoded");

    if (res && res->status == 200) {
      spdlog::info("HTTP push request sent successfully. Response: {}",
                   res->body);
    } else {
      spdlog::error("Failed to send HTTP push request.");
      return 1;
    }

    // TCP 连接部分
    boost::asio::io_context io_context;
    tcp::socket socket(io_context);
    spdlog::info("Connecting to TCP server...");
    socket.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                      1234)); // 替换为您的 TCP 端口

    for (int i = 0; i < 10000; ++i) {
      // 发送数字
      int num = i; // 示例数字
      spdlog::info("Sending number: {}", num);
      boost::asio::write(socket, boost::asio::buffer(&num, sizeof(num)));
    }
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
    return 1;
  }

  spdlog::info("Client finished successfully.");
  return 0;
}
