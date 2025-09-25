/* Copyright 2023 The MediaPipe Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "mediapipe/framework/deps/file_path.h"
#include "mediapipe/framework/formats/classification.pb.h"
#include "mediapipe/framework/formats/landmark.pb.h"
#include "mediapipe/framework/port/file_helpers.h"
#include "mediapipe/framework/port/gmock.h"
#include "mediapipe/framework/port/gtest.h"
#include "mediapipe/framework/port/parse_text_proto.h"
#include "mediapipe/framework/port/status_matchers.h"
#include "mediapipe/tasks/c/components/containers/landmark.h"
#include "mediapipe/tasks/c/core/mp_status.h"
#include "mediapipe/tasks/c/vision/core/common.h"
#include "mediapipe/tasks/c/vision/core/image.h"
#include "mediapipe/tasks/c/vision/core/image_processing_options.h"
#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker_result.h"
#include "mediapipe/tasks/cc/components/containers/proto/landmarks_detection_result.pb.h"
#include "mediapipe/tasks/cc/vision/utils/image_utils.h"

namespace {

using ::mediapipe::file::GetContents;
using ::mediapipe::file::JoinPath;
using ::mediapipe::tasks::containers::proto::LandmarksDetectionResult;
using ::mediapipe::tasks::vision::DecodeImageFromFile;
using testing::HasSubstr;

constexpr char kTestDataDirectory[] = "/mediapipe/tasks/testdata/vision/";
constexpr char kModelName[] = "hand_landmarker.task";
constexpr char kPointingUpImage[] = "pointing_up.jpg";
constexpr char kPointingUpRotatedImage[] = "pointing_up_rotated.jpg";
constexpr char kPointingUpLandmarksFilename[] = "pointing_up_landmarks.pbtxt";
constexpr char kPointingUpRotatedLandmarksFilename[] =
    "pointing_up_rotated_landmarks.pbtxt";
constexpr float kScorePrecision = 1e-2;
constexpr float kLandmarkPrecision = 1e-1;
constexpr int kIterations = 100;

std::string GetFullPath(absl::string_view file_name) {
  return JoinPath("./", kTestDataDirectory, file_name);
}

LandmarksDetectionResult GetLandmarksDetectionResult(
    absl::string_view landmarks_file_name) {
  LandmarksDetectionResult result;
  std::string contents;
  MP_EXPECT_OK(GetContents(
      JoinPath("./", kTestDataDirectory, landmarks_file_name), &contents));
  EXPECT_TRUE(mediapipe::ParseTextProto(contents, &result))
      << "Failed to parse landmarks from " << landmarks_file_name;
  return result;
}

void ExpectHandLandmarkerResultsCorrect(
    const HandLandmarkerResult* actual,
    const LandmarksDetectionResult& expected_proto,
    const float landmark_precision, const float score_precision) {
  ASSERT_EQ(actual->handedness_count, 1);
  ASSERT_EQ(actual->hand_landmarks_count, 1);

  const auto& actual_handedness = actual->handedness[0];
  const auto& expected_classification_list = expected_proto.classifications();
  ASSERT_EQ(actual_handedness.categories_count,
            expected_classification_list.classification_size());
  for (int j = 0; j < actual_handedness.categories_count; ++j) {
    const auto& actual_category = actual_handedness.categories[j];
    const auto& expected_class = expected_classification_list.classification(j);
    EXPECT_EQ(actual_category.index, expected_class.index());
    EXPECT_EQ(std::string{actual_category.category_name},
              expected_class.label());
    EXPECT_NEAR(actual_category.score, expected_class.score(), score_precision);
  }

  const auto& actual_landmark_list = actual->hand_landmarks[0];
  const auto& expected_landmark_list_proto = expected_proto.landmarks();
  ASSERT_EQ(actual_landmark_list.landmarks_count,
            expected_landmark_list_proto.landmark_size());
  for (int j = 0; j < actual_landmark_list.landmarks_count; ++j) {
    const auto& actual_landmark = actual_landmark_list.landmarks[j];
    const auto& expected_landmark_proto =
        expected_landmark_list_proto.landmark(j);
    EXPECT_NEAR(actual_landmark.x, expected_landmark_proto.x(),
                landmark_precision);
    EXPECT_NEAR(actual_landmark.y, expected_landmark_proto.y(),
                landmark_precision);
  }
}

struct MpImageDeleter {
  void operator()(MpImagePtr image) const {
    if (image) {
      MpImageFree(image);
    }
  }
};
using ScopedMpImage = std::unique_ptr<MpImageInternal, MpImageDeleter>;

ScopedMpImage GetImage(const std::string& file_name) {
  MpImagePtr image_ptr = nullptr;
  MpStatus status = MpImageCreateFromFile(file_name.c_str(), &image_ptr);
  EXPECT_EQ(status, kMpOk);
  EXPECT_NE(image_ptr, nullptr);
  return ScopedMpImage(image_ptr);
}

TEST(HandLandmarkerTest, ImageModeTest) {
  const auto image = GetImage(GetFullPath(kPointingUpImage));

  const std::string model_path = GetFullPath(kModelName);
  HandLandmarkerOptions options = {
      /* base_options= */ {/* model_asset_buffer= */ nullptr,
                           /* model_asset_buffer_count= */ 0,
                           /* model_asset_path= */ model_path.c_str()},
      /* running_mode= */ RunningMode::IMAGE,
      /* num_hands= */ 1,
      /* min_hand_detection_confidence= */ 0.5,
      /* min_hand_presence_confidence= */ 0.5,
      /* min_tracking_confidence= */ 0.5,
  };

  void* landmarker = hand_landmarker_create(&options, /* error_msg */ nullptr);
  EXPECT_NE(landmarker, nullptr);

  HandLandmarkerResult result;
  hand_landmarker_detect_image(landmarker, image.get(), &result,
                               /* error_msg */ nullptr);

  LandmarksDetectionResult expected_landmarks =
      GetLandmarksDetectionResult(kPointingUpLandmarksFilename);
  ExpectHandLandmarkerResultsCorrect(&result, expected_landmarks,
                                     kLandmarkPrecision, kScorePrecision);
  hand_landmarker_close_result(&result);
  hand_landmarker_close(landmarker, /* error_msg */ nullptr);
}

