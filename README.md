# Remote Render UI

A remote viewer client for the **IPU 3D Gaussian Splatting** render server. The
server (separate repo, `gaussian_splat_ipu`) renders novel views on a Graphcore
IPU and streams them over TCP; this client decodes the video stream, displays
it, and sends camera/control packets back.

There are two clients in this repo:

| Binary | Toolkit | Notes |
|---|---|---|
| `remote-ui-imgui` | Dear ImGui | **Recommended.** Game-style WASD + mouse-look controls. |
| `remote-ui` | nanogui | Original/stable fallback, form-style controls. |

Tested on Ubuntu (20.04+). Should also work anywhere the dependencies below are
available.

---

## Dependencies

### System packages

- **CMake** (3.20 or newer) and **Ninja** (`pip install cmake ninja` if your distro ships older).
- A **C++17** compiler (GCC ≥ 9 / Clang).
- **Boost** (`program_options`, `log`), tested with 1.71.
- **OpenCV** (core, imgproc, imgcodecs, videoio).
- **FFmpeg** dev libraries (used by the `videolib` submodule for video decode):
  `libavcodec`, `libavformat`, `libavutil`, `libswscale`.
- **OpenGL + X11 dev** headers (needed to build the bundled GLFW / nanogui).

On Ubuntu:

```bash
sudo apt install \
  cmake ninja-build build-essential \
  libboost-program-options-dev libboost-log-dev \
  libopencv-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  xorg-dev libgl1-mesa-dev libglu1-mesa-dev
```

> `cmake` from apt may be too old. If `cmake --version` reports less than 3.20,
> install a newer one with `pip install --upgrade cmake`.

### Bundled (git submodules, no manual install)

- [nanogui](https://github.com/mitsuba-renderer/nanogui) (also provides GLFW)
- [packetcomms](https://github.com/mpups/packetcomms): low-latency TCP protocol
- [videolib](https://github.com/markp-gc/videolib): FFmpeg wrapper for TCP video
- [imgui](https://github.com/ocornut/imgui): for `remote-ui-imgui`

### Optional: Azure Kinect (k4a)

Host-side Kinect capture is **off by default** and is **not needed** to view the
render stream. Only enable it (`-DENABLE_KINECT=ON`) if you have the
[Azure Kinect SDK](https://learn.microsoft.com/azure/kinect-dk/sensor-sdk-download)
installed and specifically want local capture.

---

## Building

```bash
# 1. Clone with submodules
git clone --recursive <this-repo-url> remote_render_ui
cd remote_render_ui
# (if you forgot --recursive: git submodule update --init --recursive)

# 2. Configure (ImGui client ON; nanogui client always builds)
cmake -G Ninja -S . -B build -DBUILD_IMGUI_UI=ON

# 3. Build
ninja -C build
```

Produces `build/remote-ui-imgui` and `build/remote-ui`.

Options:
- `-DBUILD_IMGUI_UI=ON`: also build the ImGui client (default OFF).
- `-DENABLE_KINECT=ON`: build with Azure Kinect capture (default OFF; needs the k4a SDK).

---

## Running

Start the IPU render server first (see the `gaussian_splat_ipu` repo); it listens
on a UI port (e.g. 5000). Then launch a client pointed at it:

```bash
# ImGui client (recommended)
./build/remote-ui-imgui --host <server-hostname-or-ip> --port 5000

# nanogui client
./build/remote-ui --hostname <server-hostname-or-ip> --port 5000
```

Run either with `--help` for the full option list (`--width`, `--height`,
`--log-level` and others).

### Controls (`remote-ui-imgui`)

| Input | Action |
|---|---|
| **W A S D** / arrows | translate (forward / back / strafe) |
| **Q / E** | down / up |
| **left-click-drag** in viewport | mouse-look (pitch / yaw) |
| **Shift** | sprint (faster movement) |
| **Space** | reset pose |

Both clients also have a **Screenshot** button that saves the current decoded
frame as a timestamped PNG.
