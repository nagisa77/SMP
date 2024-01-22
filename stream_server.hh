
#ifndef STREAM_SERVER_HH
#define STREAM_SERVER_HH

#include "include/httplib.h"
#include <boost/asio.hpp>
#include <memory>
#include <stream_interface.hh>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

using boost::asio::ip::tcp;

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

struct CodecInfo {
  AVCodecID codec_id_;
  int width;
  int height;
  int pix_fmt;
  int extradata_size;
  std::string extradata_base64; 
};

enum PullerDataType {
  kPullerDataTypeCodecInfo = 0,
  kPullerDataTypePacket = 1,
};

struct PullerData {
  std::vector<PullerDataType> data_send_stack_; 
  std::shared_ptr<CodecInfo> codec_info_;
  std::shared_ptr<AVPacket> packet_;
};

class StreamSession {
public:
  explicit StreamSession(std::shared_ptr<tcp::socket> socket);
  ~StreamSession(); 
  std::shared_ptr<tcp::socket> socket();
  virtual void Start() = 0;

protected:
  std::shared_ptr<tcp::socket> socket_;
};

class StreamPushSession : 
public StreamSession,
public StreamPusher<PullerData> {
public:
  explicit StreamPushSession(std::shared_ptr<tcp::socket> socket, const std::string& stream_id);

  void Start() override;
  void NotifyPuller(const PullerData& data) override;

private:
  void ReadMessage();
  int data_ = 0;
  std::string stream_id_;
  std::shared_ptr<CodecInfo> codec_info_;
};

class StreamPullSession : 
public StreamSession,
public StreamPuller<PullerData> {
public:
  explicit StreamPullSession(std::shared_ptr<tcp::socket> socket);
  void OnData(const PullerData& data) override;
  void Start() override;
  bool HasReceiveCodecInfo();
  
private:
  void PopStreamData(std::shared_ptr<PullerData> stream_data);
  bool has_receive_codec_info_ = false;
};

class StreamingServer : public StreamPushListener {
public:
  StreamingServer(boost::asio::io_context& io_context, short stream_port);
  ~StreamingServer();
  void StartAccept();
  void OnPushStreamComplete(const std::string& stream_id) override;
  
private:
  void HandleNewConnection(std::shared_ptr<tcp::socket> socket);

  boost::asio::io_context& io_context_;
  tcp::acceptor acceptor_;
  std::map<std::string, std::shared_ptr<StreamPushSession>> push_sessions_;
};

#endif // STREAM_SERVER_HH
