//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/channelz/channel_trace.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <stdlib.h>

#include <list>
#include <ostream>
#include <string>
#include <thread>

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

namespace grpc_core {

template <typename Sink>
void AbslStringify(Sink& sink, const Json& json) {
  sink.Append(JsonDump(json));
}

std::ostream& operator<<(std::ostream& os, const Json& json) {
  return os << JsonDump(json);
}

namespace channelz {
namespace testing {

namespace {

MATCHER_P(IsJsonString, expected, "is JSON string") {
  if (!::testing::ExplainMatchResult(Json::Type::kString, arg.type(),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.string(), result_listener);
}

MATCHER_P(IsJsonStringNumber, expected, "is JSON string containing number") {
  if (!::testing::ExplainMatchResult(Json::Type::kString, arg.type(),
                                     result_listener)) {
    return false;
  }
  int actual;
  if (!absl::SimpleAtoi(arg.string(), &actual)) {
    *result_listener << "JSON string \"" << arg.string()
                     << " does not contain numeric value";
    return false;
  }
  return ::testing::ExplainMatchResult(expected, actual, result_listener);
}

MATCHER_P(IsJsonObject, matcher, "is JSON object") {
  if (!::testing::ExplainMatchResult(Json::Type::kObject, arg.type(),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, arg.object(), result_listener);
}

MATCHER_P(IsJsonArray, matcher, "is JSON array") {
  if (!::testing::ExplainMatchResult(Json::Type::kArray, arg.type(),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, arg.array(), result_listener);
}

MATCHER_P2(IsTraceEvent, description, severity, "is trace event") {
  return ::testing::ExplainMatchResult(
      IsJsonObject(::testing::ElementsAre(
          ::testing::Pair("description", IsJsonString(description)),
          ::testing::Pair("severity", IsJsonString(severity)),
          ::testing::Pair("timestamp", IsJsonString(::testing::_)))),
      arg, result_listener);
}

MATCHER_P2(IsTraceEventWithChannelRef, description, channel_ref,
           "is trace event with channel ref") {
  return ::testing::ExplainMatchResult(
      IsJsonObject(::testing::ElementsAre(
          ::testing::Pair("description", IsJsonString(description)),
          ::testing::Pair("severity", IsJsonString("CT_INFO")),
          ::testing::Pair("timestamp", IsJsonString(::testing::_)))),
      arg, result_listener);
}

MATCHER_P3(IsTraceEventWithSubchannelRef, description, severity, subchannel_ref,
           "is trace event with subchannel ref") {
  return ::testing::ExplainMatchResult(
      IsJsonObject(::testing::ElementsAre(
          ::testing::Pair("description", IsJsonString(description)),
          ::testing::Pair("severity", IsJsonString("CT_INFO")),
          ::testing::Pair("timestamp", IsJsonString(::testing::_)))),
      arg, result_listener);
}

MATCHER_P(IsEmptyChannelTrace, num_events_logged_expected,
          "is empty channel trace") {
  return ::testing::ExplainMatchResult(
      IsJsonObject(::testing::ElementsAre(
          ::testing::Pair("creationTimestamp", IsJsonString(::testing::_)),
          ::testing::Pair("numEventsLogged",
                          IsJsonStringNumber(num_events_logged_expected)))),
      arg, result_listener);
}

MATCHER_P2(IsChannelTraceWithEvents, num_events_logged_expected, events_matcher,
           "is channel trace") {
  return ::testing::ExplainMatchResult(
      IsJsonObject(::testing::ElementsAre(
          ::testing::Pair("creationTimestamp", IsJsonString(::testing::_)),
          ::testing::Pair("events", IsJsonArray(events_matcher)),
          ::testing::Pair("numEventsLogged",
                          IsJsonStringNumber(num_events_logged_expected)))),
      arg, result_listener);
}

void ValidateJsonProtoTranslation(const Json& json) {
  std::string json_str = JsonDump(json);
  grpc::testing::ValidateChannelTraceProtoJsonTranslation(json_str);
}

}  // anonymous namespace

const int kEventListMemoryLimit = 1024 * 1024;

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
TEST(ChannelTracerTest, BasicTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  tracer.NewNode("one").Commit();
  tracer.NewNode("two").Commit();
  tracer.NewNode("three").Commit();
  tracer.NewNode("four").Commit();
  Json json = tracer.RenderJson();
  ValidateJsonProtoTranslation(json);
  EXPECT_THAT(json,
              IsChannelTraceWithEvents(
                  4, ::testing::ElementsAre(IsTraceEvent("one", "CT_INFO"),
                                            IsTraceEvent("two", "CT_INFO"),
                                            IsTraceEvent("three", "CT_INFO"),
                                            IsTraceEvent("four", "CT_INFO"))))
      << JsonDump(json);
  tracer.NewNode("five").Commit();
  tracer.NewNode("six").Commit();
  json = tracer.RenderJson();
  ValidateJsonProtoTranslation(json);
  EXPECT_THAT(
      json,
      IsChannelTraceWithEvents(
          6,
          ::testing::ElementsAre(
              IsTraceEvent("one", "CT_INFO"), IsTraceEvent("two", "CT_INFO"),
              IsTraceEvent("three", "CT_INFO"), IsTraceEvent("four", "CT_INFO"),
              IsTraceEvent("five", "CT_INFO"), IsTraceEvent("six", "CT_INFO"))))
      << JsonDump(json);
}

TEST(ChannelTracerTest, BasicProtoTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  tracer.NewNode("one").Commit();
  tracer.NewNode("two").Commit();
  tracer.NewNode("three").Commit();
  tracer.NewNode("four").Commit();
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
  tracer.Render(entity, arena);
  size_t size;
  const grpc_channelz_v2_TraceEvent* const* trace =
      grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 4);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[0])),
      "one");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[1])),
      "two");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[2])),
      "three");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[3])),
      "four");
  upb_Arena_Free(arena);
  tracer.NewNode("five").Commit();
  tracer.NewNode("six").Commit();
  arena = upb_Arena_New();
  entity = grpc_channelz_v2_Entity_new(arena);
  tracer.Render(entity, arena);
  trace = grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 6);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[0])),
      "one");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[1])),
      "two");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[2])),
      "three");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[3])),
      "four");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[4])),
      "five");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[5])),
      "six");
  upb_Arena_Free(arena);
}

