#pragma once
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* host stand-in for the AVR header the original includes (tests/grids_ref.cc only) */
#include <stdint.h>
namespace avrlib {
static inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance) { return (uint16_t)(a * (255 - balance) + b * balance) >> 8; }
static inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) { return (uint16_t)(a * b) >> 8; }
static inline uint16_t U8U8Mul(uint8_t a, uint8_t b) { return (uint16_t)(a * b); }
}
