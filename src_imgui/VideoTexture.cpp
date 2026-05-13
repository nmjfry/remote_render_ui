#include "VideoTexture.hpp"

#include <PacketComms.h>
#include <boost/log/trivial.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <GL/gl.h>

#include <chrono>

VideoTexture::VideoTexture(PacketDemuxer& receiver)
  : videoClient(std::make_unique<VideoClient>(receiver, "render_preview")),
    lastFrameTime(std::chrono::steady_clock::now()) {
  using namespace std::chrono_literals;
  if (!videoClient->initialiseVideoStream(5s)) {
    BOOST_LOG_TRIVIAL(warning) << "Failed to initialise video stream.";
    return;
  }
  frameW = videoClient->getFrameWidth();
  frameH = videoClient->getFrameHeight();
  rgbBuffer.assign(frameW * frameH * 3, 0);

  // Create the GL texture up front. We update it via glTexSubImage2D each frame.
  glGenTextures(1, &glTexture);
  glBindTexture(GL_TEXTURE_2D, glTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameW, frameH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);

  BOOST_LOG_TRIVIAL(info) << "VideoTexture: " << frameW << "x" << frameH;
  startDecodeThread();
}

VideoTexture::~VideoTexture() {
  stopDecodeThread();
  if (glTexture) glDeleteTextures(1, &glTexture);
}

void VideoTexture::startDecodeThread() {
  decodeThread.reset(new std::thread([this]() {
    while (runDecoder) decodeOne();
  }));
}

void VideoTexture::stopDecodeThread() {
  runDecoder = false;
  if (decodeThread && decodeThread->joinable()) decodeThread->join();
  decodeThread.reset();
}

void VideoTexture::decodeOne() {
  bool got = videoClient->receiveVideoFrame([this](LibAvCapture& stream) {
    int w = stream.GetFrameWidth();
    int h = stream.GetFrameHeight();
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (w != frameW || h != frameH) return; // shouldn't happen once initialised
    stream.ExtractRgbImage(rgbBuffer.data(), w * 3);
    newFrame = true;
  });
  if (got) {
    double bps = videoClient->computeVideoBandwidthConsumed();
    double imbps = bps / (1024.0 * 1024.0);
    mbps = 0.9 * mbps + 0.1 * imbps;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
    double ifps = 1000.0 / std::max<long>(elapsedMs, 1);
    fps = 0.9 * fps + 0.1 * ifps;
    lastFrameTime = now;
  }
}

void VideoTexture::uploadIfDirty() {
  if (!glTexture || !newFrame.exchange(false)) return;
  std::lock_guard<std::mutex> lock(bufferMutex);
  glBindTexture(GL_TEXTURE_2D, glTexture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameW, frameH, GL_RGB, GL_UNSIGNED_BYTE, rgbBuffer.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

std::string VideoTexture::saveScreenshot(const std::string& path) const {
  if (!ready()) return {};
  // Snapshot under the mutex — the decode thread may be writing concurrently.
  cv::Mat rgb(frameH, frameW, CV_8UC3);
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::memcpy(rgb.data, rgbBuffer.data(), rgbBuffer.size());
  }
  cv::Mat bgr;
  cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
  if (!cv::imwrite(path, bgr)) {
    BOOST_LOG_TRIVIAL(warning) << "Failed to write screenshot: " << path;
    return {};
  }
  BOOST_LOG_TRIVIAL(info) << "Saved screenshot: " << path;
  return path;
}
