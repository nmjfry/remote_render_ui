// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "RenderClientApp.hpp"
#include "VideoCapture.hpp"

#include <GLFW/glfw3.h>
#include <PacketSerialisation.h>
#include <VideoLib.h>
#include <iomanip>
#include <cmath>

RenderClientApp::RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& tx, PacketDemuxer& rx)
    : nanogui::Screen(size, "IPU Neural Render Preview", false),
      sender(tx),
      preview(nullptr),
      form(nullptr) {

  form = new ControlsForm(this, tx, rx, preview);

  syncWithServer(tx, rx, "ready");

  // TODO: make this not so weird (for the camera thread)
  // cameraThread.reset(new std::thread([&]() {new VideoCapture(tx);}));
  preview = new VideoPreviewWindow(this, "Render Preview", rx);

  // Have to manually set positions due to bug in ComboBox:
  const int margin = 10;
  nanogui::Vector2i pos(margin, margin);
  preview->set_position(pos);
  pos[0] += margin + preview->width();
  form->set_position(nanogui::Vector2i(pos));
  perform_layout();

  lastDrawTime = glfwGetTime();
}

RenderClientApp::~RenderClientApp() {
  // Tell the server we are disconnecting so
  // it can cleanly tear down its communications:
  serialise(sender, "detach", true);
}

void RenderClientApp::sendPose() {
  // The server expects world-space offsets (X, Y, Z) and pitch/yaw in degrees
  // (envRotationDegrees, envRotationDegrees2).
  serialise(sender, "X", camX);
  serialise(sender, "Y", camY);
  serialise(sender, "Z", camZ);
  serialise(sender, "env_rotation",   pitchDeg);
  serialise(sender, "env_rotation_2", yawDeg);
}

bool RenderClientApp::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (Screen::keyboard_event(key, scancode, action, modifiers)) {
    return true;
  }

  // Track shift state for sprint:
  if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
    keyShift = (action != GLFW_RELEASE);
    return true;
  }

  // WASD / arrow keys — hold to move; we process held state in draw() so motion
  // stays smooth regardless of OS key-repeat rate.
  auto setHeld = [&](bool& flag) {
    if (action == GLFW_PRESS)   flag = true;
    if (action == GLFW_RELEASE) flag = false;
  };

  switch (key) {
    case GLFW_KEY_W: case GLFW_KEY_UP:    setHeld(keyW); return true;
    case GLFW_KEY_S: case GLFW_KEY_DOWN:  setHeld(keyS); return true;
    case GLFW_KEY_A: case GLFW_KEY_LEFT:  setHeld(keyA); return true;
    case GLFW_KEY_D: case GLFW_KEY_RIGHT: setHeld(keyD); return true;
    case GLFW_KEY_Q:                      setHeld(keyQ); return true;
    case GLFW_KEY_E:                      setHeld(keyE); return true;
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
    // Reset pose with Space:
    if (key == GLFW_KEY_SPACE) {
      camX = camY = camZ = 0.f;
      pitchDeg = yawDeg = 0.f;
      sendPose();
      return true;
    }
    // Toggle inverted-Y mouse-look with Y:
    if (key == GLFW_KEY_Y) {
      invertPitch = !invertPitch;
      return true;
    }
  }

  return false;
}

// Check whether a screen-space point is inside the preview window's rect.
// Preview is a nanogui::Window whose position() is in screen coords.
static bool insidePreview(const nanogui::Widget* preview, const nanogui::Vector2i& p) {
  if (!preview) return false;
  auto pos  = preview->position();
  auto size = preview->size();
  return p.x() >= pos.x() && p.x() < pos.x() + size.x()
      && p.y() >= pos.y() && p.y() < pos.y() + size.y();
}

