#include "stream_server.hh"
#include <spdlog/spdlog.h>

int main() {
  try {
    spdlog::info("Starting Streaming Server");
    boost::asio::io_context io_context;
    short stream_port = 1234;
    short http_port = 2345;

    StreamingServer server(io_context, stream_port, http_port);
    io_context.run();
  } catch (const std::exception& e) {
    spdlog::error("Exception: {}", e.what());
  }
  return 0;
}
