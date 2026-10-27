#include <gtest/gtest.h>
#include "utils/HashFunctions.h"
#include "BaseEncoderTest.h"
#include <string>
#include <vector>

static void UpdateHashFromFrame (const SFrameBSInfo& info, SHA1Context* ctx) {
  for (int i = 0; i < info.iLayerNum; ++i) {
    const SLayerBSInfo& layerInfo = info.sLayerInfo[i];
    int layerSize = 0;
    for (int j = 0; j < layerInfo.iNalCount; ++j) {
      layerSize += layerInfo.pNalLengthInByte[j];
    }
    SHA1Input (ctx, layerInfo.pBsBuf, layerSize);
  }
}

class EncoderInitTest : public ::testing::Test, public BaseEncoderTest {
 public:
  virtual void SetUp() {
    BaseEncoderTest::SetUp();
  }
  virtual void TearDown() {
    BaseEncoderTest::TearDown();
  }
};

TEST_F (EncoderInitTest, JustInit) {}

struct EncodeFileParam {
  const char* pkcFileName;
  const char* pkcHashStr[2];
  EUsageType eUsageType;
  int iWidth;
  int iHeight;
  float fFrameRate;
  SliceModeEnum eSliceMode;
  bool bDenoise;
  int  iLayerNum;
  bool bLossless;
  bool bEnableLtr;
  bool bCabac;
// unsigned short iMultipleThreadIdc;
};

void EncFileParamToParamExt (EncodeFileParam* pEncFileParam, SEncParamExt* pEnxParamExt) {
  ASSERT_TRUE (NULL != pEncFileParam && NULL != pEnxParamExt);
  pEnxParamExt->iUsageType       = pEncFileParam->eUsageType;
  pEnxParamExt->iPicWidth        = pEncFileParam->iWidth;
  pEnxParamExt->iPicHeight       = pEncFileParam->iHeight;
  pEnxParamExt->fMaxFrameRate    = pEncFileParam->fFrameRate;
  pEnxParamExt->iSpatialLayerNum = pEncFileParam->iLayerNum;

  pEnxParamExt->bEnableDenoise   = pEncFileParam->bDenoise;
  pEnxParamExt->bIsLosslessLink  = pEncFileParam->bLossless;
  pEnxParamExt->bEnableLongTermReference = pEncFileParam->bEnableLtr;
  pEnxParamExt->iEntropyCodingModeFlag   = pEncFileParam->bCabac ? 1 : 0;

  for (int i = 0; i < pEnxParamExt->iSpatialLayerNum; i++) {
    pEnxParamExt->sSpatialLayers[i].sSliceArgument.uiSliceMode = pEncFileParam->eSliceMode;
  }

}

class EncoderOutputTest : public ::testing::WithParamInterface<EncodeFileParam>,
  public EncoderInitTest , public BaseEncoderTest::Callback {
 public:
  virtual void SetUp() {
    EncoderInitTest::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    SHA1Reset (&ctx_);
  }
  virtual void onEncodeFrame (const SFrameBSInfo& frameInfo) {
    UpdateHashFromFrame (frameInfo, &ctx_);
  }

 protected:
  SHA1Context ctx_;
};


