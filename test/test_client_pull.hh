#ifndef VIDEO_VIEW_HH
#define VIDEO_VIEW_HH

#include "blocking_queue.h"
#include <boost/asio.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}
#include <QWidget>
#include <QPainter>
#include <QKeyEvent>
#include <QImage>

struct AVFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};

using AVFramePtr = std::shared_ptr<AVFrame>;

inline AVFramePtr createAVFramePtr() {
    return AVFramePtr(av_frame_alloc(), AVFrameDeleter());
}

class VideoCodecListener {
 public:
  virtual void OnVideoFrame(AVFramePtr frame) = 0;
  virtual void OnMediaError() = 0;
};


class VideoPlayerView : public QWidget, public VideoCodecListener {
  using super_class = QWidget;
  using this_class = VideoPlayerView;

  Q_OBJECT

 public:
  VideoPlayerView();
  virtual ~VideoPlayerView();

  void renderFrame(QImage frame);
  void paintEvent(QPaintEvent* event) override;

  void OnVideoFrame(AVFramePtr frame) override;
  
 signals:
  void frameReady(QImage frame);
  
 private:
  void OnMediaError() override;
  void keyPressEvent(QKeyEvent *event) override;

 private:
  QImage current_frame_;
  bool pause_ = false;
  BlockingQueue<AVFramePtr> fq_;
};

#endif
