#include <boost/asio.hpp>
#include <httplib.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using JSON = nlohmann::json;

using boost::asio::ip::tcp;

static void send_json(tcp::socket& socket, const JSON& json) {
  std::string json_str = json.dump();
  size_t json_length = json_str.size();
  std::vector<boost::asio::const_buffer> buffers;
  buffers.push_back(boost::asio::buffer(&json_length, sizeof(json_length)));
  buffers.push_back(boost::asio::buffer(json_str));
  boost::asio::write(socket, buffers);
}

static void receive_json(tcp::socket& socket, JSON& json) {
  std::uint32_t json_length;
  boost::asio::read(socket, boost::asio::buffer(&json_length, sizeof(json_length)));
  std::vector<char> json_str(json_length);
  boost::asio::read(socket, boost::asio::buffer(json_str.data(), json_length));
  json = JSON::parse(json_str.begin(), json_str.end());
}

int main() {
  spdlog::info("Starting client...");

  try {
    boost::asio::io_context io_context;
    tcp::socket socket(io_context);
    spdlog::info("Connecting to TCP server...");
    socket.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                      1234)); // 替换为您的 TCP 端口
    JSON json_obj;

    json_obj["is_push"] = true;
    json_obj["stream_id"] = "sub-video-tim";
    
    send_json(socket, json_obj);
    
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
    return 1;
  }

  spdlog::info("Client finished successfully.");
  return 0;
}