TEST_P (EncoderOutputTest, CompareOutput) {
  EncodeFileParam p = GetParam();
  SEncParamExt EnxParamExt;

  EncFileParamToParamExt (&p, &EnxParamExt);

#if defined(ANDROID_NDK)
  std::string filename = std::string ("/sdcard/") + p.pkcFileName;
  EncodeFile (p.pkcFileName, &EnxParamExt, this);
#else
  EncodeFile (p.pkcFileName, &EnxParamExt, this);
#endif
  //will remove this after screen content algorithms are ready,
  //because the bitstream output will vary when the different algorithms are added.
  unsigned char digest[SHA_DIGEST_LENGTH];
  SHA1Result (&ctx_, digest);
  if (!HasFatalFailure()) {
    CompareHashAnyOf (digest, p.pkcHashStr, sizeof p.pkcHashStr / sizeof *p.pkcHashStr);
  }
}
static const EncodeFileParam kFileParamArray[] = {
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    {"19dd24f5a3ae9e9582024db5b63696948b8ac82f"}, CAMERA_VIDEO_REAL_TIME, 320, 192, 12.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/CiscoVT2people_160x96_6fps.yuv",
    {"7acf58ba0e72ffc4725ad897ed45f990b9ab8413"}, CAMERA_VIDEO_REAL_TIME, 160, 96, 6.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/Static_152_100.yuv",
    {"d27c6a56efe899528ddd3f8f3b33efb8f3ce2d51"}, CAMERA_VIDEO_REAL_TIME, 152, 100, 6.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    {"5111d90ce90849020b7ed830cc2f962ccfea4457"}, CAMERA_VIDEO_REAL_TIME, 320, 192, 12.0f, SM_RASTER_SLICE, false, 1, false, false, false // One slice per MB row
  },
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    {"91ca058f728a001546ae34ce1b7a00163800faac"}, CAMERA_VIDEO_REAL_TIME, 320, 192, 12.0f, SM_SINGLE_SLICE, true, 1, false, false, false
  },
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    // Allow for different output depending on whether averaging is done
    // vertically or horizontally first when downsampling.
    { "4faa2a17c6629135af43c2392531f4ce954ec92d", "a4c2caccada00189c683cd61c6f0bb995d09aeff" },
    CAMERA_VIDEO_REAL_TIME, 320, 192, 12.0f, SM_SINGLE_SLICE, false, 2, false, false, false
  },
  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    {"abd7aa034f916659a9f68b33160b404fb43411dd"}, CAMERA_VIDEO_REAL_TIME, 1280, 720, 30.0f, SM_SIZELIMITED_SLICE, false, 1, false, false, false
  },
  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    // Allow for different output depending on whether averaging is done
    // vertically or horizontally first when downsampling.
    { "291fba557fdfd0e077ff6681425dc81c65704b62", "9e4fb2b503a877c21441545ee4f0cd5416dbe5e9" },
    CAMERA_VIDEO_REAL_TIME, 1280, 720, 30.0f, SM_SINGLE_SLICE, false, 4, false, false, false
  },

  // the following values may be adjusted for times since we start tuning the strategy
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    {"dd51c2007f944ee752532c74c4d38da1413acd88"}, SCREEN_CONTENT_REAL_TIME, 320, 192, 12.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/CiscoVT2people_160x96_6fps.yuv",
    {"564b0e774f0157c91c7ff3548708f88fb0f3abc6"}, SCREEN_CONTENT_REAL_TIME, 160, 96, 6.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/Static_152_100.yuv",
    {"9b88952f0ec5ebf3e25e642b8a9f355854681f57"}, SCREEN_CONTENT_REAL_TIME, 152, 100, 6.0f, SM_SINGLE_SLICE, false, 1, false, false, false
  },
  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    {"7db72f0d6f347f57d5137a5cd4de2a3e3fba5b95"}, SCREEN_CONTENT_REAL_TIME, 1280, 720, 30.0f, SM_SIZELIMITED_SLICE, false, 1, false, false, false
  },
  //for different strategy
  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    {"30b877303081e2c5ee579d8bbeb2094abd2018c1"}, SCREEN_CONTENT_REAL_TIME, 1280, 720, 30.0f, SM_SIZELIMITED_SLICE, false, 1, true, true, false
  },
  {
    "res/CiscoVT2people_320x192_12fps.yuv",
    {"3c27f9134d79edaa769741005b569565c2367e6f"}, CAMERA_VIDEO_REAL_TIME, 320, 192, 12.0f, SM_SINGLE_SLICE, false, 1, false, false, true //turn on cabac
  },

  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    {"e5a91447595a2a3560f7cdabbb62b694134fe4ca"}, CAMERA_VIDEO_REAL_TIME, 1280, 720, 30.0f, SM_SIZELIMITED_SLICE, false, 1, false, false, true
  },

  {
    "res/Cisco_Absolute_Power_1280x720_30fps.yuv",
    {"77aa8bc391a84d308a09d4fbb859f4a0e8ae8196"}, CAMERA_VIDEO_REAL_TIME, 1280, 720, 30.0f, SM_FIXEDSLCNUM_SLICE, false, 1, false, false, true
  },

};

INSTANTIATE_TEST_CASE_P (EncodeFile, EncoderOutputTest,
                         ::testing::ValuesIn (kFileParamArray));

struct NeutralChromaParam {
  int width;
  int height;
  unsigned short threads;
  EUsageType usage;
  bool bitrate;
  bool ltr;
};

class NeutralChromaTest : public ::testing::TestWithParam<NeutralChromaParam> {
 protected:
  ISVCEncoder* encoders_[2];
  ISVCDecoder* decoders_[2];

  virtual void SetUp() {
    memset (encoders_, 0, sizeof (encoders_));
    memset (decoders_, 0, sizeof (decoders_));
    for (int i = 0; i < 2; ++i) {
      ASSERT_EQ (0, WelsCreateSVCEncoder (&encoders_[i]));
      ASSERT_EQ (0, WelsCreateDecoder (&decoders_[i]));
      SDecodingParam decoding = {};
      decoding.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
      decoding.eEcActiveIdc = ERROR_CON_DISABLE;
      ASSERT_EQ (0, decoders_[i]->Initialize (&decoding));
    }
  }

