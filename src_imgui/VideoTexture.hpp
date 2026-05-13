// SPDX-License-Identifier: MIT
// Minimal GL-texture-backed video preview for the ImGui client.
// Streams decoded frames from the existing VideoClient into an OpenGL
// texture that ImGui can draw via ImGui::Image.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "VideoClient.hpp"

class PacketDemuxer;

class VideoTexture {
public:
  VideoTexture(PacketDemuxer& receiver);
  ~VideoTexture();

  // Upload the most-recently-decoded frame to the GPU. Must be called on the
  // thread that owns the GL context (the main thread).
  void uploadIfDirty();

  // ImGui wants an ImTextureID which is just an opaque integer; for the
  // OpenGL backend it's the GL texture handle cast to void*.
  void* imguiTextureId() const { return (void*)(intptr_t)glTexture; }

  int width()  const { return frameW; }
  int height() const { return frameH; }
  bool ready() const { return frameW > 0 && frameH > 0; }

  double getMbps() const { return mbps; }
  double getFps()  const { return fps;  }

  // Save the most-recently-decoded frame to a PNG file (BGR on disk so
  // colour survives the OpenCV round-trip). Returns the actual path written,
  // empty on failure.
  std::string saveScreenshot(const std::string& path) const;

private:
  void startDecodeThread();
  void stopDecodeThread();
  void decodeOne();

  std::unique_ptr<VideoClient> videoClient;

  // Decoded frame, always stored as RGB 8-bit, guarded by a mutex. Marked
  // mutable so that const methods (screenshot saving) can take a brief lock
  // to snapshot the latest frame.
  mutable std::vector<std::uint8_t> rgbBuffer;
  mutable std::mutex bufferMutex;
  std::atomic<bool> newFrame{false};

  int frameW = 0, frameH = 0;

  // GL handle — zero until first upload.
  unsigned int glTexture = 0;

  // Stats
  double mbps = 0.0;
  double fps  = 0.0;
  std::chrono::steady_clock::time_point lastFrameTime;

  std::atomic<bool> runDecoder{true};
  std::unique_ptr<std::thread> decodeThread;
};
