
#include "stream_server.hh"
#include "stream_interface.hh"
#include <iostream>
#include <spdlog/spdlog.h>

StreamSession::StreamSession(boost::asio::io_context& io_context)
    : socket_(io_context) {
  // 构造函数实现
}

tcp::socket& StreamSession::socket() { return socket_; }

void StreamSession::Start() {
  // 开始处理数据流
  // 实现数据接收和发送逻辑
}

StreamingServer::StreamingServer(boost::asio::io_context& io_context,
                                 short stream_port, short http_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), stream_port)) {

  http_server_.listen("0.0.0.0", http_port);
  SetupRoutes();
  StartAccept();
}

void StreamingServer::SetupRoutes() {
  // 处理推流信令
  http_server_.Post(
      "/push", [&](const httplib::Request& req, httplib::Response& res) {
        // 解析请求
        auto stream_id = req.get_param_value("stream_id");
        auto enable = req.get_param_value("enable");

        spdlog::info("push, stream_id: {}, enable: {}", stream_id, enable);
        // 根据请求处理推流逻辑
        // ...

        res.set_content("Push signal processed", "text/plain");
      });

  // 处理拉流信令
  http_server_.Post(
      "/pull", [&](const httplib::Request& req, httplib::Response& res) {
        // 解析请求
        auto stream_id = req.get_param_value("stream_id");
        auto enable = req.get_param_value("enable");

        spdlog::info("push, stream_id: {}, enable: {}", stream_id, enable);
        // 根据请求处理拉流逻辑
        // ...

        res.set_content("Pull signal processed", "text/plain");
      });
}

void StreamingServer::StartAccept() {
  auto session = std::make_shared<StreamSession>(io_context_);
  acceptor_.async_accept(
      session->socket(),
      [this, session](const boost::system::error_code& error) {
        HandleAccept(session, error);
      });
}

void StreamingServer::HandleAccept(std::shared_ptr<StreamSession> session,
                                   const boost::system::error_code& error) {
  if (!error) {
    session->Start();
  }
  StartAccept();
}