  virtual void TearDown() {
    for (int i = 0; i < 2; ++i) {
      if (encoders_[i]) {
        encoders_[i]->Uninitialize();
        WelsDestroySVCEncoder (encoders_[i]);
      }
      if (decoders_[i]) {
        decoders_[i]->Uninitialize();
        WelsDestroyDecoder (decoders_[i]);
      }
    }
  }

  void InitializeEncoders (int mode0 = 0, int mode1 = 1) {
    const NeutralChromaParam p = GetParam();
    for (int i = 0; i < 2; ++i) {
      SEncParamExt param = {};
      ASSERT_EQ (0, encoders_[i]->GetDefaultParams (&param));
      param.iUsageType = p.usage;
      param.iPicWidth = p.width;
      param.iPicHeight = p.height;
      param.iTargetBitrate = 500000;
      param.iRCMode = p.bitrate ? RC_BITRATE_MODE : RC_OFF_MODE;
      param.fMaxFrameRate = 30;
      param.iTemporalLayerNum = 1;
      param.iSpatialLayerNum = 1;
      param.uiIntraPeriod = 8;
      param.iMultipleThreadIdc = p.threads;
      param.bUseLoadBalancing = false; // Timing-dependent slice boundaries prevent exact A/B comparisons.
      param.bEnableFrameSkip = false;
      param.bEnableSceneChangeDetect = false;
      param.bEnableDenoise = false;
      param.bEnableLongTermReference = p.ltr;
      param.iLTRRefNum = p.ltr ? 2 : 0;
      param.iLtrMarkPeriod = 4;
      SSpatialLayerConfig& layer = param.sSpatialLayers[0];
      layer.iVideoWidth = p.width;
      layer.iVideoHeight = p.height;
      layer.fFrameRate = 30;
      layer.iSpatialBitrate = 500000;
      layer.iMaxSpatialBitrate = 500000;
      layer.iDLayerQp = 32;
      layer.uiProfileIdc = PRO_BASELINE;
      layer.sSliceArgument.uiSliceMode = p.threads == 1 ? SM_SINGLE_SLICE : SM_FIXEDSLCNUM_SLICE;
      layer.sSliceArgument.uiSliceNum = p.threads;
      ASSERT_EQ (0, encoders_[i]->InitializeExt (&param));
    }
    const int modes[2] = { mode0, mode1 };
    for (int i = 0; i < 2; ++i) {
      int mode = modes[i];
      if (mode)
        ASSERT_EQ (0, encoders_[i]->SetOption (ENCODER_OPTION_NEUTRAL_CHROMA, &mode));
    }
  }
};

TEST_P (NeutralChromaTest, PreservesDecodedAlphaAndNeutralReferences) {
  InitializeEncoders();
  ASSERT_FALSE (HasFatalFailure());
  const NeutralChromaParam p = GetParam();
  const int ySize = p.width * p.height;
  std::vector<unsigned char> pixels (ySize * 3 / 2, 128);
  SSourcePicture picture = {};
  picture.iColorFormat = videoFormatI420;
  picture.iPicWidth = p.width;
  picture.iPicHeight = p.height;
  picture.iStride[0] = p.width;
  picture.iStride[1] = picture.iStride[2] = p.width / 2;
  picture.pData[0] = &pixels[0];
  picture.pData[1] = &pixels[ySize];
  picture.pData[2] = &pixels[ySize + ySize / 4];

  for (int frame = 0; frame < 24; ++frame) {
    SCOPED_TRACE (frame);
    for (int y = 0; y < p.height; ++y) {
      for (int x = 0; x < p.width; ++x) {
        const int pattern = (frame / 2) % 6;
        pixels[y * p.width + x] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                                 pattern == 2 ? (x * 255 / p.width) :
                                 pattern == 3 ? (((x / 4 + y / 4) & 1) * 255) :
                                 pattern == 4 ? ((x + frame * 3) % p.width < p.width / 2 ? 255 : 0) :
                                 ((x * 37 + y * 13 + frame * 17) & 255);
      }
    }
    picture.uiTimeStamp = frame * 33;
    unsigned char* decoded[2][3] = {};
    SBufferInfo buffers[2] = {};
    for (int i = 0; i < 2; ++i) {
      if (frame == 13)
        ASSERT_EQ (0, encoders_[i]->ForceIntraFrame (true));
      SFrameBSInfo info = {};
      ASSERT_EQ (0, encoders_[i]->EncodeFrame (&picture, &info));
      ASSERT_NE (videoFrameTypeSkip, info.eFrameType);
      std::vector<unsigned char> packet;
      for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo& bs = info.sLayerInfo[layer];
        int length = 0;
        for (int nal = 0; nal < bs.iNalCount; ++nal)
          length += bs.pNalLengthInByte[nal];
        packet.insert (packet.end(), bs.pBsBuf, bs.pBsBuf + length);
      }
      ASSERT_FALSE (packet.empty());
      ASSERT_EQ (dsErrorFree, decoders_[i]->DecodeFrameNoDelay (&packet[0], static_cast<int> (packet.size()),
                 decoded[i], &buffers[i]));
      ASSERT_EQ (1, buffers[i].iBufferStatus);
      ASSERT_EQ (p.width, buffers[i].UsrData.sSystemBuffer.iWidth);
      ASSERT_EQ (p.height, buffers[i].UsrData.sSystemBuffer.iHeight);
    }
    for (int y = 0; y < p.height; ++y) {
      ASSERT_EQ (0, memcmp (decoded[0][0] + y * buffers[0].UsrData.sSystemBuffer.iStride[0],
                           decoded[1][0] + y * buffers[1].UsrData.sSystemBuffer.iStride[0], p.width));
    }
    for (int i = 0; i < 2; ++i) {
      for (int plane = 1; plane < 3; ++plane) {
        for (int y = 0; y < p.height / 2; ++y) {
          for (int x = 0; x < p.width / 2; ++x) {
            ASSERT_EQ (128, decoded[i][plane][y * buffers[i].UsrData.sSystemBuffer.iStride[1] + x]);
          }
        }
      }
    }
  }
}

