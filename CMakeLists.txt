cmake_minimum_required(VERSION 3.20)

project(remote-ui)

find_package(OpenGL REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options log)
find_package(OpenCV REQUIRED)

# Azure Kinect (k4a) frame capture is an optional feature (host-side webcam-style
# capture). It is OFF by default so the clients build on any machine without the
# Azure Kinect SDK installed. The viewer streams video from the render server
# over TCP and does not need this. Enable with -DENABLE_KINECT=ON.
option(ENABLE_KINECT "Build with Azure Kinect (k4a) capture support" OFF)
if(ENABLE_KINECT)
  find_package(k4a REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# GLFW does not compile without warnings:
add_compile_options(-Wno-sign-compare -Wno-missing-field-initializers -Wno-type-limits)

# Configure nanogui subproject:
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
set(NANOGUI_INSTALL       ON CACHE BOOL " " FORCE)
add_subdirectory(external/nanogui)
set_property(TARGET glfw glfw_objects nanogui PROPERTY FOLDER "dependencies")
add_definitions(${NANOGUI_EXTRA_DEFS})

# Configure packetcomms suboroject:
add_subdirectory(external/packetcomms)
add_subdirectory(external/videolib)

include_directories(${OpenCV_INCLUDE_DIRS} ${NANOGUI_EXTRA_INCS} ${PACKETCOMMS_INCLUDES} ${VIDEOLIB_INCLUDES})

# ============================================================================
#   remote-ui  — nanogui-based client (original, kept as stable fallback)
# ============================================================================
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.hpp ${PROJECT_SOURCE_DIR}/src/*.cpp)
if(NOT ENABLE_KINECT)
  # VideoCapture is the only k4a-dependent unit; drop it when Kinect is off.
  list(REMOVE_ITEM SRC
       ${PROJECT_SOURCE_DIR}/src/VideoCapture.cpp
       ${PROJECT_SOURCE_DIR}/src/VideoCapture.hpp)
endif()
add_executable(remote-ui ${SRC})
target_link_libraries(
  remote-ui
  Boost::program_options Boost::log nanogui
  ${NANOGUI_EXTRA_LIBS} ${OPENGL_LIBRARIES}
  ${PACKETCOMMS_LIBRARIES} ${VIDEOLIB_LIBRARIES}
  ${OpenCV_LIBS}
)

target_compile_definitions(remote-ui PRIVATE -DBOOST_LOG_DYN_LINK)
if(ENABLE_KINECT)
  target_link_libraries(remote-ui k4a::k4a)
  target_compile_definitions(remote-ui PRIVATE -DENABLE_KINECT)
endif()

# ============================================================================
#   remote-ui-imgui  — Dear ImGui-based client (optional, game-style controls)
# ============================================================================
option(BUILD_IMGUI_UI "Build the Dear ImGui variant of the remote UI" OFF)

if(BUILD_IMGUI_UI)
  set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/external/imgui
      CACHE PATH "Path to a local clone of ocornut/imgui")
  if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR
      "BUILD_IMGUI_UI=ON but could not find imgui.cpp at ${IMGUI_DIR}.\n"
      "Clone it with:\n"
      "  git clone --depth 1 --branch v1.90.5 https://github.com/ocornut/imgui.git external/imgui\n"
      "or pass -DIMGUI_DIR=/path/to/imgui to cmake.")
  endif()
  message(STATUS "Using ImGui at ${IMGUI_DIR}")

  # ImGui does not ship a CMakeLists — build the core + the GLFW/OpenGL backend
  # as a static lib here.
  add_library(imgui_core STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui_core PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
  )
  # Reuse the GLFW that nanogui already builds:
  target_link_libraries(imgui_core PUBLIC glfw ${OPENGL_LIBRARIES} ${CMAKE_DL_LIBS})

  file(GLOB_RECURSE IMGUI_SRC
       ${PROJECT_SOURCE_DIR}/src_imgui/*.hpp
       ${PROJECT_SOURCE_DIR}/src_imgui/*.cpp)
  # Re-use the non-UI pieces of the original build (packet descriptions, TCP
  # video client, etc.) — they have no nanogui dependency. The ImGui client
  # receives video over TCP (VideoClient) and never uses the Kinect path.
  list(APPEND IMGUI_SRC ${PROJECT_SOURCE_DIR}/src/VideoClient.cpp)
  if(ENABLE_KINECT)
    list(APPEND IMGUI_SRC ${PROJECT_SOURCE_DIR}/src/VideoCapture.cpp)
  endif()

  add_executable(remote-ui-imgui ${IMGUI_SRC})
  target_include_directories(remote-ui-imgui PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(
    remote-ui-imgui
    imgui_core
    Boost::program_options Boost::log
    ${OPENGL_LIBRARIES}
    ${PACKETCOMMS_LIBRARIES} ${VIDEOLIB_LIBRARIES}
    ${OpenCV_LIBS}
  )
  target_compile_definitions(remote-ui-imgui PRIVATE -DBOOST_LOG_DYN_LINK)
  if(ENABLE_KINECT)
    target_link_libraries(remote-ui-imgui k4a::k4a)
    target_compile_definitions(remote-ui-imgui PRIVATE -DENABLE_KINECT)
  endif()
endif()
