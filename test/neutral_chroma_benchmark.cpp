#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "wels/codec_api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Check (bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error (message);
}

class Library {
 public:
  explicit Library (const char* path) : module_ (LoadLibraryA (path)) {
    Check (module_ != NULL, "Could not load benchmark DLL.");
    createEncoder = reinterpret_cast<int (*) (ISVCEncoder**)> (GetProcAddress (module_, "WelsCreateSVCEncoder"));
    destroyEncoder = reinterpret_cast<void (*) (ISVCEncoder*)> (GetProcAddress (module_, "WelsDestroySVCEncoder"));
    createDecoder = reinterpret_cast<long (*) (ISVCDecoder**)> (GetProcAddress (module_, "WelsCreateDecoder"));
    destroyDecoder = reinterpret_cast<void (*) (ISVCDecoder*)> (GetProcAddress (module_, "WelsDestroyDecoder"));
    Check (createEncoder && destroyEncoder && createDecoder && destroyDecoder, "Missing OpenH264 exports.");
    std::fprintf (stderr, "Loaded %s\n", path);
  }
  ~Library() { FreeLibrary (module_); }
  Library (const Library&) = delete;
  Library& operator= (const Library&) = delete;

  int (*createEncoder) (ISVCEncoder**);
  void (*destroyEncoder) (ISVCEncoder*);
  long (*createDecoder) (ISVCDecoder**);
  void (*destroyDecoder) (ISVCDecoder*);

 private:
  HMODULE module_;
};

struct Result {
  std::vector<double> times;
  std::vector<double> cycles;
  size_t bytes = 0;
};

struct EncoderDeleter {
  Library* library;
  void operator() (ISVCEncoder* encoder) const {
    encoder->Uninitialize();
    library->destroyEncoder (encoder);
  }
};

struct DecoderDeleter {
  Library* library;
  void operator() (ISVCDecoder* decoder) const {
    decoder->Uninitialize();
    library->destroyDecoder (decoder);
  }
};

using EncoderPtr = std::unique_ptr<ISVCEncoder, EncoderDeleter>;
using DecoderPtr = std::unique_ptr<ISVCDecoder, DecoderDeleter>;

EncoderPtr CreateEncoder (Library& library, bool neutral, int width, int height) {
  ISVCEncoder* rawEncoder = NULL;
  Check (library.createEncoder (&rawEncoder) == 0 && rawEncoder, "Create encoder failed.");
  EncoderPtr encoder (rawEncoder, EncoderDeleter { &library });
  int quiet = WELS_LOG_QUIET;
  Check (encoder->SetOption (ENCODER_OPTION_TRACE_LEVEL, &quiet) == 0, "Set encoder tracing failed.");

  SEncParamExt param = {};
  Check (encoder->GetDefaultParams (&param) == 0, "GetDefaultParams failed.");
  param.iUsageType = CAMERA_VIDEO_REAL_TIME;
  param.iPicWidth = width;
  param.iPicHeight = height;
  param.iTargetBitrate = 1000000;
  param.iRCMode = RC_OFF_MODE;
  param.fMaxFrameRate = 30;
  param.iTemporalLayerNum = param.iSpatialLayerNum = 1;
  param.uiIntraPeriod = 60;
  param.iMultipleThreadIdc = 1;
  param.iComplexityMode = MEDIUM_COMPLEXITY;
  param.bEnableAdaptiveQuant = true;
  param.bEnableBackgroundDetection = true;
  param.bUseLoadBalancing = false;
  param.bEnableLongTermReference = false;
  param.bEnableFrameSkip = false;
  param.bEnableSceneChangeDetect = false;
  param.bEnableDenoise = false;
  param.iMinQp = param.iMaxQp = 32;
  SSpatialLayerConfig& layer = param.sSpatialLayers[0];
  layer.iVideoWidth = width;
  layer.iVideoHeight = height;
  layer.fFrameRate = 30;
  layer.iSpatialBitrate = layer.iMaxSpatialBitrate = 1000000;
  layer.iDLayerQp = 32;
  layer.uiProfileIdc = PRO_BASELINE;
  layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  Check (encoder->InitializeExt (&param) == 0, "InitializeExt failed.");
  int format = videoFormatI420;
  Check (encoder->SetOption (ENCODER_OPTION_DATAFORMAT, &format) == 0, "Set input format failed.");
  if (neutral) {
    int enabled = 1;
    Check (encoder->SetOption (ENCODER_OPTION_NEUTRAL_CHROMA, &enabled) == 0, "Neutral option rejected.");
  }
  SEncParamExt effective = {};
  Check (encoder->GetOption (ENCODER_OPTION_SVC_ENCODE_PARAM_EXT, &effective) == 0, "Get effective parameters failed.");
  std::fprintf (stderr, "Effective: RC=%d QP=%d..%d threads=%d complexity=%d AQ=%d BG=%d GOP=%u LTR=%d slice=%d\n",
                effective.iRCMode, effective.iMinQp, effective.iMaxQp, effective.iMultipleThreadIdc,
                effective.iComplexityMode, effective.bEnableAdaptiveQuant, effective.bEnableBackgroundDetection,
                effective.uiIntraPeriod, effective.bEnableLongTermReference,
                effective.sSpatialLayers[0].sSliceArgument.uiSliceMode);
  Check (effective.iRCMode == RC_OFF_MODE && effective.iMinQp == 32 && effective.iMaxQp == 32 &&
         effective.iMultipleThreadIdc == 1 && effective.iComplexityMode == MEDIUM_COMPLEXITY &&
         !effective.bEnableAdaptiveQuant && effective.bEnableBackgroundDetection &&
         effective.uiIntraPeriod == 60 && !effective.bEnableLongTermReference &&
         effective.sSpatialLayers[0].sSliceArgument.uiSliceMode == SM_SINGLE_SLICE,
         "Effective encoder configuration differs from the AlphaExamples defaults.");
  return encoder;
}

