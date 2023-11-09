#include "VideoCapture.hpp"
#include <cstddef>

VideoCapture::VideoCapture(PacketMuxer &sender)
    : device(NULL), runEncoderThread(true) {
  BOOST_LOG_TRIVIAL(info) << "Starting video capture.";
  configure();

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

  BOOST_LOG_TRIVIAL(info) << "Starting video encoding.";
  if (device != NULL) {
    k4a_image_t frame = captureFrame(CAPTURE_TIMEOUT_IN_MS);
    if (frame != NULL) {
      // infer width and height of video from capture
      width = k4a_image_get_width_pixels(frame);
      height = k4a_image_get_height_pixels(frame);
      k4a_image_release(frame);

      BOOST_LOG_TRIVIAL(info)
          << "Frame height and width: " << width << " " << height;
      initialiseVideoStream();
      startEncodeThread();
    } else {
      BOOST_LOG_TRIVIAL(info) << "Failed to capture frame.";
    }
  }
}

VideoCapture::~VideoCapture() {
  if (device != NULL) {
    k4a_device_close(device);
  }
  stopEncodeThread();
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
  config.color_format = K4A_IMAGE_FORMAT_COLOR_MJPG;
  config.color_resolution = K4A_COLOR_RESOLUTION_2160P;
  config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
  config.camera_fps = K4A_FRAMES_PER_SECOND_30;

  if (K4A_RESULT_SUCCEEDED != k4a_device_start_cameras(device, &config)) {
    BOOST_LOG_TRIVIAL(info) << "Failed to start device ";
    return;
  }
}

void VideoCapture::initialiseVideoStream() {
  if (videoStream) {
    videoStream->AddVideoStream(width, height, 30,
                                video::FourCc('F', 'M', 'P', '4'));
    BOOST_LOG_TRIVIAL(info) << "Succesfully initialised camera stream.";
  } else {
    BOOST_LOG_TRIVIAL(error) << "No object to add video stream to.";
  }
}

/// Send the preview image in a compressed video stream:
bool VideoCapture::sendPreviewImage(k4a_image_t &k4_image) {
  uint8_t *buffer = k4a_image_get_buffer(k4_image);
  auto stride = k4a_image_get_stride_bytes(k4_image);
  BOOST_LOG_TRIVIAL(info) << "Putting frame... ";
  VideoFrame frame(buffer, AV_PIX_FMT_BGR24, width, height, stride);
  if (videoStream || videoStream->IsOpen() || videoStream.get() != NULL) {
    // currently segfaults
    return videoStream->PutVideoFrame(frame);
  }
  BOOST_LOG_TRIVIAL(info) << "Video stream did not initialise properly... ";
  return false;
}

void VideoCapture::startEncodeThread() {
  // Thread just encodes video frames as fast as it can capture:
  videoEncodeThread.reset(new std::thread([&]() {
    BOOST_LOG_TRIVIAL(info) << "Video encode thread launched.";
    while (runEncoderThread) {
      encodeVideoFrame();
    }
  }));
}

void VideoCapture::stopEncodeThread() {
  runEncoderThread = false;
  if (videoEncodeThread) {
    try {
      videoEncodeThread->join();
      BOOST_LOG_TRIVIAL(debug) << "Video encode thread joined successfully.";
      videoEncodeThread.reset();
    } catch (std::system_error &e) {
      BOOST_LOG_TRIVIAL(warning) << "Video encode thread could not be joined.";
    }
  }
}

void VideoCapture::encodeVideoFrame() {
  // throttle camera rate
  std::this_thread::sleep_for(2ms);
  k4a_image_t frame = captureFrame(CAPTURE_TIMEOUT_IN_MS);
  bool ok;
  if (frame != NULL) {
    ok = sendPreviewImage(frame);
    BOOST_LOG_TRIVIAL(info) << "Sent frame: " << ok;
    k4a_image_release(frame);
  }
  if (!ok) {
    BOOST_LOG_TRIVIAL(error) << ("Could not send video frame.");
  }
}

k4a_image_t VideoCapture::captureFrame(const int32_t TIMEOUT_IN_MS) {
  k4a_image_t image;
  k4a_capture_t capture = NULL;

  if (device == NULL) {
    BOOST_LOG_TRIVIAL(error)
        << ("Camera failed to initialise, cannot capture frame.");
    return image;
  }

  // Get a depth frame
  switch (k4a_device_get_capture(device, &capture, TIMEOUT_IN_MS)) {
  case K4A_WAIT_RESULT_SUCCEEDED:
    break;
  case K4A_WAIT_RESULT_TIMEOUT:
    printf("Timed out waiting for a capture\n");
    return image;
  case K4A_WAIT_RESULT_FAILED:
    printf("Failed to read a capture\n");
    return image;
  }

  if (capture == NULL) {
    BOOST_LOG_TRIVIAL(info) << "CAPTURE IS NULL :( ";
  }

  printf("Capture");

  image = k4a_capture_get_color_image(capture);
  if (image) {
    printf(
        " | Color res:%4dx%4d stride:%5d ", k4a_image_get_height_pixels(image),
        k4a_image_get_width_pixels(image), k4a_image_get_stride_bytes(image));
    k4a_image_release(image);
  } else {
    printf(" | Color None                       ");
  }

  // probe for a IR16 image
  image = k4a_capture_get_ir_image(capture);
  if (image != NULL) {
    printf(
        " | Ir16 res:%4dx%4d stride:%5d ", k4a_image_get_height_pixels(image),
        k4a_image_get_width_pixels(image), k4a_image_get_stride_bytes(image));
    k4a_image_release(image);
  } else {
    printf(" | Ir16 None                       ");
  }

  // Probe for a depth16 image
  image = k4a_capture_get_depth_image(capture);
  if (image != NULL) {
    printf(" | Depth16 res:%4dx%4d stride:%5d\n",
           k4a_image_get_height_pixels(image),
           k4a_image_get_width_pixels(image),
           k4a_image_get_stride_bytes(image));
  } else {
    printf(" | Depth16 None\n");
  }

  // release capture
  k4a_capture_release(capture);
  fflush(stdout);
  return image;
}
