// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "joint_distribution_sample_generator.h"

#include "gtest/gtest.h"

namespace distbench {

class DistributionSampleGeneratorTest : public ::testing::Test {};

TEST(DistributionSampleGeneratorTest, NoDistributionConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest, BothCdfAndPdfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(1);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);

  auto* cdf_point = config.add_cdf_points();
  cdf_point->set_cdf(1);
  cdf_point->set_value(10);

  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest, ValidateDistributionPmfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 10.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status, absl::OkStatus());
}

TEST(DistributionSampleGeneratorTest, InvalidDistributionPmfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 20.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Cumulative value of all PMFs should be 1. It is '0.5' instead."));
}

TEST(DistributionSampleGeneratorTest, ValidateDistributionCdfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 10.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status, absl::OkStatus());
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigErraneousCdf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 20.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The maximum value of cdf is '0.5' in CDF:'MyReqPayloadDC'. "
                "It must be exactly equal to 1."));
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigNonIncreasingValues) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(100 - 10 * i);
    cdf += i / 20.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The value:'80' must be greater than previous_value:'90' at "
                "index '1' in CDF:'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigNonIncreasingCdf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 20.0;
    cdf_point->set_cdf(1 / cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The cdf value:'6.66667' must be greater than previous cdf "
                "value:'20' at index '1' in CDF:'MyReqPayloadDC'."));
}

int EstimateCount(int N, float fraction) { return N * fraction; }

TEST(DistributionSampleGeneratorTest, FullTestPmf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    // Generate values of 1, 2, 3 and 4 with a probability
    // of 0.1, 0.2, 0.3 and 0.4 respectively.
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 10.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status, absl::OkStatus());

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  std::map<int, int> sample_count;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample_value = sg->GetRandomSample();
    sample_count[sample_value[0]]++;
  }

  const int kTolerance = kReps / 100;
  ASSERT_EQ(sample_count.size(), 4);
  for (int i = 1; i < 5; i++) {
    ASSERT_LT(abs(sample_count[i] - EstimateCount(kReps, i / 10.0)),
              kTolerance);
  }
}

TEST(DistributionSampleGeneratorTest, FullTestCdf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  config.set_is_cdf_uniform(false);

  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    // Generate exact values of 100, 200, 300 and 400 with a CDF
    // of 0.1, 0.3, 0.6 and 1.0 respectively.
    cdf += i / 10.0;
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i * 100);
    cdf_point->set_cdf(cdf);
  }

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.status(), absl::OkStatus());
  auto sg = std::move(maybe_sg.value());

  std::map<int, int> sample_count;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample_value = sg->GetRandomSample();
    sample_count[sample_value[0]]++;
  }

  ASSERT_EQ(sample_count.size(), 4);
  const int kTolerance = kReps / 100;
  for (int i = 1; i < 5; i++) {
    ASSERT_LT(abs(sample_count[i * 100] - EstimateCount(kReps, i / 10.0)),
              kTolerance);
  }
}

TEST(DistributionSampleGeneratorTest, FullTestCdfUniformIntervals) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  config.set_is_cdf_uniform(true);

  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    // Generate integral values of less than or equal to 100, 200, 300
    // and 400 with a CDF of 0.1, 0.3, 0.6 and 1.0 respectively.
    cdf += i / 10.0;
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i * 100);
    cdf_point->set_cdf(cdf);
  }

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.status(), absl::OkStatus());
  auto sg = std::move(maybe_sg.value());

  std::map<int, int> sample_count;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample_value = sg->GetRandomSample();
    sample_count[sample_value[0]]++;
  }

  ASSERT_GT(sample_count.size(), 100);

  std::map<int, int> bucket;
  for (auto& sample : sample_count) {
    auto value = sample.first;
    if (value <= 100)
      bucket[100] += sample.second;
    else if (value <= 200)
      bucket[200] += sample.second;
    else if (value <= 300)
      bucket[300] += sample.second;
    else if (value <= 400)
      bucket[400] += sample.second;
    else {
      FAIL() << "Out of range value '" << value
             << "' generated by sample generator: '" << config.name() << "'.";
    }
  }

  const int kTolerance = kReps / 100;
  for (int i = 1; i < 5; i++) {
    ASSERT_LT(abs(bucket[i * 100] - EstimateCount(kReps, i / 10.0)),
              kTolerance);
  }
}

TEST(DistributionSampleGeneratorTest, InvalidCdfInitializeTest) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    // Generate values of 1, 2, 3 and 4 with a CDF
    // of 0.01, 0.03, 0.06 and 0.1 respectively.
    cdf += i / 100.0;
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf_point->set_cdf(cdf);
  }

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(
      maybe_sg.status(),
      absl::InvalidArgumentError(
          "The maximum value of cdf is '0.1' in CDF:'MyReqPayloadDC'. It must "
          "be exactly equal to 1."));
}

TEST(DistributionSampleGeneratorTest, InvalidPmfInitializeTest) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    // Generate values of 1, 2, 3 and 4 with a probability
    // of 0.01, 0.02, 0.03 and 0.04 respectively.
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 100.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(
      maybe_sg.status(),
      absl::InvalidArgumentError(
          "Cumulative value of all PMFs should be 1. It is '0.1' instead."));
}

