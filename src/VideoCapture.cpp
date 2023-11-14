#include "VideoCapture.hpp"
#include <cstddef>

VideoCapture::VideoCapture(PacketMuxer &sender)
    : device(NULL), runEncoderThread(false) {
  BOOST_LOG_TRIVIAL(info) << "Starting video capture.";

  // Lambda that enqueues video packets via the Muxing system:
  FFMpegStdFunctionIO videoIO(
      FFMpegCustomIO::WriteBuffer, [&](uint8_t *buffer, int size) {
        BOOST_LOG_TRIVIAL(info)
            << "Sending compressed video packet of size: " << size;
        sender.emplacePacket("render_preview",
                             reinterpret_cast<VectorStream::CharType *>(buffer),
                             size);
        return sender.ok() ? size : -1;
      });
  videoStream.reset(new LibAvWriter(videoIO));

  configure();
  initialiseVideoStream();

  while (runEncoderThread && sender.ok()) {
    std::this_thread::sleep_for(35ms);
    captureFrame();
  }
  BOOST_LOG_TRIVIAL(info) << "Stopping camera feed.";
}

VideoCapture::~VideoCapture() {
  if (device != NULL) {
    k4a_device_close(device);
  }
  runEncoderThread = false;
  BOOST_LOG_TRIVIAL(info) << "Camera thread shutting down...";
}

void VideoCapture::initialiseVideoStream() {
  if (videoStream) {
    bool ok = videoStream->AddVideoStream(width, height, 30,
                                video::FourCc('F', 'M', 'P', '4'));
    BOOST_LOG_TRIVIAL(info) << "Succesfully initialised camera stream. ";
    runEncoderThread = runEncoderThread && ok;
  } else {
    BOOST_LOG_TRIVIAL(error) << "No object to add video stream to.";
  }
}

void VideoCapture::configure() {

  uint32_t device_count = k4a_device_get_installed_count();
  if (device_count == 0) {
    BOOST_LOG_TRIVIAL(info) << "Could not connect to K4a, no devices found ";
    return;
  }

  if (K4A_RESULT_SUCCEEDED != k4a_device_open(K4A_DEVICE_DEFAULT, &device)) {
    BOOST_LOG_TRIVIAL(info)
        << "Could not connect to K4a, failed to open device ";
    return;
  }

  k4a_device_configuration_t config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
  config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
  config.color_resolution = K4A_COLOR_RESOLUTION_2160P;
  config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
  config.camera_fps = K4A_FRAMES_PER_SECOND_30;
  config.synchronized_images_only = true;

  if (K4A_RESULT_SUCCEEDED != k4a_device_start_cameras(device, &config)) {
    BOOST_LOG_TRIVIAL(info) << "Failed to start device ";
    return;
  }

  k4a_image_t image;
  k4a_capture_t capture = NULL;

  // Get a depth frame
  switch (k4a_device_get_capture(device, &capture, CAPTURE_TIMEOUT_IN_MS)) {
  case K4A_WAIT_RESULT_SUCCEEDED:
    break;
  case K4A_WAIT_RESULT_TIMEOUT:
    printf("Timed out waiting for a capture\n");
    return;
  case K4A_WAIT_RESULT_FAILED:
    printf("Failed to read a capture\n");
    return;
  }

  printf("Capture");

  // Probe for a color image and set the image stream dimensions.
  image = k4a_capture_get_color_image(capture);
  printf("\n %p", image);
  if (image) {
    width = k4a_image_get_width_pixels(image);
    height = k4a_image_get_height_pixels(image);
    stride = k4a_image_get_stride_bytes(image);
    printf(" | Color res: %4ldx%4ld ", height, width);
    runEncoderThread = true;
  } else {
    printf(" | Color None                       ");
  }

  // release image & capture
  k4a_image_release(image);
  k4a_capture_release(capture);
  fflush(stdout);
}

void VideoCapture::captureFrame() {
  k4a_image_t image;
  k4a_capture_t capture = NULL;

  // Get a depth frame
  switch (k4a_device_get_capture(device, &capture, CAPTURE_TIMEOUT_IN_MS)) {
  case K4A_WAIT_RESULT_SUCCEEDED:
    break;
  case K4A_WAIT_RESULT_TIMEOUT:
    printf("Timed out waiting for a capture\n");
    return;
  case K4A_WAIT_RESULT_FAILED:
    printf("Failed to read a capture\n");
    return;
  }

  printf("Capture");

  // Probe for a color image
  image = k4a_capture_get_color_image(capture);
  if (image) {
    printf(" | Getting buffer\n");
    uint8_t *buffer = k4a_image_get_buffer(image);

    BOOST_LOG_TRIVIAL(info) << "Putting frame... ";
    if (buffer == NULL) {
      BOOST_LOG_TRIVIAL(info) << "Error buffer is null! ";
      // release image & capture
      k4a_image_release(image);
      k4a_capture_release(capture);
      fflush(stdout);
      return;
    }

    VideoFrame frame(buffer, AV_PIX_FMT_BGR24, width, height, stride);
    if (videoStream || videoStream->IsOpen() || videoStream.get() != NULL) {
      // currently segfaults
      videoStream->PutVideoFrame(frame);
    } else {
      BOOST_LOG_TRIVIAL(info) << "Video stream did not initialise properly... ";
    }
  } else {
    printf(" | Color None                       ");
  }

  // release image & capture
  k4a_image_release(image);
  k4a_capture_release(capture);
  fflush(stdout);
}