TEST(ChannelTracerTest, StreamingOutputTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  GRPC_CHANNELZ_LOG(tracer) << "one";
  GRPC_CHANNELZ_LOG(tracer) << "two";
  GRPC_CHANNELZ_LOG(tracer) << "three";
  GRPC_CHANNELZ_LOG(tracer) << "four";
  Json json = tracer.RenderJson();
  ValidateJsonProtoTranslation(json);
  EXPECT_THAT(json,
              IsChannelTraceWithEvents(
                  4, ::testing::ElementsAre(IsTraceEvent("one", "CT_INFO"),
                                            IsTraceEvent("two", "CT_INFO"),
                                            IsTraceEvent("three", "CT_INFO"),
                                            IsTraceEvent("four", "CT_INFO"))))
      << JsonDump(json);
  tracer.NewNode("five").Commit();
  tracer.NewNode("six").Commit();
  json = tracer.RenderJson();
  ValidateJsonProtoTranslation(json);
  EXPECT_THAT(
      json,
      IsChannelTraceWithEvents(
          6,
          ::testing::ElementsAre(
              IsTraceEvent("one", "CT_INFO"), IsTraceEvent("two", "CT_INFO"),
              IsTraceEvent("three", "CT_INFO"), IsTraceEvent("four", "CT_INFO"),
              IsTraceEvent("five", "CT_INFO"), IsTraceEvent("six", "CT_INFO"))))
      << JsonDump(json);
}

TEST(ChannelTracerTest, StreamingOutputProtoTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  GRPC_CHANNELZ_LOG(tracer) << "one";
  GRPC_CHANNELZ_LOG(tracer) << "two";
  GRPC_CHANNELZ_LOG(tracer) << "three";
  GRPC_CHANNELZ_LOG(tracer) << "four";
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
  tracer.Render(entity, arena);
  size_t size;
  const grpc_channelz_v2_TraceEvent* const* trace =
      grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 4);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[0])),
      "one");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[1])),
      "two");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[2])),
      "three");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[3])),
      "four");
  upb_Arena_Free(arena);
  GRPC_CHANNELZ_LOG(tracer) << "five";
  GRPC_CHANNELZ_LOG(tracer) << "six";
  arena = upb_Arena_New();
  entity = grpc_channelz_v2_Entity_new(arena);
  tracer.Render(entity, arena);
  trace = grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 6);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[0])),
      "one");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[1])),
      "two");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[2])),
      "three");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[3])),
      "four");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[4])),
      "five");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(trace[5])),
      "six");
  upb_Arena_Free(arena);
}

TEST(ChannelTracerTest, TestSmallMemoryLimit) {
  // Doesn't make sense in practice, but serves a testing purpose for the
  // channel tracing bookkeeping. All tracing events added should get
  // immediately garbage collected.
  const int kSmallMemoryLimit = 1;
  ChannelTrace tracer(kSmallMemoryLimit);
  const size_t kNumEvents = 4;
  for (size_t i = 0; i < kNumEvents; ++i) {
    tracer.NewNode("trace").Commit();
  }
  Json json = tracer.RenderJson();
  ValidateJsonProtoTranslation(json);
  EXPECT_THAT(json, IsEmptyChannelTrace(4)) << JsonDump(json);
}

TEST(ChannelTracerTest, TestSmallMemoryLimitProto) {
  // Set a very small memory limit for the trace.
  const int kSmallMemoryLimit = 1;
  ChannelTrace tracer(kSmallMemoryLimit);
  const size_t kNumEvents = 4;
  // Add a few trace events. These should be immediately garbage collected
  // from the event list due to the small memory limit.
  for (size_t i = 0; i < kNumEvents; ++i) {
    tracer.NewNode("trace").Commit();
  }
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
  // Render the trace to the proto.
  tracer.Render(entity, arena);
  size_t size;
  grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 0);
  upb_Arena_Free(arena);
}

// Tests that the code is thread-safe.
TEST(ChannelTracerTest, ThreadSafety) {
  ChannelTrace tracer(kEventListMemoryLimit);
  absl::Notification done;
  std::vector<std::unique_ptr<std::thread>> threads;
  for (size_t i = 0; i < 10; ++i) {
    threads.push_back(std::make_unique<std::thread>([&]() {
      do {
        for (int i = 0; i < 100; i++) {
          if (done.HasBeenNotified()) return;
          tracer.NewNode("trace").Commit();
        }
        absl::SleepFor(absl::Milliseconds(1));
      } while (!done.HasBeenNotified());
    }));
  }
  for (size_t i = 0; i < 10; ++i) {
    absl::SleepFor(absl::Milliseconds(1));
    tracer.RenderJson();
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    upb_Arena_Free(arena);
  }
  done.Notify();
  for (const auto& thd : threads) {
    thd->join();
  }
}

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
