#pragma once

#include <PacketComms.h>

#include "VideoPreview.hpp"

class RenderClient {
public:
  RenderClient(PacketMuxer& sender, PacketDemuxer& receiver);
  virtual ~RenderClient();

private:
  PacketMuxer& sender;
  std::unique_ptr<std::thread> cameraThread;
};