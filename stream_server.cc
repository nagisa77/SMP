
#include "stream_server.hh"
#include "stream_interface.hh"
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

extern "C" {
#include <libavutil/frame.h>
}

using JSON = nlohmann::json;

enum MessageType {
  kTypeStreamInfo = 0,
  kTypePacket = 1,
};

using WriteJSONComplete = std::function<void(const boost::system::error_code&, std::size_t)>;
using ReadJSONComplete = std::function<void(const JSON& json, const boost::system::error_code&, std::size_t)>;
using ReadAVPacketComplete = std::function<void(std::shared_ptr<AVPacket> packet, const boost::system::error_code&, std::size_t)>;
using WriteAVPacketComplete = std::function<void(const boost::system::error_code&, std::size_t)>;

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

static void send_json_async(std::shared_ptr<tcp::socket> socket, const JSON& json, WriteJSONComplete callback) {
  auto json_str = std::make_shared<std::string>(json.dump());
  auto json_length = std::make_shared<std::uint32_t>(json_str->size());

  std::vector<boost::asio::const_buffer> buffers;
  buffers.push_back(boost::asio::buffer(json_length.get(), sizeof(*json_length)));
  buffers.push_back(boost::asio::buffer(*json_str));

  boost::asio::async_write(*socket, buffers,
    [json_str, json_length, callback](const boost::system::error_code& ec, std::size_t bytes_transferred) {
      if (callback) {
        callback(ec, bytes_transferred);
      }
    });
}

static void receive_json_async(std::shared_ptr<tcp::socket> socket, ReadJSONComplete callback) {
  auto json_length = std::make_shared<std::size_t>(0);

  boost::asio::async_read(*socket, boost::asio::buffer(json_length.get(), sizeof(*json_length)),
    [json_length, socket, callback](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
      if (!ec) {
        auto json_str = std::make_shared<std::vector<char>>(*json_length);
        boost::asio::async_read(*socket, boost::asio::buffer(json_str->data(), json_str->size()),
        [json_str, callback](const boost::system::error_code& ec, std::size_t bytes_transferred) {
          if (!ec) {
            JSON json = JSON::parse(json_str->begin(), json_str->end());
            if (callback) {
              callback(json, ec, bytes_transferred);
            }
          } else {
            if (callback) {
              JSON json;
              callback(json, ec, bytes_transferred);
            }
          }
        });
      } else {
        if (callback) {
          JSON json;
          callback(json, ec, 0);
        }
      }
    });
}

static void receive_packet_async(std::shared_ptr<tcp::socket> socket, int size, ReadAVPacketComplete callback) {
  char* packet_data = (char*) malloc(size * sizeof(char)); // leak
  boost::asio::async_read(*socket, boost::asio::buffer(packet_data, size),
  [packet_data, size, callback](const boost::system::error_code& ec, std::size_t bytes_transferred) {
    static auto av_packet_deleter = [](AVPacket* pkt) {
      av_packet_free(&pkt);
    };
    std::shared_ptr<AVPacket> packet(av_packet_alloc(), av_packet_deleter);
    av_packet_from_data(packet.get(), (uint8_t*)packet_data, size);
    callback(packet, ec, bytes_transferred);
    
    spdlog::debug("read a AVPacket.");
  });
}

static void send_packet_async(std::shared_ptr<tcp::socket> socket, const std::shared_ptr<AVPacket>& pkt, WriteAVPacketComplete callback) {
  auto data = std::make_shared<std::vector<char>>(pkt->data, pkt->data + pkt->size);

  std::vector<boost::asio::const_buffer> buffers;
  buffers.push_back(boost::asio::buffer(*data));

  boost::asio::async_write(*socket, buffers,
  [socket, data, callback](const boost::system::error_code& ec, std::size_t bytes_transferred) {
    if (callback) {
      callback(ec, bytes_transferred);
    }
  });
}

StreamSession::StreamSession(std::shared_ptr<tcp::socket> socket)
: socket_(socket) {
  spdlog::info("StreamSession");
}

StreamSession::~StreamSession() {
  spdlog::info("~StreamSession");
}

StreamPushSession::~StreamPushSession() {}

StreamPullSession::~StreamPullSession() {}

std::shared_ptr<tcp::socket> StreamSession::socket() { return socket_; }

StreamPullSession::StreamPullSession(std::shared_ptr<tcp::socket> socket, const std::string& puller_id)
    : StreamSession(socket)/*, StreamPuller<std::shared_ptr<AVPacket>>(puller_id) */ {}

