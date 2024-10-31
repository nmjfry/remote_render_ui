// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "VideoPreview.hpp"

#include <PacketComms.h>

#include <boost/log/trivial.hpp>

VideoPreview::VideoPreview(
    const std::string& title,
    PacketDemuxer& receiver) :
      videoClient(std::make_unique<VideoClient>(receiver, "render_preview")),
      mbps(0.0),
      m_lastFrameTime(std::chrono::steady_clock::now()),
      fps(0.f),
      newFrameDecoded(false),
      runDecoderThread(true),
      showRawPixelValues(false) {
  using namespace std::chrono_literals;
  bool videoOk = videoClient->initialiseVideoStream(5s);

  if (videoOk) {
    // Allocate a buffer to store the decoded and converted images:
    auto w = videoClient->getFrameWidth();
    auto h = videoClient->getFrameHeight();

    startDecodeThread();

  } else {
    BOOST_LOG_TRIVIAL(warning) << "Failed to initialise video stream.";
  }
}

VideoPreview::~VideoPreview() {
  stopDecodeThread();
}

void VideoPreview::startDecodeThread() {
  // Thread just decodes video frames as fast as it can:
  videoDecodeThread.reset(new std::thread([&]() {
    BOOST_LOG_TRIVIAL(debug) << "Video decode thread launched.";
    if (videoClient == nullptr) {
      BOOST_LOG_TRIVIAL(debug) << "Video client must be initialised before decoding.";
      throw std::logic_error("No VideoClient object available.");
    }

    while (runDecoderThread) {
      decodeVideoFrame();
    }
  }));
}

void VideoPreview::stopDecodeThread() {
  runDecoderThread = false;
  if (videoDecodeThread) {
    try {
      videoDecodeThread->join();
      BOOST_LOG_TRIVIAL(debug) << "Video decode thread joined successfully.";
      videoDecodeThread.reset();
    } catch (std::system_error& e) {
      BOOST_LOG_TRIVIAL(warning) << "Video decode thread could not be joined.";
    }
  }
}

/// Decode a video frame into the buffer.
void VideoPreview::decodeVideoFrame() {
  newFrameDecoded = videoClient->receiveVideoFrame(
      [this](LibAvCapture& stream) {
        BOOST_LOG_TRIVIAL(debug) << "Decoded video frame";
        auto w = stream.GetFrameWidth();
        auto h = stream.GetFrameHeight();
        if (channels == 3 || channels == 4) {
          // Extract decoded data to the buffer:
          std::lock_guard<std::mutex> lock(bufferMutex);
          if (channels == 3) {
            stream.ExtractRgbImage(bgrBuffer.data(), w * channels);
          } else if (channels == 4) {
            stream.ExtractRgbaImage(bgrBuffer.data(), w * channels);
          } else {
            throw std::runtime_error("Unsupported number of texture channels");
          }
        }
      });

  if (newFrameDecoded) {
    double bps = videoClient->computeVideoBandwidthConsumed();
    auto imbps = bps / (1024.0 * 1024.0);
    mbps = (0.9f * mbps) + (.1f * imbps);
    BOOST_LOG_TRIVIAL(trace) << "Video bit-rate instantaneous: " << imbps << " Mbps" << std::endl;
    BOOST_LOG_TRIVIAL(debug) << "Video bit-rate filtered: " << mbps << " Mbps" << std::endl;

    // Calculate instantaneous frame rate:
    auto newFrameTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(newFrameTime - m_lastFrameTime).count();
    auto ifps = 1000.0 / (std::max(elapsed, 1L));
    fps = (0.9f * fps) + (.1f * ifps);
    BOOST_LOG_TRIVIAL(trace) << "Frame rate instantaneous: " << ifps << " Fps" << std::endl;
    BOOST_LOG_TRIVIAL(debug) << "Frame rate filtered: " << fps << " Fps" << std::endl;
    m_lastFrameTime = newFrameTime;
  }
}
