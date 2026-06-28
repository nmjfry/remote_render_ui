// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <PacketComms.h>
#include <nanogui/nanogui.h>

#include "ControlsForm.hpp"
#include "VideoPreviewWindow.hpp"
#if defined(ENABLE_KINECT)
#include "VideoCapture.hpp"  // pulls in <k4a/k4a.h>; only when Kinect is enabled
#endif

/// A screen containing all the application's other windows.
class RenderClientApp : public nanogui::Screen {
public:
  RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& sender, PacketDemuxer& receiver);
  virtual ~RenderClientApp();

  virtual bool keyboard_event(int key, int scancode, int action, int modifiers);
  virtual bool mouse_motion_event(const nanogui::Vector2i& p,
                                  const nanogui::Vector2i& rel,
                                  int button, int modifiers);
  virtual bool mouse_button_event(const nanogui::Vector2i& p,
                                  int button, bool down, int modifiers);

  virtual void draw(NVGcontext* ctx);

private:
  // ---- FPS camera state (world-space offset from initial scene view) ----
  float camX = 0.f, camY = 0.f, camZ = 0.f;
  float pitchDeg = 0.f, yawDeg = 0.f;

  // Held keys for WASD-style movement (updated each frame in draw()):
  bool keyW = false, keyA = false, keyS = false, keyD = false;
  bool keyQ = false, keyE = false;
  bool keyShift = false;

  // Mouse-look state: left-click inside the preview starts a look-drag.
  // The click is consumed before nanogui's Window sees it, so the window
  // does not move.
  bool lookDragActive = false;

  // When true, mouse DOWN tilts the camera UP (flight-sim / inverted-Y style).
  // Toggle at runtime with the Y key.
  bool invertPitch = true;

  // Timing for frame-rate-independent movement:
  double lastDrawTime = 0.0;

  void sendPose();

  PacketMuxer& sender;
  VideoPreviewWindow* preview;
  std::unique_ptr<std::thread> cameraThread;
  ControlsForm* form;
};
