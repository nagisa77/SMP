
#ifndef STREAM_SERVER_HH
#define STREAM_SERVER_HH

#include "include/httplib.h"
#include <boost/asio.hpp>
#include <memory>

using boost::asio::ip::tcp;

class StreamSession : public std::enable_shared_from_this<StreamSession> {
public:
  explicit StreamSession(boost::asio::io_context& io_context);
  tcp::socket& socket();
  void Start();

private:
  tcp::socket socket_;
  // 其他成员变量和方法
};

class StreamingServer {
public:
  StreamingServer(boost::asio::io_context& io_context, short stream_port,
                  short http_port);
  void StartAccept();
  void SetupRoutes();

private:
  void HandleAccept(std::shared_ptr<StreamSession> session,
                    const boost::system::error_code& error);

  boost::asio::io_context& io_context_;
  tcp::acceptor acceptor_;
  httplib::Server http_server_;
};

#endif // STREAM_SERVER_HH
