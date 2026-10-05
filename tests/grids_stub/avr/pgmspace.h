#pragma once
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* host stand-in for the AVR header the original includes (tests/grids_ref.cc only) */
#include <stdint.h>
#include <string.h>
#define PROGMEM
typedef char prog_char;
typedef uint8_t prog_uint8_t;
typedef uint16_t prog_uint16_t;
typedef uint32_t prog_uint32_t;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define strncpy_P strncpy
#define memcpy_P memcpy
