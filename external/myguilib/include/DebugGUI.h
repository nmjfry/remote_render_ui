#ifndef DEBUG_GUI_H
#define DEBUG_GUI_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

class DebugGUI {
public:
    DebugGUI();
    ~DebugGUI();

    bool Initialize(const char* window_title, int width, int height);
    void Cleanup();
    bool BeginFrame();
    void EndFrame();
    void ShowPreferencesWindow(bool* p_open);
    void UpdateImageTexture(const unsigned char* image_data, int width, int height);

private:
    struct Preferences {
        float background_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        bool show_grid = true;
        int display_mode = 0;
        float zoom_level = 1.0f;
    };

    GLFWwindow* window;
    Preferences prefs;
    bool show_demo_window = false;
    GLuint image_texture = 0;
    int image_width = 0;
    int image_height = 0;
};

#endif // DEBUG_GUI_H