void StreamPullSession::OnData(const std::shared_ptr<AVPacket>& data) {
  JSON json;
  json["type"] = MessageType::kTypePacket;
  json["packet_size"] = data->size;

  spdlog::info("pull session {} write json: {}", this, json.dump());
  send_json_async(socket_, json, [=](const boost::system::error_code& ec, std::size_t sz) {
    if (ec) { // Check if there is an error
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
//      OnPullComplete();
    } else {
      send_packet_async(socket_, data, [=](const boost::system::error_code& ec, std::size_t) {
        if (ec) { // Check if there is an error
          spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
//          OnPullComplete();
        } else {
          spdlog::info("pull session {} write packet success", this);
        }
      });
    }
  });
}

void StreamPullSession::Start() {}

StreamPushSession::StreamPushSession(std::shared_ptr<tcp::socket> socket, const std::string& stream_id)
: StreamSession(socket)/*, StreamPusher<std::shared_ptr<AVPacket>>(stream_id)*/ {}

void StreamPushSession::Start() { 
  ReadMessage();
}

void StreamPushSession::ReadMessage() {
  receive_json_async(socket_, [=](const JSON& json, const boost::system::error_code& ec, std::size_t) {
    if (ec) { // Check if there is an error
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
      if (listener_) { listener_->OnPushStreamComplete(stream_id_); }
    } else {
      spdlog::debug("read json: {}", json.dump());
      int message_type = json["type"];
      if (message_type == MessageType::kTypePacket) {
        int packet_size = json["packet_size"];
        receive_packet_async(socket_, packet_size, [=](std::shared_ptr<AVPacket> packet, const boost::system::error_code&, std::size_t) {
          // 转发帧
          for (auto&& puller : pullers_) {
            puller->OnData(packet);
          }
          
          ReadMessage();
        });
      } else if (message_type == MessageType::kTypeStreamInfo) {
        bool enable = json["enable"];
        if (!enable) {
          if (listener_) {
            listener_->OnPushStreamComplete(stream_id_);
          }
        }
      }
    }
  });
}

StreamingServer::StreamingServer(boost::asio::io_context& io_context,
                                 short stream_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), stream_port)) {
  StartAccept();
}

StreamingServer::~StreamingServer() {}

void StreamingServer::OnPushStreamComplete(const std::string& stream_id) {
  push_sessions_.erase(stream_id);
}

void StreamingServer::HandleNewConnection(std::shared_ptr<tcp::socket> socket) {
  receive_json_async(socket, [=](const JSON& json, const boost::system::error_code& ec, std::size_t) {
    if (ec) { // Check if there is an error
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
    } else {
//      spdlog::debug("read json: {}", json.dump());
      int message_type = json["type"];
      
      if (message_type != MessageType::kTypeStreamInfo) {
        spdlog::info("type error: {}", message_type);
        return;
      }
      
      bool is_push = json["is_push"];
      bool enable = json["enable"];
      std::string stream_id = json["stream_id"];
      spdlog::info("read json, is_push: {}, stream_id: {}", is_push, stream_id); 
      
      if (is_push) {
        if (enable) {
          std::shared_ptr<StreamPushSession> push_session = std::make_shared<StreamPushSession>(socket, stream_id);
          push_session->RegisterStreamPushLister(this);
          push_session->Start();
          if (push_sessions_.find(stream_id) != push_sessions_.end()) {
            spdlog::error("push stream id: {} is exists.", stream_id);
          }
          push_sessions_[stream_id] = push_session;
        }
      } else {
        std::string puller_id = json["puller_id"];
        if (push_sessions_.find(stream_id) == push_sessions_.end()) {
          spdlog::error("push stream id: {} is not exists.", stream_id);
          // 没推流就先拉流
          return;
        }
        if (enable) {
          std::shared_ptr<StreamPullSession> pull_session = std::make_shared<StreamPullSession>(socket, puller_id);
          pull_session->Start();
          push_sessions_[stream_id]->RegisterPuller(pull_session);
        }
      }
    }
  });
}

void StreamingServer::StartAccept() {
  // 创建一个新的 socket
  std::shared_ptr<tcp::socket> socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
  // 异步等待新的连接
  acceptor_.async_accept(
      *socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
          // 成功接受新的连接
          HandleNewConnection(socket);

          // 再次开始等待接受下一个连接
          StartAccept();
        } else {
          // 处理错误
        }
      });
}