DecoderPtr CreateDecoder (Library& library) {
  ISVCDecoder* rawDecoder = NULL;
  Check (library.createDecoder (&rawDecoder) == 0 && rawDecoder, "Create stock decoder failed.");
  DecoderPtr decoder (rawDecoder, DecoderDeleter { &library });
  int quiet = WELS_LOG_QUIET;
  Check (decoder->SetOption (DECODER_OPTION_TRACE_LEVEL, &quiet) == 0, "Set decoder tracing failed.");
  SDecodingParam decoding = {};
  decoding.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  decoding.eEcActiveIdc = ERROR_CON_DISABLE;
  Check (decoder->Initialize (&decoding) == 0, "Stock decoder Initialize failed.");
  return decoder;
}

struct Variant {
  const char* name;
  Library* library;
  bool neutral;
};

struct Session {
  EncoderPtr encoder;
  DecoderPtr decoder;
  std::vector<unsigned char> packet;
  Result result;

  Session (const Variant& variant, Library& stock, int width, int height)
    : encoder (CreateEncoder (*variant.library, variant.neutral, width, height)),
      decoder (CreateDecoder (stock)) {}
};

std::vector<unsigned char> ReadAlpha (const char* path, size_t frameSize, int frames) {
  std::ifstream file (path, std::ios::binary | std::ios::ate);
  Check (file.is_open(), "Could not open raw R8 input.");
  const std::streamoff length = file.tellg();
  const size_t required = frameSize * frames;
  Check (length >= 0 && static_cast<unsigned long long> (length) >= required &&
         static_cast<unsigned long long> (length) % frameSize == 0,
         "Raw input is too short or contains an incomplete R8 frame.");
  std::vector<unsigned char> pixels (required);
  file.seekg (0);
  file.read (reinterpret_cast<char*> (pixels.data()), static_cast<std::streamsize> (required));
  Check (file.gcount() == static_cast<std::streamsize> (required), "Could not read all raw R8 frames.");
  return pixels;
}

