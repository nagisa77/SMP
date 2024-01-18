#ifndef BLOCKING_QUEUE_
#define BLOCKING_QUEUE_

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>
#include <optional>

template <typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(size_t max_length) : max_length_(max_length), is_locked_(false) {}

  void replace(std::queue<T> queue) {
    boost::mutex::scoped_lock lock(mutex_);
    queue_ = std::move(queue);
    condition_.notify_one();
  }

  void clear() {
    boost::mutex::scoped_lock lock(mutex_);
    std::queue<T> empty;
    std::swap(queue_, empty);
    condition_.notify_one();
  }

  void push(const T& value) {
    boost::mutex::scoped_lock lock(mutex_);
    while (queue_.size() >= max_length_ || is_locked_) {
      condition_full_.wait(lock);
    }
    queue_.push(value);
    condition_.notify_one();
  }

  std::optional<T> popOrEmpty() {
    boost::mutex::scoped_lock lock(mutex_);
    if (queue_.empty() || is_locked_) {
      return {};
    }

    T value = std::move(queue_.front());
    queue_.pop();
    if (queue_.size() < max_length_) {
      condition_full_.notify_one();
    }
    return value;
  }

  T pop() {
    boost::mutex::scoped_lock lock(mutex_);
    while (queue_.empty() || is_locked_) {
      condition_.wait(lock);
    }
    T value = std::move(queue_.front());
    queue_.pop();
    if (queue_.size() < max_length_) {
      condition_full_.notify_one();
    }
    return value;
  }

  bool empty() const {
    boost::mutex::scoped_lock lock(mutex_);
    return queue_.empty();
  }

  size_t size() const {
    boost::mutex::scoped_lock lock(mutex_);
    return queue_.size();
  }

  void lock() {
    boost::mutex::scoped_lock lock(mutex_);
    is_locked_ = true;
  }

  void unlock() {
    boost::mutex::scoped_lock lock(mutex_);
    is_locked_ = false;
    condition_.notify_all();
    condition_full_.notify_all();
  }

 private:
  mutable boost::mutex mutex_;
  boost::condition_variable condition_;
  boost::condition_variable condition_full_;
  std::queue<T> queue_;
  size_t max_length_;
  bool is_locked_;
};

#endif