TEST_P (NeutralChromaTest, RejectsModeChangesWithExistingReferencesAndUnsafeReconfiguration) {
  InitializeEncoders();
  ASSERT_FALSE (HasFatalFailure());
  const NeutralChromaParam p = GetParam();
  std::vector<unsigned char> pixels (p.width * p.height * 3 / 2, 128);
  SSourcePicture picture = {};
  picture.iColorFormat = videoFormatI420;
  picture.iPicWidth = p.width;
  picture.iPicHeight = p.height;
  picture.iStride[0] = p.width;
  picture.iStride[1] = picture.iStride[2] = p.width / 2;
  picture.pData[0] = &pixels[0];
  picture.pData[1] = &pixels[p.width * p.height];
  picture.pData[2] = picture.pData[1] + p.width * p.height / 4;
  for (int i = 0; i < 2; ++i) {
    SFrameBSInfo info = {};
    ASSERT_EQ (0, encoders_[i]->EncodeFrame (&picture, &info));
    int changed = i == 0 ? 1 : 0;
    EXPECT_EQ (cmInitParaError, encoders_[i]->SetOption (ENCODER_OPTION_NEUTRAL_CHROMA, &changed));
  }
  SEncParamExt param = {};
  ASSERT_EQ (0, encoders_[1]->GetOption (ENCODER_OPTION_SVC_ENCODE_PARAM_EXT, &param));
  EXPECT_EQ (cmUnsupportedData, encoders_[1]->SetOption (ENCODER_OPTION_SVC_ENCODE_PARAM_EXT, &param));
  picture.pData[1] = NULL;
  SFrameBSInfo info = {};
  EXPECT_EQ (cmInitParaError, encoders_[1]->EncodeFrame (&picture, &info));
  ASSERT_EQ (0, encoders_[1]->Uninitialize());
  ASSERT_EQ (0, encoders_[1]->InitializeExt (&param));
  int enabled = -1;
  ASSERT_EQ (0, encoders_[1]->GetOption (ENCODER_OPTION_NEUTRAL_CHROMA, &enabled));
  EXPECT_EQ (0, enabled);
  enabled = 2;
  EXPECT_EQ (cmInitParaError, encoders_[1]->SetOption (ENCODER_OPTION_NEUTRAL_CHROMA, &enabled));
}

static const NeutralChromaParam kNeutralChromaParams[] = {
  { 16, 16, 1, CAMERA_VIDEO_REAL_TIME, false, false },
  { 34, 50, 1, CAMERA_VIDEO_REAL_TIME, false, false },
  { 128, 96, 1, CAMERA_VIDEO_REAL_TIME, false, true },
  { 320, 180, 2, CAMERA_VIDEO_REAL_TIME, false, false },
  { 320, 180, 1, CAMERA_VIDEO_REAL_TIME, true, true },
  { 128, 96, 1, SCREEN_CONTENT_REAL_TIME, false, false },
  { 320, 180, 2, SCREEN_CONTENT_REAL_TIME, true, false }
};

INSTANTIATE_TEST_CASE_P (Alpha, NeutralChromaTest, ::testing::ValuesIn (kNeutralChromaParams));
