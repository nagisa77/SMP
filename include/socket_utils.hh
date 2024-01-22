#ifndef SOCKET_UTILS_HH
#define SOCKET_UTILS_HH

#include <boost/asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

using boost::asio::ip::tcp;
using JSON = nlohmann::json;

using WriteJSONComplete = std::function<void(const boost::system::error_code&, std::size_t)>;
using ReadJSONComplete = std::function<void(const JSON& json, const boost::system::error_code&, std::size_t)>;
using ReadAVPacketComplete = std::function<void(std::shared_ptr<AVPacket> packet, const boost::system::error_code&, std::size_t)>;
using WriteAVPacketComplete = std::function<void(const boost::system::error_code&, std::size_t)>;

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

    spdlog::debug(ss.str());
}

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
    
    spdlog::debug("receive_packet_async");
    logAVPacket(packet.get());

    callback(packet, ec, bytes_transferred);
    spdlog::debug("read a AVPacket.");
  });
}

static void send_packet_async(std::shared_ptr<tcp::socket> socket, const std::shared_ptr<AVPacket>& pkt, WriteAVPacketComplete callback) {
  spdlog::debug("send_packet_async");
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



#endif