std::vector<Result> Run (const std::vector<Variant>& variants, Library& stock,
                        int width, int height, int frames, int repeat,
                        const std::vector<unsigned char>& raw) {
  std::vector<Session> sessions;
  sessions.reserve (variants.size());
  for (const Variant& variant : variants)
    sessions.emplace_back (variant, stock, width, height);
  const size_t ySize = static_cast<size_t> (width) * height;
  std::vector<unsigned char> pixels (ySize);
  std::vector<unsigned char> neutral (ySize / 4, 128);
  SSourcePicture picture = {};
  picture.iColorFormat = videoFormatI420;
  picture.iPicWidth = width;
  picture.iPicHeight = height;
  picture.iStride[0] = width;
  picture.iStride[1] = picture.iStride[2] = width / 2;
  picture.pData[1] = picture.pData[2] = neutral.data();
  std::vector<SFrameBSInfo> infos (variants.size());
  std::vector<std::array<unsigned char*, 3> > output (variants.size());
  std::vector<SBufferInfo> buffers (variants.size());
  for (int frame = 0; frame < frames; ++frame) {
    if (raw.empty()) {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const int movingX = (x + frame * 7) % width;
          pixels[static_cast<size_t> (y) * width + x] =
            movingX < width / 3 ? 0 : movingX > width * 2 / 3 ? 255 :
            static_cast<unsigned char> (((movingX * 255 / width) + y / 8) & 255);
        }
      }
      picture.pData[0] = pixels.data();
    } else {
      picture.pData[0] = const_cast<unsigned char*> (raw.data() + frame * ySize);
    }
    picture.uiTimeStamp = frame * 33;
    // Rotate each frame's order so frequency, cache warmth and scheduling do not always favor one variant.
    for (size_t position = 0; position < variants.size(); ++position) {
      const size_t index = (frame + repeat + position) % variants.size();
      Session& session = sessions[index];
      SFrameBSInfo& info = infos[index];
      info = {};
      ULONG64 cyclesBefore = 0, cyclesAfter = 0;
      Check (QueryThreadCycleTime (GetCurrentThread(), &cyclesBefore) != 0, "Could not query encoder thread cycles.");
      const auto start = std::chrono::steady_clock::now();
      const int status = session.encoder->EncodeFrame (&picture, &info);
      const auto end = std::chrono::steady_clock::now();
      Check (QueryThreadCycleTime (GetCurrentThread(), &cyclesAfter) != 0, "Could not query encoder thread cycles.");
      Check (status == 0 && info.eFrameType != videoFrameTypeSkip && info.iLayerNum > 0, "Encode failed or skipped.");
      if (frame >= 5) {
        session.result.times.push_back (std::chrono::duration<double, std::micro> (end - start).count());
        session.result.cycles.push_back (static_cast<double> (cyclesAfter - cyclesBefore));
      }
    }
    for (size_t index = 0; index < variants.size(); ++index) {
      Session& session = sessions[index];
      const SFrameBSInfo& info = infos[index];
      session.packet.clear();
      for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo& bs = info.sLayerInfo[layer];
        int length = 0;
        for (int nal = 0; nal < bs.iNalCount; ++nal)
          length += bs.pNalLengthInByte[nal];
        session.packet.insert (session.packet.end(), bs.pBsBuf, bs.pBsBuf + length);
      }
      Check (!session.packet.empty(), "Empty packet.");
      session.result.bytes += session.packet.size();
      output[index] = {};
      buffers[index] = {};
      Check (session.decoder->DecodeFrameNoDelay (session.packet.data(), static_cast<int> (session.packet.size()),
             output[index].data(), &buffers[index]) == dsErrorFree && buffers[index].iBufferStatus == 1,
             "Stock decoder rejected stream.");
      const SSysMEMBuffer& sys = buffers[index].UsrData.sSystemBuffer;
      Check (sys.iWidth == width && sys.iHeight == height, "Unexpected decoded dimensions.");
      for (int plane = 1; plane < 3; ++plane)
        for (int y = 0; y < height / 2; ++y)
          Check (std::memcmp (output[index][plane] + y * sys.iStride[1], neutral.data(), width / 2) == 0,
                 "Non-neutral reconstructed chroma.");
      if (index > 0) {
        const SSysMEMBuffer& reference = buffers[0].UsrData.sSystemBuffer;
        for (int y = 0; y < height; ++y)
          Check (std::memcmp (output[0][0] + y * reference.iStride[0],
                             output[index][0] + y * sys.iStride[0], width) == 0,
                 "Decoded alpha differs across variants.");
        Check (sessions[0].packet.size() == session.packet.size(), "Per-frame encoded sizes differ.");
      }
    }
  }
  std::vector<Result> results;
  for (Session& session : sessions)
    results.push_back (std::move (session.result));
  return results;
}

