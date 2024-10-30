#include "DebugGUI.h"

// Include additional libraries and ImGui setup if needed

DebugGUI::DebugGUI() : window(nullptr), image_texture(0), image_width(0), image_height(0) {}

DebugGUI::~DebugGUI() {
    Cleanup();
}

bool DebugGUI::Initialize(const char* window_title, int width, int height) {
     // Initialize GLFW
    if (!glfwInit())
        return false;

    // GL 3.3 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create window with graphics context
    window = glfwCreateWindow(width, height, window_title, NULL, NULL);
    if (window == NULL)
        return false;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize GLEW
    if (glewInit() != GLEW_OK)
        return false;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();  // Uncomment for light theme

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    return true;
}

void DebugGUI::Cleanup() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

bool DebugGUI::BeginFrame() {
    if (glfwWindowShouldClose(window))
        return false;

    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Set up a dockspace over the entire application window
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.f); 
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("DockSpace Demo", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // DockSpace
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();

    return true;
}

void DebugGUI::EndFrame() {
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(prefs.background_color[0], prefs.background_color[1],
                prefs.background_color[2], prefs.background_color[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
}

void DebugGUI::ShowPreferencesWindow(bool* p_open) {
    if (ImGui::Begin("Preferences", p_open)) {
        ImGui::ColorEdit4("Background Color", prefs.background_color);
        ImGui::Checkbox("Show Grid", &prefs.show_grid);
        ImGui::SliderFloat("Zoom Level", &prefs.zoom_level, 0.1f, 10.0f);
        const char* display_modes[] = { "Normal", "Wireframe", "Debug" };
        ImGui::Combo("Display Mode", &prefs.display_mode, display_modes, IM_ARRAYSIZE(display_modes));
    }
    ImGui::End();
}

void DebugGUI::UpdateImageTexture(const unsigned char* image_data, int width, int height) {
    if (image_texture == 0 || width != image_width || height != image_height) {
        // Delete old texture if it exists
        if (image_texture != 0) {
            glDeleteTextures(1, &image_texture);
        }

        // Generate new texture
        glGenTextures(1, &image_texture);
        glBindTexture(GL_TEXTURE_2D, image_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        image_width = width;
        image_height = height;
    }

    // Update texture data
    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, 
                GL_UNSIGNED_BYTE, image_data);
}