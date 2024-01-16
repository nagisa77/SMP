
#include "stream_server.hh"
#include "stream_interface.hh"
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using JSON = nlohmann::json;

using WriteJSONComplete = std::function<void(const boost::system::error_code&, std::size_t)>;
using ReadJSONComplete = std::function<void(const JSON& json, const boost::system::error_code&, std::size_t)>;

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

StreamSession::StreamSession(std::shared_ptr<tcp::socket> socket)
: socket_(socket) {
}

StreamPushSession::~StreamPushSession() {}

StreamPullSession::~StreamPullSession() {}

std::shared_ptr<tcp::socket> StreamSession::socket() { return socket_; }

StreamPullSession::StreamPullSession(std::shared_ptr<tcp::socket> socket)
    : StreamSession(socket) {}

void StreamPullSession::OnData(const int& data) {}

void StreamPullSession::Start() {}

StreamPushSession::StreamPushSession(std::shared_ptr<tcp::socket> socket)
    : StreamSession(socket) {}

void StreamPushSession::Start() { 
  receive_json_async(socket_, [](const JSON& json, const boost::system::error_code& ec, std::size_t) {
    if (ec) { // Check if there is an error
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
    } else {
      spdlog::info("read json: \n{}", json.dump(2));
    }
  });
}

StreamingServer::StreamingServer(boost::asio::io_context& io_context,
                                 short stream_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), stream_port)) {
  StartAccept();
}

StreamingServer::~StreamingServer() {}

void StreamingServer::HandleNewConnection(std::shared_ptr<tcp::socket> socket) {
  receive_json_async(socket, [](const JSON& json, const boost::system::error_code& ec, std::size_t) {
    if (ec) { // Check if there is an error
      spdlog::error("Error: {} - {}", ec.value(), ec.message()); // Log the error information
    } else {
      // spdlog::debug("read json: \n{}", json.dump(2));
      bool is_push = json["is_push"];
      std::string stream_id = json["stream_id"];
      spdlog::info("read json, is_push: {}, stream_id: {}", is_push, stream_id); 
      
      if (is_push) {
        std::shared_ptr<StreamPushSession> push_session = std::make_shared<StreamPushSession>(socket);
        push_session->Start();
        if (push_sessions_.find(stream_id) != push_sessions_.end()) {
          spdlog::error("push stream id: {} is exists.", stream_id);
        }
        push_sessions_[stream_id] = push_session;
      } else {
        std::shared_ptr<StreamPullSession> pull_session = std::make_shared<StreamPullSession>(socket);
        pull_session->Start();
        
        if (push_sessions_.find(stream_id) == push_sessions_.end()) {
          spdlog::error("push stream id: {} is not exists.", stream_id);
          
          // 没推流就先拉流
          return;
        }
        push_sessions_[stream_id]->RegisterPuller(pull_session); 
      }
    }
  });
//  
//  std::shared_ptr<StreamPushSession> push_session = std::make_shared<StreamPushSession>(socket);
//  push_session->Start();
//  push_sessions_["stream_id_01"] = push_session;
}

void StreamingServer::StartAccept() {
  // 创建一个新的 socket
  std::shared_ptr<tcp::socket> socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
  // 异步等待新的连接
  acceptor_.async_accept(
      *socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
          // 成功接受新的连接
          HandleNewConnection(socket);

          // 再次开始等待接受下一个连接
          StartAccept();
        } else {
          // 处理错误
        }
      });

  //  acceptor_.async_accept(
  //      push_session->socket(),
  //      [this, push_session](const boost::system::error_code& error) {
  //        HandleAcceptPush(push_session, error);
  //      });
  //  // 处理推流信令
  //  http_server_.Post(
  //      "/push", [&](const httplib::Request& req, httplib::Response& res) {
  //        // 解析请求
  //        auto stream_id = req.get_param_value("stream_id");
  //        auto enable = req.get_param_value("enable");
  //
  //        spdlog::info("push, stream_id: {}, enable: {}", stream_id, enable);
  //        // 根据请求处理推流逻辑
  //        // ...
  //
  //        res.set_content("Push signal processed", "text/plain");
  //
  //        StartAccept(stream_id, true); // true 表示这是一个推流会话
  //      });
  //
  //  // 处理拉流信令
  //  http_server_.Post(
  //      "/pull", [&](const httplib::Request& req, httplib::Response& res) {
  //        // 解析请求
  //        auto stream_id = req.get_param_value("stream_id");
  //        auto enable = req.get_param_value("enable");
  //
  //        spdlog::info("push, stream_id: {}, enable: {}", stream_id, enable);
  //        // 根据请求处理拉流逻辑
  //        // ...
  //
  //        res.set_content("Pull signal processed", "text/plain");
  //
  //        StartAccept(stream_id, false); // false 表示这是一个拉流会话
  //      });
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

//void StreamingServer::StartAccept(const std::string& stream_id, bool is_push) {
//  if (is_push) {
//    std::shared_ptr<StreamPushSession> push_session =
//        std::make_shared<StreamPushSession>(io_context_);
//
//    if (push_sessions_.find(stream_id) != push_sessions_.end()) {
//      spdlog::error("push sessions stream_id exists: {}", stream_id);
//    }
//
//    push_sessions_[stream_id] = push_session;
//
//    acceptor_.async_accept(
//        push_session->socket(),
//        [this, push_session](const boost::system::error_code& error) {
//          HandleAcceptPush(push_session, error);
//        });
//  } else {
//    std::shared_ptr<StreamPullSession> pull_session =
//        std::make_shared<StreamPullSession>(io_context_);
//    if (push_sessions_.find(stream_id) == push_sessions_.end()) {
//      spdlog::error("push sessions stream_id not exists: {}", stream_id);
//      return;
//    }
//    push_sessions_[stream_id]->RegisterPuller(pull_session);
//
//    acceptor_.async_accept(
//        pull_session->socket(),
//        [this, pull_session](const boost::system::error_code& error) {
//          HandleAcceptPull(pull_session, error);
//        });
//  }
//}

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