void Print (const char* name, int repeat, const Result& result, bool raw) {
  std::vector<double> sorted = result.times;
  std::sort (sorted.begin(), sorted.end());
  double sum = 0;
  for (double time : sorted)
    sum += time;
  if (raw) {
    const double cycles = std::accumulate (result.cycles.begin(), result.cycles.end(), 0.0) / result.cycles.size();
    std::printf ("%s,%d,%zu,%.3f,%.3f,%.3f,%.3f,%.0f,%zu\n", name, repeat, sorted.size(), sum / sorted.size(),
                 sorted[sorted.size() / 2], sorted[sorted.size() * 95 / 100],
                 sorted[sorted.size() * 99 / 100], cycles, result.bytes);
  } else {
    std::printf ("%s,%d,%.3f,%.3f,%zu\n", name, repeat, sum / sorted.size(),
                 sorted[sorted.size() * 95 / 100], result.bytes);
  }
}

} // namespace

int main (int argc, char** argv) {
  try {
    Check (argc >= 7 && argc <= 9,
           "Usage: neutral_chroma_benchmark baseline.dll alpha.dll stock.dll width height frames [raw-r8-file [repeats]]");
    const int width = std::stoi (argv[4]);
    const int height = std::stoi (argv[5]);
    const int frames = std::stoi (argv[6]);
    Check (width >= 16 && height >= 16 && width <= 4096 && height <= 2160 &&
           !(width & 1) && !(height & 1) && frames > 5 && frames <= 300, "Invalid benchmark dimensions/frame count.");
    const bool rawInput = argc >= 8;
    const int repeats = argc == 9 ? std::stoi (argv[8]) : rawInput ? 4 : 3;
    Check (repeats >= 1 && repeats <= 20, "Invalid repetition count.");
    const std::vector<unsigned char> raw = rawInput ?
      ReadAlpha (argv[7], static_cast<size_t> (width) * height, frames) : std::vector<unsigned char>();
    if (rawInput) {
      DWORD_PTR processMask = 0, systemMask = 0;
      Check (GetProcessAffinityMask (GetCurrentProcess(), &processMask, &systemMask) != 0 && processMask != 0,
             "Could not query benchmark CPU affinity.");
      // Keep comparisons on one allowed logical CPU to avoid heterogeneous-core migrations.
      const DWORD_PTR mask = processMask & (~processMask + 1);
      Check (SetThreadAffinityMask (GetCurrentThread(), mask) != 0, "Could not pin the benchmark thread.");
      std::fprintf (stderr, "Benchmark thread affinity mask=0x%llx\n", static_cast<unsigned long long> (mask));
    }
    Library baseline (argv[1]), alpha (argv[2]), stock (argv[3]);
    std::vector<Variant> variants;
    if (rawInput)
      variants.push_back (Variant { "Stock", &stock, false });
    variants.push_back (Variant { "Source", &baseline, false });
    variants.push_back (Variant { "ExperimentalOff", &alpha, false });
    variants.push_back (Variant { "Luma", &alpha, true });
    std::fprintf (stderr, "Input=%s %dx%d frames=%d repeats=%d warmup=5; QP32 GOP60 medium AQ requested=1 BG=1 single-thread/slice.\n",
                  rawInput ? argv[7] : "synthetic", width, height, frames, repeats);
    std::puts (rawInput ?
      "Variant,Repeat,MeasuredFrames,EncodeMeanUs,EncodeP50Us,EncodeP95Us,EncodeP99Us,EncodeThreadCyclesMean,EncodedBytes" :
      "Variant,Repeat,EncodeMeanUs,EncodeP95Us,EncodedBytes");
    for (int repeat = 0; repeat < repeats; ++repeat) {
      const std::vector<Result> results = Run (variants, stock, width, height, frames, repeat, raw);
      for (size_t index = 0; index < variants.size(); ++index)
        Print (variants[index].name, repeat, results[index], rawInput);
    }
    std::fprintf (stderr, "PASS: exact decoded alpha and neutral U/V through stock decoder in all runs.\n");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf (stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