TEST(DistributionSampleGeneratorTest, PmfRangeTest) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  // The range of 'small' numbers.
  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.2);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_lower(10);
  data_point->set_upper(20);

  // The range of 'big' numbers.
  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.8);
  data_point = pmf_point->add_data_points();
  data_point->set_lower(10000);
  data_point->set_upper(10010);

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  int small_count = 0;
  int big_count = 0;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample = sg->GetRandomSample();
    auto sample_value = sample[0];
    if ((sample_value >= 10) && (sample_value <= 20))
      small_count++;
    else if ((sample_value >= 10000) && (sample_value <= 10010))
      big_count++;
    else {
      FAIL() << "Out of range value '" << sample_value
             << "' generated by sample generator: '" << config.name() << "'.";
    }
  }

  const int kTolerance = kReps / 100;
  ASSERT_LT(abs(small_count - EstimateCount(kReps, 0.2)), kTolerance);
  ASSERT_LT(abs(big_count - EstimateCount(kReps, 0.8)), kTolerance);
}

TEST(DistributionSampleGeneratorTest, PmfRangeAndValueMixTest) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  // 10 is our 'small' number.
  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.2);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);

  // [10000, 10010] range of 'big' numbers.
  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.8);
  data_point = pmf_point->add_data_points();
  data_point->set_lower(10000);
  data_point->set_upper(10010);

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  int small_count = 0;
  int big_count = 0;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample = sg->GetRandomSample();
    auto sample_value = sample[0];
    if (sample_value == 10)
      small_count++;
    else if ((sample_value >= 10000) && (sample_value <= 10010))
      big_count++;
    else {
      FAIL() << "Out of range value '" << sample_value
             << "' generated by sample generator: '" << config.name() << "'.";
    }
  }
  const int kTolerance = kReps / 100;
  ASSERT_LT(abs(small_count - EstimateCount(kReps, 0.2)), kTolerance);
  ASSERT_LT(abs(big_count - EstimateCount(kReps, 0.8)), kTolerance);
}

TEST(DistributionSampleGeneratorTest, Pmf2Vars) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    // Generate values of (1,10) , (2,20), (3,30) and (4,40) with a probability
    // of 0.1, 0.2, 0.3 and 0.4 respectively.
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 10.0);

    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);

    data_point = pmf_point->add_data_points();
    data_point->set_exact(i * 10);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status, absl::OkStatus());

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  std::map<std::vector<int>, int> sample_count;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample = sg->GetRandomSample();
    ASSERT_EQ(sample.size(), 2);
    sample_count[sample]++;
  }

  const int kTolerance = kReps / 100;
  for (int i = 1; i < 5; i++) {
    std::vector<int> key = {i, i * 10};
    ASSERT_LT(abs(sample_count[key] - EstimateCount(kReps, i / 10.0)),
              kTolerance);
  }
}

TEST(DistributionSampleGeneratorTest, PmfRangeAndValueMixTest2Vars) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  // The 'small' sample has first variable exactly equal to 10.
  // The 'small' sample has second variable exactly in range [20, 30].
  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.2);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);
  data_point = pmf_point->add_data_points();
  data_point->set_lower(20);
  data_point->set_upper(30);

  // The 'big' sample has first variable exactly in range [10010, 10030].
  // The 'big' sample has second variable exactly equal to 10000.
  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.8);
  data_point = pmf_point->add_data_points();
  data_point->set_lower(10010);
  data_point->set_upper(10030);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(10000);

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  int small_count = 0;
  int big_count = 0;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample = sg->GetRandomSample();
    ASSERT_EQ(sample.size(), 2);
    auto var1 = sample[0];
    auto var2 = sample[1];
    if ((var1 == 10) && (var2 >= 20 && var2 <= 30))
      small_count++;
    else if ((var1 >= 10010 && var1 <= 10030) && (var2 == 10000))
      big_count++;
    else {
      FAIL() << "Out of range value (" << var1 << "," << var2
             << ") generated by sample generator: '" << config.name() << "'.";
    }
  }
  const int kTolerance = kReps / 100;
  ASSERT_LT(abs(small_count - EstimateCount(kReps, 0.2)), kTolerance);
  ASSERT_LT(abs(big_count - EstimateCount(kReps, 0.8)), kTolerance);
}

TEST(DistributionSampleGeneratorTest, Pmf3Vars) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.1);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(100);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(1000);

  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.3);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(30);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(300);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(3000);

  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.6);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(60);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(600);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(6000);

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.ok(), true);
  auto sg = std::move(maybe_sg.value());

  int small_count = 0;
  int medium_count = 0;
  int big_count = 0;
  const int kReps = 100000;
  for (int i = 0; i < kReps; i++) {
    auto sample = sg->GetRandomSample();
    ASSERT_EQ(sample.size(), 3);
    auto var0 = sample[0];
    auto var1 = sample[1];
    auto var2 = sample[2];
    if ((var0 == 10) && (var1 == 100) && (var2 == 1000))
      small_count++;
    else if ((var0 == 30) && (var1 == 300) && (var2 == 3000))
      medium_count++;
    else if ((var0 == 60) && (var1 == 600) && (var2 == 6000))
      big_count++;
    else {
      FAIL() << "Out of range value (" << var0 << "," << var1 << "," << var2
             << ") generated by sample generator: '" << config.name() << "'.";
    }
  }

  const int kTolerance = kReps / 100;
  ASSERT_LT(abs(small_count - EstimateCount(kReps, 0.1)), kTolerance);
  ASSERT_LT(abs(medium_count - EstimateCount(kReps, 0.3)), kTolerance);
  ASSERT_LT(abs(big_count - EstimateCount(kReps, 0.6)), kTolerance);
}

TEST(DistributionSampleGeneratorTest, PmfPointDifferentVariableNumbers) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.1);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(100);

  pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(0.9);
  data_point = pmf_point->add_data_points();
  data_point->set_exact(90);

  auto maybe_sg = AllocateSampleGenerator(config);
  ASSERT_EQ(maybe_sg.status(),
            absl::InvalidArgumentError(
                "The size of data_points must be same in all PmfPoints."));
}

}  // namespace distbench