TEST(HandLandmarkerTest, ImageModeWithOptionsTest) {
  const auto image = GetImage(GetFullPath(kPointingUpRotatedImage));

  const std::string model_path = GetFullPath(kModelName);
  HandLandmarkerOptions options = {
      /* base_options= */ {/* model_asset_buffer= */ nullptr,
                           /* model_asset_buffer_count= */ 0,
                           /* model_asset_path= */ model_path.c_str()},
      /* running_mode= */ RunningMode::IMAGE,
      /* num_hands= */ 1,
      /* min_hand_detection_confidence= */ 0.5,
      /* min_hand_presence_confidence= */ 0.5,
      /* min_tracking_confidence= */ 0.5,
  };

  void* landmarker = hand_landmarker_create(&options, /* error_msg */ nullptr);
  EXPECT_NE(landmarker, nullptr);

  ImageProcessingOptions image_processing_options;
  image_processing_options.has_region_of_interest = 0;
  image_processing_options.rotation_degrees = -90;

  HandLandmarkerResult result;
  hand_landmarker_detect_image_with_options(landmarker, image.get(),
                                            &image_processing_options, &result,
                                            /* error_msg */ nullptr);

  LandmarksDetectionResult expected_landmarks =
      GetLandmarksDetectionResult(kPointingUpRotatedLandmarksFilename);
  ExpectHandLandmarkerResultsCorrect(&result, expected_landmarks,
                                     kLandmarkPrecision, kScorePrecision);
  hand_landmarker_close_result(&result);
  hand_landmarker_close(landmarker, /* error_msg */ nullptr);
}

TEST(HandLandmarkerTest, VideoModeTest) {
  const auto image = GetImage(GetFullPath(kPointingUpImage));

  const std::string model_path = GetFullPath(kModelName);
  HandLandmarkerOptions options = {
      /* base_options= */ {/* model_asset_buffer= */ nullptr,
                           /* model_asset_buffer_count= */ 0,
                           /* model_asset_path= */ model_path.c_str()},
      /* running_mode= */ RunningMode::VIDEO,
      /* num_hands= */ 1,
      /* min_hand_detection_confidence= */ 0.5,
      /* min_hand_presence_confidence= */ 0.5,
      /* min_tracking_confidence= */ 0.5,
  };

  void* landmarker = hand_landmarker_create(&options, /* error_msg */ nullptr);
  EXPECT_NE(landmarker, nullptr);

  LandmarksDetectionResult expected_landmarks =
      GetLandmarksDetectionResult(kPointingUpLandmarksFilename);
  for (int i = 0; i < kIterations; ++i) {
    HandLandmarkerResult result;
    hand_landmarker_detect_for_video(landmarker, image.get(), i, &result,
                                     /* error_msg */ nullptr);

    ExpectHandLandmarkerResultsCorrect(&result, expected_landmarks,
                                       kLandmarkPrecision, kScorePrecision);
    hand_landmarker_close_result(&result);
  }
  hand_landmarker_close(landmarker, /* error_msg */ nullptr);
}

