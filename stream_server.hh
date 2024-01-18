
#ifndef STREAM_SERVER_HH
#define STREAM_SERVER_HH

#include "include/httplib.h"
#include "stream_interface.hh"
#include <boost/asio.hpp>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

using boost::asio::ip::tcp;

struct CodecInfo {
  AVCodecID codec_id_;
};

enum StreamDataType {
  kStreamDataTypeCodecInfo = 0,
  kStreamDataTypePacket = 1,
};

struct StreamData {
  std::vector<StreamDataType> data_send_stack_; 
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
public StreamPusher<StreamData> {
public:
  explicit StreamPushSession(std::shared_ptr<tcp::socket> socket, const std::string& stream_id);

  void Start() override;
  void NotifyPuller(const StreamData& data) override;

private:
  void ReadMessage();
  int data_ = 0;
  std::string stream_id_;
  std::shared_ptr<CodecInfo> codec_info_;
};

class StreamPullSession : 
public StreamSession,
public StreamPuller<StreamData> {
public:
  explicit StreamPullSession(std::shared_ptr<tcp::socket> socket);
  void OnData(const StreamData& data) override;
  void Start() override;
  bool HasReceiveCodecInfo();
  
private:
  void PopStreamData(StreamData& stream_data);
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
