# remote-ui-imgui

A Dear ImGui-based alternative to the nanogui client. Provides game-engine-style
camera controls:

- **Left-click drag** inside the viewport to mouse-look.
- **W/A/S/D** (or arrow keys) to translate.
- **Q / E** to move down / up.
- **Shift** to sprint.
- **Space** to reset pose.

Wire protocol is identical to the nanogui client — it sends `X`, `Y`, `Z`,
`env_rotation` (pitch), `env_rotation_2` (yaw), `fov`, `device`, `stop`, and
`detach` packets to the server.

## Building

The ImGui variant is **off by default** (the first configure clones ImGui from
GitHub via CMake FetchContent and can take a while on slow networks). Turn it
on with:

```bash
cd ~/splatting/nf20_splatting/remote_render_ui/build
cmake .. -DBUILD_IMGUI_UI=ON
make -j$(nproc) remote-ui-imgui
```

Both binaries (`remote-ui` and `remote-ui-imgui`) coexist when built. The
nanogui client remains the stable fallback.

## Running

```bash
./remote-ui-imgui --host <ipu-machine> --port 5000
```

All the usual options (`--host`, `--port`, `--width`, `--height`, `--log-level`)
are supported. The Kinect capture path and NIF path JSON are **not** wired in
yet — if you need those, use the nanogui `remote-ui` for now.

## Dependencies

ImGui is fetched automatically via CMake `FetchContent` from
`github.com/ocornut/imgui` (v1.90.5). No submodule setup needed.

GLFW is reused from the nanogui subproject (same OpenGL context setup that
nanogui uses).