// A structure to support LiveStreamModeTest below. This structure holds a
// static method `Fn` for a callback function of C API. A `static` qualifier
// allows to take an address of the method to follow API style. Another static
// struct member is `last_timestamp` that is used to verify that current
// timestamp is greater than the previous one.
struct LiveStreamModeCallback {
  static int64_t last_timestamp;
  static void Fn(HandLandmarkerResult* landmarker_result, MpImagePtr image,
                 int64_t timestamp, char* error_msg) {
    ASSERT_NE(landmarker_result, nullptr);
    ASSERT_EQ(error_msg, nullptr);
    LandmarksDetectionResult expected_landmarks =
        GetLandmarksDetectionResult(kPointingUpLandmarksFilename);
    ExpectHandLandmarkerResultsCorrect(landmarker_result, expected_landmarks,
                                       kLandmarkPrecision, kScorePrecision);
    EXPECT_GT(MpImageGetWidth(image), 0);
    EXPECT_GT(MpImageGetHeight(image), 0);
    EXPECT_GT(timestamp, last_timestamp);
    ++last_timestamp;
  }
};
int64_t LiveStreamModeCallback::last_timestamp = -1;

// TODO: Await the callbacks and re-enable test
TEST(HandLandmarkerTest, DISABLED_LiveStreamModeTest) {
  const auto image = GetImage(GetFullPath(kPointingUpImage));

  const std::string model_path = GetFullPath(kModelName);

  HandLandmarkerOptions options = {
      /* base_options= */ {/* model_asset_buffer= */ nullptr,
                           /* model_asset_buffer_count= */ 0,
                           /* model_asset_path= */ model_path.c_str()},
      /* running_mode= */ RunningMode::LIVE_STREAM,
      /* num_hands= */ 1,
      /* min_hand_detection_confidence= */ 0.5,
      /* min_hand_presence_confidence= */ 0.5,
      /* min_tracking_confidence= */ 0.5,
      /* result_callback_fn= */ LiveStreamModeCallback::Fn,
  };

  void* landmarker = hand_landmarker_create(&options, /* error_msg */ nullptr);
  EXPECT_NE(landmarker, nullptr);

  for (int i = 0; i < kIterations; ++i) {
    EXPECT_GE(hand_landmarker_detect_async(landmarker, image.get(), i,
                                           /* error_msg */ nullptr),
              0);
  }
  hand_landmarker_close(landmarker, /* error_msg */ nullptr);

  // Due to the flow limiter, the total of outputs might be smaller than the
  // number of iterations.
  EXPECT_LE(LiveStreamModeCallback::last_timestamp, kIterations);
  EXPECT_GT(LiveStreamModeCallback::last_timestamp, 0);
}

TEST(HandLandmarkerTest, InvalidArgumentHandling) {
  // It is an error to set neither the asset buffer nor the path.
  HandLandmarkerOptions options = {
      /* base_options= */ {/* model_asset_buffer= */ nullptr,
                           /* model_asset_buffer_count= */ 0,
                           /* model_asset_path= */ nullptr},
      /* running_mode= */ RunningMode::IMAGE,
      /* num_hands= */ 1,
      /* min_hand_detection_confidence= */ 0.5,
      /* min_hand_presence_confidence= */ 0.5,
      /* min_tracking_confidence= */ 0.5,
  };

  char* error_msg;
  void* landmarker = hand_landmarker_create(&options, &error_msg);
  EXPECT_EQ(landmarker, nullptr);

  EXPECT_THAT(error_msg, HasSubstr("ExternalFile must specify"));

  free(error_msg);
}

}  // namespace
