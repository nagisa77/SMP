
#ifndef STREAM_SERVER_HH
#define STREAM_SERVER_HH

#include "include/httplib.h"
#include "stream_interface.hh"
#include <boost/asio.hpp>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

using boost::asio::ip::tcp;

class StreamSession : public std::enable_shared_from_this<StreamSession> {
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
public StreamPusher<std::shared_ptr<AVPacket>> {
public:
  explicit StreamPushSession(std::shared_ptr<tcp::socket> socket, const std::string& stream_id);

  void Start() override;

private:
  void ReadMessage();
  int data_ = 0;
  std::string stream_id_;
};

class StreamPullSession : 
public StreamSession,
public StreamPuller<std::shared_ptr<AVPacket>> {
public:
  explicit StreamPullSession(std::shared_ptr<tcp::socket> socket);
  void OnData(const std::shared_ptr<AVPacket>& data) override;
  void Start() override;
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
