// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "RenderClient.hpp"
#include "VideoCapture.hpp"

#include <GLFW/glfw3.h>
#include <PacketSerialisation.h>
#include <VideoLib.h>
#include <iomanip>

RenderClient::RenderClient(PacketMuxer& tx, PacketDemuxer& rx)
    : sender(tx) {

  syncWithServer(tx, rx, "ready");
}

RenderClient::~RenderClient() {
  // Tell the server we are disconnecting so
  // it can cleanly tear down its communications:
  serialise(sender, "detach", true);
}
