#pragma once
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* host stand-in for the AVR header the original includes (tests/grids_ref.cc only) */
#include <stdint.h>
static inline uint8_t eeprom_read_byte(const uint8_t *a) { (void)a; return 0x08 | 0x40; }   /* no swing, drums */
static inline void eeprom_write_byte(uint8_t *a, uint8_t v) { (void)a; (void)v; }
