#include <dolphin/os.h>
#include <dolphin/gx/GXStruct.h>

#include <aurora/allocation.hpp>
#include <aurora/diagnostics.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif __has_include(<execinfo.h>)
#include <execinfo.h>
#include <unistd.h>
#define AURORA_HAS_EXECINFO 1
#endif

namespace {

// Fixed stack storage also permits reports during client heap exhaustion. The
// native unwinder may initialize its own state; never route that to Game heaps.
void report_native_stack() noexcept {
  const aurora::allocation::HostAllocationScope host;
  std::fputs("Native stack (most recent call first):\n", stderr);
  std::fflush(stderr);
  void* frames[64];
#if defined(_WIN32)
  const auto count = CaptureStackBackTrace(0, 64, frames, nullptr);
  for (unsigned i = 0; i < count; ++i) std::fprintf(stderr, "  %p\n", frames[i]);
#elif defined(AURORA_HAS_EXECINFO)
  const int count = backtrace(frames, 64);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
#else
  std::fputs("Native stack unwinding is unavailable on this platform.\n", stderr);
#endif
}

[[noreturn]] void report_unhandled_exception() noexcept {
  static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
  if (reporting.test_and_set(std::memory_order_relaxed)) std::abort();
  const aurora::allocation::HostAllocationScope host;
  std::fputs("Aurora: native C++ termination\n", stderr);
  if (const auto exception = std::current_exception()) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "Unhandled exception: %s\n", error.what());
    } catch (...) {
      std::fputs("Unhandled exception: non-std::exception object\n", stderr);
    }
  } else {
    std::fputs("No active C++ exception.\n", stderr);
  }
  report_native_stack();
  std::fflush(stderr);
  std::abort();
}

} // namespace

void aurora::diagnostics::install_native_exception_reporting() noexcept {
  const allocation::HostAllocationScope host;
  std::set_terminate(report_unhandled_exception);
}

void OSReport(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  OSVReport(msg, args);
  va_end(args);
}

void OSRegisterVersion(const char* id) {
  OSReport("%s\n", id);
}

void OSVReport(const char* msg, va_list list) {
  // The SDK serial report channel maps to host stderr. Keep reporting usable
  // during heap exhaustion: no Game allocation or formatted-string owner is
  // required, and the caller retains its va_list state.
  va_list copy;
  va_copy(copy, list);
  std::vfprintf(stderr, msg, copy);
  va_end(copy);
}

void OSPanic(const char* file, int line, const char* msg, ...) {
  std::fprintf(stderr, "PANIC %s:%d: ", file, line);
  va_list args;
  va_start(args, msg);
  OSVReport(msg, args);
  va_end(args);
  std::fputc('\n', stderr);
  report_native_stack();
  std::fflush(stderr);
  std::abort();
}

void OSFatal(GXColor, GXColor, const char* msg) {
  // Native termination does not require a working GX frame or exception UI.
  std::fprintf(stderr, "%s\n", msg);
  report_native_stack();
  std::fflush(stderr);
  std::abort();
}