bool RenderClientApp::mouse_button_event(const nanogui::Vector2i& p,
                                         int button, bool down, int modifiers) {
  // Left-click inside the preview area → start camera-look drag.
  // We MUST intercept this before dispatching to children, otherwise the
  // preview Window (a nanogui::Window) eats the click and may start dragging
  // itself.
  if (button == GLFW_MOUSE_BUTTON_LEFT && insidePreview(preview, p)) {
    lookDragActive = down;
    return true;
  }
  // All other clicks (ControlsForm, sliders, etc.) go to nanogui as normal.
  if (Screen::mouse_button_event(p, button, down, modifiers)) {
    return true;
  }
  return false;
}

bool RenderClientApp::mouse_motion_event(const nanogui::Vector2i& p,
                                         const nanogui::Vector2i& rel,
                                         int button, int modifiers) {
  // Active look-drag gets priority — don't let nanogui process the motion.
  if (lookDragActive) {
    const float sensitivity = 0.25f;
    const float pitchSign = invertPitch ? -1.f : 1.f;
    yawDeg   += rel.x() * sensitivity;
    pitchDeg += rel.y() * sensitivity * pitchSign;
    if (pitchDeg >  89.f) pitchDeg =  89.f;
    if (pitchDeg < -89.f) pitchDeg = -89.f;
    sendPose();
    return true;
  }
  return Screen::mouse_motion_event(p, rel, button, modifiers);
}

void RenderClientApp::draw(NVGcontext* ctx) {
  // Frame-rate-independent WASD movement. We convert camera-local movement
  // (W=forward, D=right, E=up) into world-space deltas using the current yaw.
  // Pitch doesn't contribute to horizontal strafe movement, matching typical
  // FPS "walk" behaviour.
  double now = glfwGetTime();
  float dt = float(now - lastDrawTime);
  lastDrawTime = now;
  if (dt > 0.1f) dt = 0.1f; // clamp big gaps (e.g. first frame)

  if (keyW || keyA || keyS || keyD || keyQ || keyE) {
    // Speed in fractions of the scene diagonal per second (the server
    // multiplies by the scene scale). 0.3/s ≈ crosses the scene in ~3 s.
    const float speed = (keyShift ? 1.0f : 0.3f);

    const float yawRad   = float(yawDeg   * M_PI / 180.f);
    const float pitchRad = float(pitchDeg * M_PI / 180.f);
    const float sy = std::sin(yawRad),   cy = std::cos(yawRad);
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);

    // Camera-relative axes in world space. The server applies R_pitch * R_yaw
    // to the scene in view space, which is the inverse of the user's camera
    // rotation in world. Undoing those rotations on (0,0,-1) / (1,0,0) / (0,1,0)
    // gives the user's local forward / right / up in world coords:
    //   forward = ( sin(yaw)·cos(pitch), -sin(pitch), -cos(yaw)·cos(pitch))
    //   right   = ( cos(yaw),             0,           sin(yaw)          )
    //   up      = ( sin(yaw)·sin(pitch),  cos(pitch), -cos(yaw)·sin(pitch))
    // Right stays horizontal (standard FPS strafe); forward/up include pitch so
    // W / Q / E fly in the direction the user is actually looking.
    const float fx = sy * cp, fy = -sp, fz = -cy * cp;
    const float rx = cy,       ry = 0.f, rz =  sy;
    const float ux = sy * sp,  uy = cp,  uz = -cy * sp;

    float dx = 0.f, dy = 0.f, dz = 0.f;
    if (keyW) { dx += fx; dy += fy; dz += fz; }
    if (keyS) { dx -= fx; dy -= fy; dz -= fz; }
    if (keyD) { dx += rx; dy += ry; dz += rz; }
    if (keyA) { dx -= rx; dy -= ry; dz -= rz; }
    if (keyE) { dx += ux; dy += uy; dz += uz; }
    if (keyQ) { dx -= ux; dy -= uy; dz -= uz; }

    // Normalise diagonal speed:
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
      float k = speed * dt / len;
      camX += dx * k;
      camY += dy * k;
      camZ += dz * k;
      sendPose();
    }
  }

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
