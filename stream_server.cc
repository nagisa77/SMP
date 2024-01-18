
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
  kTypeCodecInfo = 2, 
};

struct PacketInfo {
  int64_t pts;
  int64_t dts;
  int stream_index;
  int size;
  int duration;
  int pos;
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

void logAVPacket(const AVPacket* pkt) {
    std::stringstream ss;
    
    ss << "AVPacket: ";
    ss << "pts = " << pkt->pts << ", ";
    ss << "dts = " << pkt->dts << ", ";
    ss << "size = " << pkt->size << ", ";
    ss << "stream_index = " << pkt->stream_index << ", ";
    ss << "flags = " << pkt->flags << ", ";
    ss << "side_data_elems = " << pkt->side_data_elems << ", ";
    ss << "duration = " << pkt->duration << ", ";
    ss << "pos = " << pkt->pos << ", ";

    // 打印 data 字段的前几个字节
    ss << "data (first few bytes) = ";
    const int dataBytesToPrint = 10;
    for (int i = 0; i < std::min(pkt->size, dataBytesToPrint); ++i) {
        ss << std::setfill('0') << std::setw(2) << std::hex << (int)pkt->data[i] << " ";
    }

    // 打印 side_data 字段的前几个字节（如果存在）
    if (pkt->side_data_elems > 0 && pkt->side_data != nullptr) {
        ss << ", side_data (first few bytes) = ";
        const int sideDataBytesToPrint = 10;
        for (int i = 0; i < std::min((int)pkt->side_data->size, sideDataBytesToPrint); ++i) {
            ss << std::setfill('0') << std::setw(2) << std::hex << (int)pkt->side_data->data[i] << " ";
        }
    }

    spdlog::info(ss.str());
}

static void receive_packet_async(std::shared_ptr<tcp::socket> socket, PacketInfo info, ReadAVPacketComplete callback) {
  int size = info.size;
  char* packet_data = (char*) av_malloc(size * sizeof(char)); // leak
  boost::asio::async_read(*socket, boost::asio::buffer(packet_data, size),
  [packet_data, size, callback, info](const boost::system::error_code& ec, std::size_t bytes_transferred) {
    static auto av_packet_deleter = [](AVPacket* pkt) {
      av_packet_free(&pkt);
    };
    std::shared_ptr<AVPacket> packet(av_packet_alloc(), av_packet_deleter);
    av_packet_from_data(packet.get(), (uint8_t*)packet_data, size);
    packet->pts = info.pts;
    packet->dts = info.dts;
    packet->stream_index = info.stream_index;
    packet->duration = info.duration;
    packet->pos = info.pos;
    
    spdlog::info("receive_packet_async");
    logAVPacket(packet.get());

    callback(packet, ec, bytes_transferred);
    spdlog::debug("read a AVPacket.");
  });
}

static void send_packet_async(std::shared_ptr<tcp::socket> socket, const std::shared_ptr<AVPacket>& pkt, WriteAVPacketComplete callback) {
  spdlog::info("send_packet_async");
  logAVPacket(pkt.get());
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

std::shared_ptr<tcp::socket> StreamSession::socket() { return socket_; }

StreamPullSession::StreamPullSession(std::shared_ptr<tcp::socket> socket)
    : StreamSession(socket) {}

bool StreamPullSession::HasReceiveCodecInfo() {
  return has_receive_codec_info_;
}

void StreamPullSession::PopStreamData(std::shared_ptr<StreamData> stream_data) {
  if (stream_data->data_send_stack_.empty()) {
    return;
  }
  StreamDataType type = stream_data->data_send_stack_.back();
  stream_data->data_send_stack_.pop_back();
  if (type == StreamDataType::kStreamDataTypePacket) {
    std::shared_ptr<AVPacket> data = stream_data->packet_;
    JSON json;
    json["type"] = MessageType::kTypePacket;
    json["packet_size"] = data->size;
    json["pts"] = data->pts;
    json["dts"] = data->dts;
    json["stream_index"] = data->stream_index;
    json["duration"] = data->duration;
    json["pos"] = data->pos;
    
    spdlog::info("pull session write json: {}", json.dump());
    send_json_async(socket_, json, [=](const boost::system::error_code& ec, std::size_t sz) {
      if (ec) {
        spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
        // remove self from pusher
        pusher_->UnregisterPuller(shared_from_this());
      } else {
        send_packet_async(socket_, data, [=](const boost::system::error_code& ec, std::size_t) {
          if (ec) {
            spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
            pusher_->UnregisterPuller(shared_from_this());
          } else {
            spdlog::info("pull session write packet success");
          }
          PopStreamData(stream_data);
        });
      }
    });
  } else if (type == StreamDataType::kStreamDataTypeCodecInfo) {
    has_receive_codec_info_ = true;
    
    std::shared_ptr<CodecInfo> info = stream_data->codec_info_;
    JSON json;

    json["type"] = MessageType::kTypeCodecInfo;
    json["codec_id"] = stream_data->codec_info_->codec_id_;
    json["width"] = stream_data->codec_info_->width;
    json["height"] = stream_data->codec_info_->height;
    json["pix_fmt"] = stream_data->codec_info_->pix_fmt;
    json["extradata_size"] = stream_data->codec_info_->extradata_size;
    json["extradata"] = stream_data->codec_info_->extradata_base64;
     
    send_json_async(socket_, json, [=](const boost::system::error_code& ec, std::size_t sz) {
      if (ec) {
        spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
        // remove self from pusher
        pusher_->UnregisterPuller(shared_from_this());
      } else {
        PopStreamData(stream_data);
      }
    });
  }
}

void StreamPullSession::OnData(const StreamData& stream_data) {
  std::shared_ptr<StreamData> data_to_pop = std::make_shared<StreamData>(stream_data);
  PopStreamData(data_to_pop);
}

void StreamPullSession::Start() {}

StreamPushSession::StreamPushSession(std::shared_ptr<tcp::socket> socket, const std::string& stream_id)
: StreamSession(socket), stream_id_(stream_id) {}

void StreamPushSession::Start() { 
  ReadMessage();
}

void StreamPushSession::NotifyPuller(const StreamData& data) {
  StreamData data_to_notify = data;
  for (auto puller : pullers_) {
    auto pull_session = std::dynamic_pointer_cast<StreamPullSession>(puller);
//    if (!codec_info_) {
//      listener_->OnPushStreamComplete(stream_id_);
//      return;
//    }
    
    if (pull_session && !pull_session->HasReceiveCodecInfo()) {
      data_to_notify.data_send_stack_.push_back(StreamDataType::kStreamDataTypeCodecInfo);
      data_to_notify.codec_info_ = codec_info_;
    }
  }
  StreamPusher<StreamData>::NotifyPuller(data_to_notify);
}

void StreamPushSession::ReadMessage() {
  receive_json_async(socket_, [=](const JSON& json, const boost::system::error_code& ec, std::size_t) {
    if (ec) {
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
      if (listener_) { listener_->OnPushStreamComplete(stream_id_); }
    } else {
      spdlog::debug("read json: {}", json.dump());
      int message_type = json["type"];
      if (message_type == MessageType::kTypePacket) {
        PacketInfo info;
        info.size = json["packet_size"];
        info.pts = json["pts"];
        info.dts = json["dts"];
        info.stream_index = json["stream_index"];
        info.duration = json["duration"];
        info.pos = json["pos"];
        receive_packet_async(socket_, info, [=](std::shared_ptr<AVPacket> packet, const boost::system::error_code&, std::size_t) {
          StreamData stream_data;
          stream_data.data_send_stack_ = { StreamDataType::kStreamDataTypePacket };
          stream_data.packet_ = packet;
          // 转发帧
          NotifyPuller(stream_data);
          spdlog::info("notify pullers, num of puller: {}", pullers_.size());
          ReadMessage();
        });
      } else if (message_type == MessageType::kTypeStreamInfo) {
        bool enable = json["enable"];
        if (!enable) {
          if (listener_) {
            listener_->OnPushStreamComplete(stream_id_);
          }
        }
      } else if (message_type == MessageType::kTypeCodecInfo) {
        if (!codec_info_) {
          codec_info_ = std::make_shared<CodecInfo>();
        }
        codec_info_->codec_id_ = json["codec_id"];
        codec_info_->width = json["width"];
        codec_info_->height = json["height"];
        codec_info_->pix_fmt = json["pix_fmt"];
        codec_info_->extradata_size = json["extradata_size"];
        codec_info_->extradata_base64 = json["extradata"];

        // notify puller codec info update
        ReadMessage();
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
    if (ec) {
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
    } else {
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
        if (push_sessions_.find(stream_id) == push_sessions_.end()) {
          spdlog::error("push stream id: {} is not exists.", stream_id);
          // 没推流就先拉流
          return;
        }
        if (enable) {
          std::shared_ptr<StreamPullSession> pull_session = std::make_shared<StreamPullSession>(socket);
          pull_session->Start();
          push_sessions_[stream_id]->RegisterPuller(pull_session);
          pull_session->pusher_ = push_sessions_[stream_id];
        }
      }
    }
  });
}

void StreamingServer::StartAccept() {
  std::shared_ptr<tcp::socket> socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
  acceptor_.async_accept(
      *socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
          HandleNewConnection(socket);

          StartAccept();
        } else {
        }
      });
}

