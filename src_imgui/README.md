# remote-ui-imgui

A Dear ImGui-based alternative to the nanogui client. Provides game-engine-style
camera controls:

- **Left-click drag** inside the viewport to mouse-look.
- **W/A/S/D** (or arrow keys) to translate.
- **Q / E** to move down / up.
- **Shift** to sprint.
- **Space** to reset pose.

The wire protocol is identical to the nanogui client: it sends `X`, `Y`, `Z`,
`env_rotation` (pitch), `env_rotation_2` (yaw), `fov`, `device`, `stop`, and
`detach` packets to the server.

## Building

The ImGui variant is **off by default**. Turn it on at configure time:

```bash
cmake -G Ninja -S . -B build -DBUILD_IMGUI_UI=ON
ninja -C build remote-ui-imgui
```

Both binaries (`remote-ui` and `remote-ui-imgui`) coexist when built. The
nanogui client remains the stable fallback. See the top-level `README.md` for
the full dependency list and clone-and-run steps.

## Running

```bash
./remote-ui-imgui --host <ipu-machine> --port 5000
```

All the usual options (`--host`, `--port`, `--width`, `--height`, `--log-level`)
are supported. The Kinect capture path and NIF path JSON are not wired in yet; if you need
those, use the nanogui `remote-ui` for now.

## Dependencies

ImGui is vendored as a git submodule at `external/imgui` (pulled in by
`git clone --recursive`). GLFW is reused from the nanogui subproject (same
OpenGL context setup that nanogui uses).
