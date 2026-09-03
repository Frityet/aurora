#include <dolphin/os.h>
#include <dolphin/gx/GXStruct.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

void OSReport(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  OSVReport(msg, args);
  va_end(args);
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
  std::fflush(stderr);
  std::abort();
}

void OSFatal(GXColor, GXColor, const char* msg) {
  // Native termination does not require a working GX frame or exception UI.
  std::fprintf(stderr, "%s\n", msg);
  std::fflush(stderr);
  std::abort();
}
