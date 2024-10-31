// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include "VideoClient.hpp"

/// Window that receives an encoded video stream and displays
/// it in a nanogui::ImageView that allows panning and zooming
/// of the image. Video is decoded in a separate thread to keep
/// the UI widgets responsive (although their effect will be
/// limited by the video rate).
class VideoPreview {
public:
  VideoPreview(const std::string& title, PacketDemuxer& receiver);

  virtual ~VideoPreview();

  double getVideoBandwidthMbps() { return mbps; }
  double getFrameRate() { return fps; }
  size_t getFrameHeight() { return videoClient->getFrameHeight(); }
  size_t getFrameWidth() { return videoClient->getFrameWidth(); }


  void setRawBufferData(std::vector<float>& buffer) {
    rawBuffer = buffer;
  }

  void displayRawValues(bool displayRaw) {
    showRawPixelValues = displayRaw;
  }

  unsigned char* getBgrBuffer() {
    return bgrBuffer.data();
  }

protected:
  void startDecodeThread();

  void stopDecodeThread();

  /// Decode a video frame into the buffer.
  void decodeVideoFrame();

private:
  std::unique_ptr<VideoClient> videoClient;
  std::vector<std::uint8_t> bgrBuffer;
  std::vector<float> rawBuffer;
  double mbps;
  std::chrono::steady_clock::time_point m_lastFrameTime;
  double fps;
  int channels;
  std::unique_ptr<std::thread> videoDecodeThread;
  std::mutex bufferMutex;
  std::atomic<bool> newFrameDecoded;
  std::atomic<bool> runDecoderThread;
  bool showRawPixelValues;
};
