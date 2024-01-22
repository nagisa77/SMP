
#ifndef STREAM_INTERFACE
#define STREAM_INTERFACE
#include <memory>
#include <set>
#include <string>

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

template <typename T> class StreamPusher;
class StreamPushListener {
public:
  virtual void OnPushStreamComplete(const std::string& stream_id) = 0;
};

template <typename T> class StreamPuller : public std::enable_shared_from_this<StreamPuller<T>> {
public:
  virtual void OnData(const T& data) = 0;
  std::shared_ptr<StreamPusher<T>> pusher_;
};

template <typename T> class StreamPusher : public std::enable_shared_from_this<StreamPusher<T>> {
public:
  void RegisterPuller(std::shared_ptr<StreamPuller<T>> puller) {
    pullers_.insert(puller);
  }
  void UnregisterPuller(std::shared_ptr<StreamPuller<T>> puller) {
    pullers_.erase(puller);
  }
  virtual void NotifyPuller(const T& data) {
    for (auto puller : pullers_) {
      puller->OnData(data);
    }
  }
  void RegisterStreamPushLister(StreamPushListener* listener) {
    listener_ = listener;
  }

  StreamPushListener* listener_ = nullptr;
  std::set<std::shared_ptr<StreamPuller<T>>> pullers_;
};

#endif
