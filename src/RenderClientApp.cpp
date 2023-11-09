// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "RenderClientApp.hpp"
#include "VideoCapture.hpp"

#include <GLFW/glfw3.h>
#include <PacketSerialisation.h>
#include <VideoLib.h>
#include <iomanip>

RenderClientApp::RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& tx, PacketDemuxer& rx)
    : nanogui::Screen(size, "IPU Neural Render Preview", false),
      sender(tx),
      preview(nullptr),
      camera(nullptr),
      form(nullptr) {

  form = new ControlsForm(this, tx, rx, preview);

  syncWithServer(tx, rx, "ready");

  preview = new VideoPreviewWindow(this, "Render Preview", rx);

  camera = new VideoCapture(tx);

  // // --kinect set to true
  // if (camera != NULL) {
  //   k4a_image_t image;
  //   captureFrame(device, image, capture, TIMEOUT_IN_MS);
  //   // initialiseVideoStream();
  // }



  // Have to manually set positions due to bug in ComboBox:
  const int margin = 10;
  nanogui::Vector2i pos(margin, margin);
  preview->set_position(pos);
  pos[0] += margin + preview->width();
  form->set_position(nanogui::Vector2i(pos));
  perform_layout();
}

RenderClientApp::~RenderClientApp() {
  // Tell the server we are disconnecting so
  // it can cleanly tear down its communications:
  serialise(sender, "detach", true);
}

bool RenderClientApp::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (Screen::keyboard_event(key, scancode, action, modifiers)) {
    return true;
  }

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_R) {
      preview->reset();
      return true;
    }
    if (key == GLFW_KEY_ESCAPE) {
      set_visible(false);
      return true;
    }
  }

  return false;
}

void RenderClientApp::draw(NVGcontext* ctx) {
  if (preview != nullptr && form != nullptr) {
    // Update bandwidth text before display:
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2)
       << preview->getVideoBandwidthMbps();
    form->bitRateText->set_value(ss.str());
    // Update frame rate text:
    ss.str(std::string());
    ss << std::fixed << std::setprecision(2)
       << preview->getFrameRate();
    form->frameRateText->set_value(ss.str());
  }
  Screen::draw(ctx);
}
