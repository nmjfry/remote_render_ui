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

  virtual void captureFrame();

protected:
  void stopEncodeThread();

private:
  k4a_device_t device = NULL;
  const int32_t CAPTURE_TIMEOUT_IN_MS = 1000;
  size_t width = 0;
  size_t height = 0;
  size_t stride = 0;
  std::atomic<bool> runEncoderThread;
  std::unique_ptr<LibAvWriter> videoStream;
};
