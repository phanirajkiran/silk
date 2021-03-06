
#include <fcntl.h>
#include <time.h>

#include <vector>
#include <memory>

#include <binder/ProcessState.h>
#include <cutils/properties.h>
#include <utils/SystemClock.h>

#include "libpreview.h"
#include "SimpleH264Encoder.h"
#include "SharedSimpleH264Encoder.h"

using android::Mutex;

libpreview::Client *libpreviewClient;

SimpleH264Encoder *simpleH264Encoder;
std::vector<std::shared_ptr<SimpleH264Encoder>> moreEncoders;

Mutex simpleH264EncoderLock;
int fd;
int frameCount = 0;
int dropCount = 0;

void libpreview_FrameCallback(void *userData,
                              void *frame,
                              libpreview::FrameFormat format,
                              size_t width,
                              size_t height,
                              libpreview::FrameOwner owner)
{
  (void) userData;
  Mutex::Autolock autolock(simpleH264EncoderLock);

  frameCount++;
  bool dropFrame = false;
  if (dropCount > 0) {
    dropFrame = true;
    dropCount--;
    printf("Dropped frame #%d\n", frameCount);
  }

  if (simpleH264Encoder == nullptr || dropFrame) {
    libpreviewClient->releaseFrame(owner);
    return;
  }
  printf("Encode frame #%d\n", frameCount);

  SimpleH264Encoder::InputFrame inputFrame;
  SimpleH264Encoder::InputFrameInfo inputFrameInfo;
#ifdef ANDROID
  inputFrameInfo.captureTimeMs = android::elapsedRealtime();
#else
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  inputFrameInfo.captureTimeMs = (int64_t)now.tv_sec * 1e3 + (int64_t)now.tv_nsec / 1e6;
#endif

  if (!simpleH264Encoder->getInputFrame(inputFrame)) {
    printf("Unable to get input frame\n");
    return;
  }

  switch (format) {
  case libpreview::FRAMEFORMAT_YVU420SP:
    if (inputFrame.format != libpreview::FRAMEFORMAT_YVU420SP) {
      printf("Unsupported encoder format: %d\n", inputFrame.format);
      return;
    }

    // Copy Y plane
    memcpy(inputFrame.data, frame, width * height);

    // Copy UV plane while converting from YVU420SemiPlaner to YUV420SemiPlaner
    {
      uint8_t *s = static_cast<uint8_t *>(frame) + width * height;
      uint8_t *d = static_cast<uint8_t *>(inputFrame.data) + width * height;
      uint8_t *dEnd = d + width * height / 2;
      for (; d < dEnd; s += 2, d += 2) {
        d[0] = s[1];
        d[1] = s[0];
      }
    }
    break;
  case libpreview::FRAMEFORMAT_YUV420SP:
    if (inputFrame.format != format) {
      printf("Unsupported encoder format: %d\n", inputFrame.format);
      return;
    }
    memcpy(inputFrame.data, frame, inputFrame.size);
    break;

  case libpreview::FRAMEFORMAT_YUV420SP_VENUS:
    if (inputFrame.format != format) {
      printf("Unsupported encoder format: %d\n", inputFrame.format);
      return;
    }
    switch (inputFrame.format) {
    case libpreview::FRAMEFORMAT_YUV420SP_VENUS:
      memcpy(inputFrame.data, frame, inputFrame.size);
      break;
    case libpreview::FRAMEFORMAT_YUV420SP:
      // Copy Y plane
      memcpy(inputFrame.data, frame, width * height);

      // Pack and copy UV plane
      memcpy(
        static_cast<uint8_t *>(inputFrame.data) + width * height,
        static_cast<uint8_t *>(frame) + libpreview::VENUS_C_PLANE_OFFSET(width, height),
        width * height / 2
      );
      break;
    default:
      break;
    }
    break;

  default:
    printf("Unsupported format: %d\n", format);
    return;
  }

  simpleH264Encoder->nextFrame(inputFrame, inputFrameInfo);
  libpreviewClient->releaseFrame(owner);

}

void libpreview_AbandonedCallback(void *userData)
{
  (void) userData;
  printf("libpreview_AbandonedCallback\n");
  exit(1);
}

void frameOutCallback(SimpleH264Encoder::EncodedFrameInfo& info) {
  printf("Frame %lld size=%8d bits, keyframe=%d\n",
    info.input.captureTimeMs, info.encodedFrameLength, info.keyFrame);
  if (!info.userData) {
    TEMP_FAILURE_RETRY(
      write(
        fd,
        info.encodedFrame,
        info.encodedFrameLength
      )
    );
  }
}

int main(int argc, char **argv)
{
  (void) argc;
  (void) argv;
#ifdef ANDROID
  android::sp<android::ProcessState> ps = android::ProcessState::self();
  ps->startThreadPool();
#endif

  printf("Opening libpreview...\n");
  libpreviewClient = libpreview::open(libpreview_FrameCallback, libpreview_AbandonedCallback, 0);
  if (!libpreviewClient) {
    printf("Unable to open libpreview\n");
    return 1;
  }

  int width = property_get_int32("ro.silk.camera.width", 1280);
  int height = property_get_int32("ro.silk.camera.height", 720);
  int vbr = property_get_int32("ro.silk.camera.vbr", 1024);
  int fps = property_get_int32("ro.silk.camera.fps", 24);

  for (int i = 0; i < 1; i++) {
    char filename[32];
    snprintf(filename, sizeof(filename) - 1, "/data/vid_%d.h264", i);
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0440);
    if (fd < 0) {
      printf("Unable to open output file: %s\n", filename);
    }

    printf("Output file: %s\n", filename);
    {
      Mutex::Autolock autolock(simpleH264EncoderLock);
      simpleH264Encoder = SharedSimpleH264Encoder::Create(
        width, height, vbr, fps,
        frameOutCallback,
        nullptr
      );

      for (auto i = 0; i < 5; i++) {
        moreEncoders.push_back(
          std::shared_ptr<SimpleH264Encoder>(
            SharedSimpleH264Encoder::Create(
              width, height, vbr, fps,
              frameOutCallback,
              (void *) 0xDEADBEEF
            )
          )
        );
      }
    }
    printf("Encoder started\n");
    if (simpleH264Encoder == nullptr) {
      printf("Unable to create a SharedSimpleH264Encoder\n");
      return 1;
    }

    printf("Waiting for frames to start...\n");
    while (frameCount < 2) {
      usleep(200 * 1000);
    }
    printf("Started, getting some sleep..\n");
    sleep(5);

    // Fiddle with the bitrate while recording just because we can
    for (int j = 0; j < 10; j++) {
      int bitRateK = 1000 * (j+1) / 10;
      simpleH264Encoder->setBitRate(bitRateK);
      printf(". (bitrate=%dk)\n", bitRateK);
      sleep(1);
    }

    moreEncoders.clear();
    simpleH264Encoder->stop();
    {
      Mutex::Autolock autolock(simpleH264EncoderLock);
      delete simpleH264Encoder;
      simpleH264Encoder = nullptr;
    }
    close(fd);
    printf("Encoder stopped\n");
    sleep(1); // Take a breath...
  }

  printf("Releasing libpreview\n");
  libpreviewClient->release();

  return 0;
}
