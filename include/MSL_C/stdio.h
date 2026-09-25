#ifndef MSL_C_STDIO_H
#define MSL_C_STDIO_H

/* Include before any host stdio declarations. On Darwin, the first declaration
 * determines the assembler label. Only string-output formatting uses MSL;
 * native FILE operations and stdout logging retain their host implementation. */
#include <MSL_C/stdio_api.h>

#if defined(__clang__) || defined(__GNUC__)
#define MSL_STDIO_STRINGIFY_INNER(value) #value
#define MSL_STDIO_STRINGIFY(value) MSL_STDIO_STRINGIFY_INNER(value)
#define MSL_STDIO_LABEL(name) MSL_STDIO_STRINGIFY(__USER_LABEL_PREFIX__) #name

#if defined(__cplusplus) && defined(__linux__)
#define MSL_STDIO_NOTHROW noexcept
#else
#define MSL_STDIO_NOTHROW
#endif

#ifdef __cplusplus
extern "C" {
#endif

int sprintf(char*, const char*, ...) MSL_STDIO_NOTHROW __asm__(MSL_STDIO_LABEL(__msl_sprintf));
int snprintf(char*, size_t, const char*, ...) MSL_STDIO_NOTHROW __asm__(MSL_STDIO_LABEL(__msl_snprintf));
int vsprintf(char*, const char*, va_list) MSL_STDIO_NOTHROW __asm__(MSL_STDIO_LABEL(__msl_vsprintf));
int vsnprintf(char*, size_t, const char*, va_list) MSL_STDIO_NOTHROW __asm__(MSL_STDIO_LABEL(__msl_vsnprintf));

#ifdef __cplusplus
}
#endif

#undef MSL_STDIO_NOTHROW
#undef MSL_STDIO_LABEL
#undef MSL_STDIO_STRINGIFY
#undef MSL_STDIO_STRINGIFY_INNER
#else
#error The MSL formatted-string boundary requires compiler symbol-label support.
#endif

#ifdef __cplusplus
#include <cstdio>
#else
#include <stdio.h>
#endif

#endif
