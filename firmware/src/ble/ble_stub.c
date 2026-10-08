/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core-reference build (FELUCCA_CORE_REF=1): the core's unit compiled exactly as in a BLE build (its wrappers,
 * its main-loop calls), linked with these empty entry points instead of the BLE unit and the JieLi libraries. */
#include <stdint.h>
void ble_service(void) {}
void ble_console(const char *args) { (void)args; }
void ble_status(void) {}
int ble_started(void) { return 0; }
