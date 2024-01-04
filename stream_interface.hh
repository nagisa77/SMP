
#ifndef STREAM_INTERFACE
#define STREAM_INTERFACE
#include <set>
#include <string>

template <typename T> class StreamPuller {
public:
  virtual void OnData(const T& data) = 0;
};

template <typename T> class StreamPusher {
public:
  void RegisterPuller(StreamPuller<T>* puller) {
    pullers_.insert(puller);
  }
  void UnregisterPuller(StreamPuller<T>* puller) {
    pullers_.erase(puller);
  }
  void NotifyPuller(const T& data) {
    for (auto puller : pullers_) {
      puller->OnData(data);
    }
  }
  std::set<StreamPuller<T>*> pullers_;
};

#endif
