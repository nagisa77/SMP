
#include "test_client_pull.hh"
#include <QApplication>

static SwsContext* sws_ctx = nullptr;

void VideoPlayerView::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space) {
    spdlog::info("Space key pressed");
    pause_ = !pause_;
  } else {
    QWidget::keyPressEvent(event);
  }
}

static QImage convertToQImage(AVFramePtr frame) {
  if (frame->format != AV_PIX_FMT_YUV420P &&
      frame->format != AV_PIX_FMT_YUVJ420P) {
    return QImage();
  }

  if (!sws_ctx /* || 检查帧格式或大小是否改变 */) {
    if (sws_ctx) {
      sws_freeContext(sws_ctx);
    }
    sws_ctx = sws_getContext(frame->width, frame->height,
                             static_cast<AVPixelFormat>(frame->format),
                             frame->width, frame->height, AV_PIX_FMT_RGB32,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
      return QImage();
    }
  }

  uint8_t* dest[4] = {nullptr};
  int dest_linesize[4] = {0};
  av_image_alloc(dest, dest_linesize, frame->width, frame->height,
                 AV_PIX_FMT_RGB32, 1);

  sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, dest,
            dest_linesize);

  QImage img(dest[0], frame->width, frame->height, dest_linesize[0],
             QImage::Format_RGB32);

  av_freep(&dest[0]);

  return img;
}

VideoPlayerView::VideoPlayerView() : QWidget(nullptr) {
  spdlog::info("VideoPlayerView");

//  connect(this, &VideoPlayerView::frameReady, this,
//          &VideoPlayerView::renderFrame);
}

VideoPlayerView::~VideoPlayerView() {
  spdlog::info("~VideoPlayerView");
}

void VideoPlayerView::OnVideoFrame(AVFramePtr frame) {
  QImage image = convertToQImage(frame);
  renderFrame(image);
//  emit frameReady(image);
}

void VideoPlayerView::OnMediaError() {
  close();
}

void VideoPlayerView::renderFrame(QImage frame) {
  current_frame_ = frame;
  update();
}

void VideoPlayerView::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  if (!current_frame_.isNull()) {
    QSize windowSize = this->size();
    QImage scaledFrame = current_frame_.scaled(windowSize, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
    int startX = (windowSize.width() - scaledFrame.width()) / 2;
    int startY = (windowSize.height() - scaledFrame.height()) / 2;
    painter.drawImage(startX, startY, scaledFrame);
  }
}

using JSON = nlohmann::json;

using boost::asio::ip::tcp;

enum MessageType {
  kTypeStreamInfo = 0,
  kTypePacket = 1,
  kTypeCodecInfo = 2,
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
  spdlog::info("receive json: {}", json.dump());
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

int main(int argc, char** argv) {
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
    
    JSON codec_info;
    receive_json(socket, codec_info);
    
    if (codec_info["type"] != MessageType::kTypeCodecInfo) {
      spdlog::error("does not receive codec info");
      return 1;
    }
    
    AVCodecID codec_id = codec_info["codec_id"];
    
    const AVCodec *codec = avcodec_find_decoder(codec_id);
    if (!codec) {
      spdlog::error("Codec not found");
      return 1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
      spdlog::error("Could not allocate video codec context");
      return 1;
    }
    
    
    int q_argc = argc;
    char** q_argv = (char**)argv;
    QApplication app(q_argc, q_argv);

    VideoPlayerView video_player;
    video_player.show();

    app.exec();
    
    auto frame = createAVFramePtr();

    while (true) {
      std::shared_ptr<AVPacket> paket = receive_packet(socket);
      
      if (avcodec_send_packet(codec_ctx, paket.get()) == 0) {
        int ret = avcodec_receive_frame(codec_ctx, frame.get());

        if (ret == 0) {
          int width = frame->width;
          int height = frame->height;
          if (width <= 0 || height <= 0) {
            continue;
          }
          
          video_player.OnVideoFrame(frame);
        }
      }
    }
    
    
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
    return 1;
  }

  spdlog::info("Client finished successfully.");
  return 0;
}

