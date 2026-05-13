// ImGui-based remote UI for the IPU 3DGS renderer.
// Provides a game-engine-style viewport: click-drag inside the render to
// mouse-look, WASD to move, Shift to sprint, Space to reset pose.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <thread>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include <PacketComms.h>
#include <PacketSerialisation.h>
#include <cereal/types/string.hpp>
#include <network/TcpSocket.h>

#include "PacketDescriptions.hpp"
#include "VideoTexture.hpp"

namespace po = boost::program_options;

// -------------------------------- CLI --------------------------------
static po::variables_map parseArgs(int argc, char** argv) {
  po::options_description desc("Options");
  desc.add_options()
    ("help", "Show help.")
    ("host",  po::value<std::string>()->default_value("localhost"))
    ("port",  po::value<int>()->default_value(3000))
    ("width,w",  po::value<int>()->default_value(1600))
    ("height,h", po::value<int>()->default_value(900))
    ("log-level", po::value<std::string>()->default_value("info"));
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  if (vm.count("help")) { std::cout << desc << "\n"; std::exit(0); }
  po::notify(vm);
  return vm;
}

// -------------------------------- Camera state --------------------------------
struct Pose {
  float X = 0.f, Y = 0.f, Z = 0.f;
  float pitchDeg = 0.f, yawDeg = 0.f;
};

static void sendPose(PacketMuxer& tx, const Pose& p) {
  serialise(tx, "X", p.X);
  serialise(tx, "Y", p.Y);
  serialise(tx, "Z", p.Z);
  serialise(tx, "env_rotation",   p.pitchDeg);
  serialise(tx, "env_rotation_2", p.yawDeg);
}

