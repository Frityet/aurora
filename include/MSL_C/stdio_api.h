#ifndef MSL_C_STDIO_API_H
#define MSL_C_STDIO_API_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Concrete MSL string-output entry points. These declarations leave host
 * stdio untouched so the implementation can use native numeric conversions. */
int __msl_sprintf(char* output, const char* format, ...);
int __msl_snprintf(char* output, size_t size, const char* format, ...);
int __msl_vsprintf(char* output, const char* format, va_list args);
int __msl_vsnprintf(char* output, size_t size, const char* format, va_list args);

#ifdef __cplusplus
}
#endif

#endif
