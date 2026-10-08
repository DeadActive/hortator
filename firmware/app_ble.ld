/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Hortator's BLE build (FELUCCA_BLE=1, tools/build.py): app.ld (the same memory map) plus the JieLi BT libraries'
 * sections, placed as the AC79 SDK's cpu/wl82/sdk_ld_sfc.c does (Apache-2.0, JieLi): their code and constants in
 * XIP (.text), their data in .data, their zeroed data in .bss. The fragments' pool symbols are given with zero
 * size (no classic BT, no TWS). The radio calibration (*.rf.text.cache.L2.rf_trim, .volatile_ram_code) runs from RAM, in .ram_text, as
 * the SDK's demos build it (CONFIG_RF_TRIM_CODE_AT_RAM): during the ~500 ms trim the flash must not be read
 * (ble_port.c's enter/exit_wifi_rf_trim_region turn interrupts off; build.py's reach check: .ram_text reaches
 * nothing in XIP). build.py fails the link on any other *ram_code* section.
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE
 * FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver); 0x01C06000..0x01C08000 stays for RAM-run debug.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
        . = ALIGN(4);
        a2dp_source_media_codec_begin = .; KEEP(*(.a2dp_source_media_codec)) a2dp_source_media_codec_end = .;
        a2dp_sink_media_probe_begin = .; KEEP(*(.a2dp_sink_media_probe)) a2dp_sink_media_probe_end = .;
        a2dp_sink_media_codec_begin = .; KEEP(*(.a2dp_sink_media_codec)) a2dp_sink_media_codec_end = .;
        a2dp_event_handler_begin = .; KEEP(*(.a2dp_event_handler)) a2dp_event_handler_end = .;
        sdp_record_item_begin = .; KEEP(*(.sdp_record_item)) sdp_record_item_end = .;
        bt_sleep_begin = .; KEEP(*(.bt_sleep)) bt_sleep_end = .;
        tws_func_stub_begin = .; KEEP(*(.tws_func_stub)) tws_func_stub_end = .;
        tws_sync_channel_begin = .; KEEP(*(.tws_sync_channel)) tws_sync_channel_end = .;
        _sys_event_handler_begin = .; PROVIDE(sys_event_handler_begin = .); KEEP(*(.sys_event.*))
        _sys_event_handler_end = .; PROVIDE(sys_event_handler_end = .);
        lp_target_begin = .; KEEP(*(.lp_target)) lp_target_end = .;   /* (include_lib/driver/cpu/wl82/system.ld) */
        /* every library *_code section but .volatile_ram_code (in .ram_text below; first match wins) */
        *(.[a-uw-z]*_code) *(.v[a-np-z]*_code) *(.*_const) *(.LOG_TAG_CONST)
        . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : {
        _rt_start = .; KEEP(*(.ram_text*))
        *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
        . = ALIGN(4); _rt_end = .;
    } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .; *(.bss*) *(.sbss*) *(COMMON)
        *(.bd_base) *(.*_bulk) *(.*_bss) *(.btstack_pool)
        acl_tx_pool = .; acl_tx_pool_end = .; acl_rx_pool = .; acl_rx_pool_end = .;
        tws_bulk_pool = .; tws_bulk_pool_end = .;
        . = ALIGN(4); _bss_end = .;
    } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) *(.debug*) }
}
