#include <dolphin/os.h>
#include <dolphin/gx/GXStruct.h>
#include <gtest/gtest.h>

#include <cstdarg>
#include <string>

namespace {
void report_twice(const char* format, ...) {
  va_list args;
  va_start(args, format);
  OSVReport(format, args);
  OSVReport(format, args);
  va_end(args);
}

TEST(OSReportTest, ReportPreservesFormattingAndNewlineOwnership) {
  testing::internal::CaptureStderr();
  OSReport("frame=%u rate=%.2f %s", 60U, 1.0, "ready");
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "frame=60 rate=1.00 ready");
}

TEST(OSReportTest, VaReportDoesNotConsumeCallerArguments) {
  testing::internal::CaptureStderr();
  report_twice("%s:%d/%llx\n", "tick", -17, 0x123456789abcdef0ULL);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            "tick:-17/123456789abcdef0\ntick:-17/123456789abcdef0\n");
}

TEST(OSReportTest, ReportDoesNotTruncateLongMessages) {
  const std::string message(4096, 'x');
  testing::internal::CaptureStderr();
  OSReport("%s", message.c_str());
  EXPECT_EQ(testing::internal::GetCapturedStderr(), message);
}

TEST(OSReportTest, PanicPrintsLocationAndTerminates) {
  EXPECT_DEATH(OSPanic("heap.cpp", 17, "allocation %u", 4096U),
               "PANIC heap.cpp:17: allocation 4096");
}

TEST(OSReportTest, FatalTreatsMessageAsTextAndTerminates) {
  EXPECT_DEATH(OSFatal((GXColor{255, 255, 255, 255}), (GXColor{0, 0, 0, 255}), "fatal 50% ready"),
               "fatal 50% ready");
}
}
