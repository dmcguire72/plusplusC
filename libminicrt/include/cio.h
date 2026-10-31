// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#ifndef IO_H
#define IO_H

#include <def.h>
#include <dstr.h>
#include <parr.h>

#define SIOOUT 0
#define SIOERR 1

int print(const char *str, unsigned int stream);

int print_ds(dstr_t *s, unsigned int stream);

int puts(const char *str, unsigned int stream);

int puts_ds(dstr_t *s, unsigned int stream);

int putchar(int c, unsigned int stream);

void print_parr_c_string(const parr_t *csArr, unsigned int stream);

void print_parr_dstr(const parr_t *dsArr, unsigned int stream);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) XXC_PRINTF_VALIDATE(3, 0);

int snprintf(char *buf, size_t size, const char *fmt, ...) XXC_PRINTF_VALIDATE(3, 4);

int vprintf(const char *fmt, va_list args, unsigned int stream) XXC_PRINTF_VALIDATE(1, 0);

int printf(const char *fmt, ...) XXC_PRINTF_VALIDATE(1, 2);

#endif