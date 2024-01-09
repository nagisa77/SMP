
#ifndef STREAM_SERVER_HH
#define STREAM_SERVER_HH

#include "include/httplib.h"
#include "stream_interface.hh"
#include <boost/asio.hpp>
#include <memory>

using boost::asio::ip::tcp;

class StreamSession : public std::enable_shared_from_this<StreamSession> {
public:
  explicit StreamSession(boost::asio::io_context& io_context);
  tcp::socket& socket();
  virtual void Start() = 0;

protected:
  tcp::socket socket_;
  // 其他成员变量和方法
};

class StreamPushSession : public StreamSession, public StreamPusher<int> {
public:
  explicit StreamPushSession(boost::asio::io_context& io_context);
  void Start() override;
  void DoRead();

private:
  int data_ = 0;
};

class StreamPullSession : public StreamSession, public StreamPuller<int> {
public:
  explicit StreamPullSession(boost::asio::io_context& io_context);
  void OnData(const int& data) override;
  void Start() override;
};

class StreamingServer {
public:
  StreamingServer(boost::asio::io_context& io_context, short stream_port,
                  short http_port);
  void StartAccept(const std::string& stream_id, bool is_push);
  void SetupRoutes();

private:
  void HandleAcceptPush(std::shared_ptr<StreamPushSession> session,
                        const boost::system::error_code& error);
  void HandleAcceptPull(std::shared_ptr<StreamPullSession> session,
                        const boost::system::error_code& error);
  void StartStreamingSession(const std::string& stream_id, bool is_push);
  void StopStreamingSession(const std::string& stream_id);

  boost::asio::io_context& io_context_;
  tcp::acceptor acceptor_;
  httplib::Server http_server_;
  std::map<std::string, std::shared_ptr<StreamPushSession>> push_sessions_;
};

#endif // STREAM_SERVER_HH
