/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* The configuration the JieLi SDK's BT config sources (apps/common/config/log_config/lib_btctrler_config.c,
 * apps/common/config/bt_profile_config.c; Apache-2.0) are compiled with for the FM-1 BLE test: BLE only, central
 * (client) role, one link, no classic BT, no TWS, no mesh, no emitter. Replaces the SDK demo's app_config.h. */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#define LIB_DEBUG                       0       /* library logs off */
#define CONFIG_DEBUG_LIB(x)             (x & LIB_DEBUG)
#define TCFG_USER_BLE_ENABLE            1
#define TCFG_USER_BT_CLASSIC_ENABLE     0
#define BT_NET_HID_EN                   0
#define TRANS_DATA_EN                   0
#define BT_NET_CENTRAL_EN               1
#define TRANS_MULTI_BLE_EN              0
#define CONFIG_BLE_MESH_ENABLE          0
#define APP_NONCONN_24G                 0
#define TCFG_USER_EMITTER_ENABLE        0
#define USER_SUPPORT_DUAL_A2DP_SOURCE   0
#define TCFG_BD_NUM                     1
#define TCFG_BLE_SECURITY_EN            0
#define BT_SUPPORT_MUSIC_VOL_SYNC       0
/* (USER_SUPPORT_PROFILE_*: the SDK's bt_profile_cfg.h sets them; they matter only with classic BT) */
#endif