// -------------------------------- main --------------------------------
int main(int argc, char** argv) {
  auto args = parseArgs(argc, argv);

  namespace logging = boost::log;
  std::stringstream ss(args.at("log-level").as<std::string>());
  logging::trivial::severity_level level;  ss >> level;
  logging::core::get()->set_filter(logging::trivial::severity >= level);

  // ---- connect ----
  auto socket = std::make_unique<TcpSocket>();
  if (!socket->Connect(args.at("host").as<std::string>().c_str(), args.at("port").as<int>())) {
    BOOST_LOG_TRIVIAL(error) << "Could not connect to server.";
    return 1;
  }
  auto sender   = std::make_unique<PacketMuxer>(*socket, packets::packetTypes);
  auto receiver = std::make_unique<PacketDemuxer>(*socket, packets::packetTypes);

  // ---- GLFW + GL ----
  if (!glfwInit()) { BOOST_LOG_TRIVIAL(error) << "glfwInit failed"; return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  const int W = args.at("width").as<int>();
  const int H = args.at("height").as<int>();
  GLFWwindow* window = glfwCreateWindow(W, H, "IPU Gaussian Splat — ImGui", nullptr, nullptr);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // vsync

  // ---- ImGui ----
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO& io = ImGui::GetIO();
  // (Docking is only available on the docking branch of ImGui; skip it here.)

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");

  // ---- hand-shake with server ----
  syncWithServer(*sender, *receiver, "ready");

  // Render-time telemetry subscription (server pushes ms per frame).
  std::atomic<float> serverRenderMs{0.f};
  auto renderTimeSub = receiver->subscribe("render_time",
    [&serverRenderMs](const ComPacket::ConstSharedPacket& pkt) {
      float ms = 0.f;
      deserialise(pkt, ms);
      // EMA so the UI value is stable to read:
      float prev = serverRenderMs.load();
      serverRenderMs = 0.9f * prev + 0.1f * ms;
    });

  // FOV push from server (e.g. when launched with --from-pose). We keep the
  // slider in sync without sending back — the slider callback only fires on
  // user drag, so there's no feedback loop.
  std::atomic<float> serverFovDeg{-1.f};
  auto fovSub = receiver->subscribe("fov",
    [&serverFovDeg](const ComPacket::ConstSharedPacket& pkt) {
      float fov_half_rad = 0.f;
      deserialise(pkt, fov_half_rad);
      serverFovDeg = fov_half_rad * 2.f * 180.f / float(M_PI);
    });

  // ---- video preview ----
  auto video = std::make_unique<VideoTexture>(*receiver);

  // ---- control state ----
  Pose pose;
  float fovDeg = 60.f;           // UI slider value in degrees (full FOV)
  std::string device = "ipu";    // "ipu" or "cpu"
  bool stopRequested = false;
  bool poseDirty = true;
  bool invertPitch = true;       // flight-sim style (mouse down = look up)
  auto lastTime = std::chrono::steady_clock::now();

  while (!glfwWindowShouldClose(window) && !stopRequested) {
    glfwPollEvents();

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    if (dt > 0.1f) dt = 0.1f;

    // ---- WASD movement (world-space using current yaw) ----
    // When the viewport is focused (see below), arrow keys / WASD drive the
    // camera. We also handle Space and Escape here.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ============================ Controls ============================
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controls")) {
      ImGui::TextUnformatted("Camera: Click-drag viewport to look.");
      ImGui::TextUnformatted("WASD/arrows move, Q/E down/up, Shift sprint.");
      ImGui::TextUnformatted("Space: reset pose.");
      ImGui::Separator();
      // Absorb any server-pushed FOV without retransmitting:
      {
        float pushed = serverFovDeg.exchange(-1.f);
        if (pushed > 0.f) fovDeg = pushed;
      }
      if (ImGui::SliderFloat("Field of view", &fovDeg, 10.f, 120.f, "%.1f deg")) {
        serialise(*sender, "fov", fovDeg);
      }
      ImGui::Checkbox("Invert mouse Y (pitch)", &invertPitch);
      ImGui::Separator();
      const char* items[] = { "ipu", "cpu" };
      int idx = (device == "cpu") ? 1 : 0;
      if (ImGui::Combo("Device", &idx, items, 2)) {
        device = items[idx];
        serialise(*sender, "device", device);
      }
      ImGui::Separator();
      ImGui::Text("Pose: (%.2f, %.2f, %.2f)  yaw=%.1f pitch=%.1f",
                  pose.X, pose.Y, pose.Z, pose.yawDeg, pose.pitchDeg);
      if (video) {
        ImGui::Text("Video: %.1f Mbps, %.1f fps", video->getMbps(), video->getFps());
      }
      {
        float ms = serverRenderMs.load();
        float serverFps = ms > 0.f ? 1000.0f / ms : 0.f;
        ImGui::Text("Render: %.1f ms (%.1f fps)", ms, serverFps);
      }
      ImGui::Separator();
      if (ImGui::Button("Reset pose")) {
        pose = Pose{};
        poseDirty = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Screenshot") && video && video->ready()) {
        auto t  = std::time(nullptr);
        char tbuf[32];
        std::strftime(tbuf, sizeof(tbuf), "%Y%m%d-%H%M%S", std::localtime(&t));
        std::string path = std::string("screenshot-") + tbuf + ".png";
        auto written = video->saveScreenshot(path);
        if (!written.empty()) {
          BOOST_LOG_TRIVIAL(info) << "Saved screenshot to " << written;
        }
        // Also ask the server to dump its framebuffer + a pose JSON so
        // tools/gpu_watch.py can produce a matching GPU reference render.
        serialise(*sender, "screenshot", true);
      }
      ImGui::SameLine();
      if (ImGui::Button("Stop server")) {
        serialise(*sender, "stop", true);
        stopRequested = true;
      }
    }
    ImGui::End();

    // ============================ Viewport ============================
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Render", nullptr, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    if (video && video->ready()) {
      video->uploadIfDirty();
      ImVec2 avail = ImGui::GetContentRegionAvail();
      float scale = std::min(avail.x / video->width(), avail.y / video->height());
      ImVec2 imgSz(video->width() * scale, video->height() * scale);

      // Using ImageButton-style invisible button so we get the "this widget is
      // active" state that gives us click-drag behaviour regardless of whether
      // the cursor has moved onto another widget.
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImGui::Image(video->imguiTextureId(), imgSz);
      ImGui::SetCursorScreenPos(cursor);
      ImGui::InvisibleButton("##viewport_drag", imgSz,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      const bool viewportHovered = ImGui::IsItemHovered();
      const bool viewportActive  = ImGui::IsItemActive();

      // --- Mouse-look while the viewport drag is active ---
      if (viewportActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 d = io.MouseDelta;
        const float sens = 0.25f;
        const float pitchSign = invertPitch ? -1.f : 1.f;
        pose.yawDeg   += d.x * sens;
        pose.pitchDeg += d.y * sens * pitchSign;
        if (pose.pitchDeg >  89.f) pose.pitchDeg =  89.f;
        if (pose.pitchDeg < -89.f) pose.pitchDeg = -89.f;
        if (d.x != 0 || d.y != 0) poseDirty = true;
      }

      // --- WASD translation while the viewport is focused/hovered ---
      // We only consume keys when the viewport area is hovered or the ImGui
      // widget doesn't already want keyboard input.
      const bool captureKeys = viewportHovered || viewportActive || !io.WantCaptureKeyboard;
      if (captureKeys) {
        bool w = ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
        bool s = ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
        bool a = ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
        bool d = ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
        bool q = ImGui::IsKeyDown(ImGuiKey_Q);
        bool e = ImGui::IsKeyDown(ImGuiKey_E);
        bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
          pose = Pose{};
          poseDirty = true;
        }

        if (w || a || s || d || q || e) {
          // Speed in fractions of the scene diagonal per second (server
          // multiplies by scene scale). 0.3/s ≈ crosses the scene in ~3s.
          float speed = shift ? 1.0f : 0.3f;
          float yawRad   = pose.yawDeg   * float(M_PI / 180.0);
          float pitchRad = pose.pitchDeg * float(M_PI / 180.0);
          float sy = std::sin(yawRad),   cy = std::cos(yawRad);
          float sp = std::sin(pitchRad), cp = std::cos(pitchRad);

          // Camera-relative axes in world space. Right stays horizontal (normal
          // FPS strafe); forward and up include pitch so W/E fly in the actual
          // gaze / head-up direction.
          float fx = sy * cp, fy = -sp,   fz = -cy * cp;   // forward
          float rx = cy,      ry =  0.f,  rz =  sy;        // right
          float ux = sy * sp, uy =  cp,   uz = -cy * sp;   // up

          float dx = 0, dy = 0, dz = 0;
          if (w) { dx += fx; dy += fy; dz += fz; }
          if (s) { dx -= fx; dy -= fy; dz -= fz; }
          if (d) { dx += rx; dy += ry; dz += rz; }
          if (a) { dx -= rx; dy -= ry; dz -= rz; }
          if (e) { dx += ux; dy += uy; dz += uz; }
          if (q) { dx -= ux; dy -= uy; dz -= uz; }
          float len = std::sqrt(dx*dx + dy*dy + dz*dz);
          if (len > 1e-6f) {
            float k = speed * dt / len;
            pose.X += dx * k; pose.Y += dy * k; pose.Z += dz * k;
            poseDirty = true;
          }
        }
      }
    } else {
      ImGui::TextUnformatted("Waiting for video stream...");
    }
    ImGui::End();

    if (poseDirty) {
      sendPose(*sender, pose);
      poseDirty = false;
    }

    // ---- render ----
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  // Let the server know we're gone cleanly.
  serialise(*sender, "detach", true);

  video.reset();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();

  sender.reset();
  socket.reset();
  return 0;
}
