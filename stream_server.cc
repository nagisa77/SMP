
#include "stream_server.hh"
#include "stream_interface.hh"
#include <iostream>
#include <spdlog/spdlog.h>

StreamSession::StreamSession(boost::asio::io_context& io_context)
    : socket_(io_context) {
  // 构造函数实现
}

tcp::socket& StreamSession::socket() { return socket_; }

StreamPullSession::StreamPullSession(boost::asio::io_context& io_context)
    : StreamSession(io_context) {}

void StreamPullSession::OnData(const int& data) {}

void StreamPullSession::Start() {}

StreamPushSession::StreamPushSession(boost::asio::io_context& io_context)
    : StreamSession(io_context) {}

void StreamPushSession::Start() { DoRead(); }

void StreamPushSession::DoRead() {
  auto self(shared_from_this());
  socket_.async_read_some(
      boost::asio::buffer(&data_, sizeof(data_)),
      [this, self](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
          spdlog::info("read: {}", data_);
          // 继续读取更多数据
          DoRead();
        } else {
          // 处理错误或连接关闭
          spdlog::error("Read error: {}", ec.message());
          // 可以在这里关闭套接字或清理资源
        }
      });
}

StreamingServer::StreamingServer(boost::asio::io_context& io_context,
                                 short stream_port, short http_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), stream_port)) {
  SetupRoutes();
  // 运行 HTTP 服务器在单独的线程
  std::thread([this, http_port]() {
    bool listen_ret = http_server_.listen("127.0.0.1", http_port);
    spdlog::info("HTTP server listening on localhost:{}, ret:{}", http_port,
                 listen_ret);
  }).detach();
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

        StartAccept(stream_id, true); // true 表示这是一个推流会话
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

        StartAccept(stream_id, false); // false 表示这是一个拉流会话
      });
}

void StreamingServer::StartStreamingSession(const std::string& stream_id,
                                            bool is_push) {
  // 根据 stream_id 创建或激活相应的流会话
  // ...
}

void StreamingServer::StopStreamingSession(const std::string& stream_id) {
  // 根据 stream_id 停止并清理相应的流会话
  // ...
}

void StreamingServer::StartAccept(const std::string& stream_id, bool is_push) {
  if (is_push) {
    std::shared_ptr<StreamPushSession> push_session =
        std::make_shared<StreamPushSession>(io_context_);

    if (push_sessions_.find(stream_id) != push_sessions_.end()) {
      spdlog::error("push sessions stream_id exists: {}", stream_id);
    }

    push_sessions_[stream_id] = push_session;

    acceptor_.async_accept(
        push_session->socket(),
        [this, push_session](const boost::system::error_code& error) {
          HandleAcceptPush(push_session, error);
        });
  } else {
    std::shared_ptr<StreamPullSession> pull_session =
        std::make_shared<StreamPullSession>(io_context_);
    if (push_sessions_.find(stream_id) == push_sessions_.end()) {
      spdlog::error("push sessions stream_id not exists: {}", stream_id);
      return;
    }
    push_sessions_[stream_id]->RegisterPuller(pull_session);

    acceptor_.async_accept(
        pull_session->socket(),
        [this, pull_session](const boost::system::error_code& error) {
          HandleAcceptPull(pull_session, error);
        });
  }
}

void StreamingServer::HandleAcceptPush(
    std::shared_ptr<StreamPushSession> session,
    const boost::system::error_code& error) {
  session->Start();

  // 在这里，您可能需要做一些特定于推流会话的设置或初始化

  // 这里可能需要一个机制来判断是否应该继续接受新的连接
  // 例如，您可以在这里添加一个条件判断，只有在特定情况下才调用 StartAccept
  // 这可以基于当前活动会话的数量、服务器负载、或者其他逻辑
  // StartAccept();
}
void StreamingServer::HandleAcceptPull(
    std::shared_ptr<StreamPullSession> session,
    const boost::system::error_code& error) {
  session->Start();

  // 在这里，您可能需要做一些特定于拉流会话的设置或初始化

  // 这里可能需要一个机制来判断是否应该继续接受新的连接
  // 例如，您可以在这里添加一个条件判断，只有在特定情况下才调用 StartAccept
  // 这可以基于当前活动会话的数量、服务器负载、或者其他逻辑
  // StartAccept();
}
