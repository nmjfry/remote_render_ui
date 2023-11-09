#pragma once

#include <PacketComms.h>
#include <VideoLib.h>
#include <boost/log/trivial.hpp>
#include <cstddef>
#include <k4a/k4a.h>

/// A camera handler for k4.
class VideoCapture {
public:
  VideoCapture(PacketMuxer &tx);
  virtual ~VideoCapture();

  virtual void configure();

  virtual void initialiseVideoStream();

  virtual bool sendPreviewImage(k4a_image_t &k4_image);

  virtual k4a_image_t captureFrame(const int32_t TIMEOUT_IN_MS);

protected:
  void startEncodeThread();

  void stopEncodeThread();

  /// Encode a video frame to place into the buffer.
  void encodeVideoFrame();

private:
  k4a_device_t device = NULL;
  const int32_t CAPTURE_TIMEOUT_IN_MS = 1000;
  size_t width = 0;
  size_t height = 0;
  std::atomic<bool> runEncoderThread;
  std::unique_ptr<std::thread> videoEncodeThread;
  std::unique_ptr<LibAvWriter> videoStream;
};
