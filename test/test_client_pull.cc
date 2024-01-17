#include <boost/asio.hpp>
#include <httplib.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
}

using JSON = nlohmann::json;

using boost::asio::ip::tcp;

enum MessageType {
  kTypeStreamInfo = 0,
  kTypePacket = 1,
};

static void send_json(tcp::socket& socket, const JSON& json) {
  std::string json_str = json.dump();
  spdlog::info("send json: {}", json_str);
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

static std::shared_ptr<AVPacket> receive_packet(tcp::socket& socket) {
  JSON json;
  receive_json(socket, json);
  
  int type = json["type"]; 
  int size = json["packet_size"];
  
  spdlog::info("receive json: {}", json.dump());
  
  char* packet_data = (char*) malloc(size * sizeof(char)); // leak
  boost::asio::read(socket, boost::asio::buffer(packet_data, size));
  static auto av_packet_deleter = [](AVPacket* pkt) {
    av_packet_free(&pkt);
  };
  std::shared_ptr<AVPacket> packet(av_packet_alloc(), av_packet_deleter);
  av_packet_from_data(packet.get(), (uint8_t*)packet_data, size);
  return packet;
}

int main() {
  spdlog::info("Starting Puller client...");

  try {
    boost::asio::io_context io_context;
    tcp::socket socket(io_context);
    spdlog::info("Connecting to TCP server...");
    socket.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                      10086)); // 替换为您的 TCP 端口
    JSON start_pull;
    start_pull["type"] = MessageType::kTypeStreamInfo;
    start_pull["is_push"] = false;
    start_pull["stream_id"] = "sub-video-tim";
    start_pull["enable"] = true;
    send_json(socket, start_pull);
    
    while (true) {
      std::shared_ptr<AVPacket> paket = receive_packet(socket);
    }
    
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
    return 1;
  }

  spdlog::info("Client finished successfully.");
  return 0;
}
