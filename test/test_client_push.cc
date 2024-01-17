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

struct AVFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};

using AVFramePtr = std::shared_ptr<AVFrame>;

inline AVFramePtr createAVFramePtr() {
    return AVFramePtr(av_frame_alloc(), AVFrameDeleter());
}

using JSON = nlohmann::json;

using boost::asio::ip::tcp;

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

enum MessageType {
  kTypeStreamInfo = 0,
  kTypePacket = 1,
};

static void send_packet(tcp::socket& socket, const AVPacket* pkt) {
  auto packet_size = std::make_shared<std::uint32_t>(pkt->size);
  
  JSON json;
  json["packet_size"] = *packet_size;
  json["type"] = MessageType::kTypePacket;
  
  send_json(socket, json);
  
  auto packet_data = std::make_shared<std::vector<char>>(pkt->data, pkt->data + pkt->size);

  std::vector<boost::asio::const_buffer> buffers;
  buffers.push_back(boost::asio::buffer(*packet_data));

  boost::asio::write(socket, buffers);
}

int main() {
  spdlog::info("Starting Pusher client...");

  try {
    boost::asio::io_context io_context;
    tcp::socket socket(io_context);
    spdlog::info("Connecting to TCP server...");
    socket.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                      10086)); // 替换为您的 TCP 端口
    JSON start_push;
    start_push["type"] = MessageType::kTypeStreamInfo;
    start_push["is_push"] = true;
    start_push["stream_id"] = "sub-video-tim";
    start_push["enable"] = true;
    send_json(socket, start_push);
    
    spdlog::info("start Codec");

    std::string file_path = "/Users/jt/Downloads/output.mp4";
    
    const char* video_path = file_path.c_str();
    AVFormatContext* pFormatCtx = NULL;
    if (avformat_open_input(&pFormatCtx, video_path, NULL, NULL) != 0) {
      printf("avformat_open_input error\n");
      return 1;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
      printf("avformat_find_stream_info error\n");
      return 1;
    }

    int video_stream_index = -1;
    //  int audio_stream_index = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
      if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
      }
    }
    
    AVRational stream_time_base = pFormatCtx->streams[video_stream_index]->time_base;

    
    if (video_stream_index == -1) {
      spdlog::error("no video found");
      return 1;
    }

    const AVCodec* codec = avcodec_find_decoder(
        pFormatCtx->streams[video_stream_index]->codecpar->codec_id);
    AVCodecContext* pCodecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(
        pCodecCtx, pFormatCtx->streams[video_stream_index]->codecpar);

    if (avcodec_open2(pCodecCtx, codec, NULL) < 0) {
      fprintf(stderr, "Could not open codec\n");
      return 1;
    }
    AVPacket pkt;
    auto frame = createAVFramePtr();
    uint64_t idx = 0;

    struct timeval start, end;
    gettimeofday(&start, NULL);

    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
      if (pkt.stream_index == video_stream_index) {
        if (avcodec_send_packet(pCodecCtx, &pkt) == 0) {
          int ret = avcodec_receive_frame(pCodecCtx, frame.get());
          if (ret == 0) {
            double frame_time = av_q2d(stream_time_base) * frame->pts;
            
            static bool is_first_frame_ = true;
            static int64_t first_frame_time_us_ = 0;
            
            int64_t frame_time_us = static_cast<int64_t>(frame_time * 1000000.0);

            if (is_first_frame_) {
              first_frame_time_us_ = av_gettime();
              is_first_frame_ = false;
            }

            int64_t now_us = av_gettime();
            frame_time_us += first_frame_time_us_;

            if (now_us < frame_time_us) {
              int64_t wait_time_us = frame_time_us - now_us;
              std::this_thread::sleep_for(std::chrono::microseconds(wait_time_us));
            }
          }
        }
        send_packet(socket, &pkt);
      }
      av_packet_unref(&pkt);
    }

    // free
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);
    
    JSON stop_push;
    stop_push["type"] = MessageType::kTypeStreamInfo;
    stop_push["is_push"] = true;
    stop_push["stream_id"] = "sub-video-tim";
    stop_push["enable"] = false;
    send_json(socket, stop_push);
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
    return 1;
  }

  spdlog::info("Client finished successfully.");
  return 0;
}
