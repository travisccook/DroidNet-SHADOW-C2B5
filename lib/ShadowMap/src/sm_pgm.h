// Flash-string shims so the library builds on AVR (PROGMEM) and on the host (plain memory).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char *
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t *>(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t *>(addr))
#endif
#ifndef strcmp_P
#define strcmp_P strcmp
#endif
#ifndef strcpy_P
#define strcpy_P strcpy
#endif
#ifndef strcat_P
#define strcat_P strcat
#endif
#ifndef strlen_P
#define strlen_P strlen
#endif
#ifndef memcpy_P
#define memcpy_P memcpy
#endif
#endif
