// SPDX-License-Identifier: Apache-2.0

#include "bluetooth_ctrl.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if __has_include(<bluetooth/services/hids.h>)
#define HAS_NRF_HIDS 1
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/services/hids.h>
#else
#define HAS_NRF_HIDS 0
#endif

LOG_MODULE_REGISTER(bluetooth_ctrl, LOG_LEVEL_INF);

#if HAS_NRF_HIDS
#define OUTPUT_REPORT_MAX_LEN 0
#define INPUT_REP_MEDIA_REF_ID 1
#define INPUT_REPORT_MEDIA_MAX_LEN 2
#define HID_CONSUMER_PLAY 0x00B0
#define HID_CONSUMER_PAUSE 0x00B1

enum {
	INPUT_REP_MEDIA_IDX = 0
};

BT_HIDS_DEF(hids_obj, OUTPUT_REPORT_MAX_LEN, INPUT_REPORT_MEDIA_MAX_LEN);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

struct conn_mode {
	struct bt_conn *conn;
	bool reserved;
};

static struct conn_mode conn_mode[CONFIG_BT_HIDS_MAX_CLIENT_COUNT];
static bool bluetooth_ready;
static bool bluetooth_target_enabled;
static struct k_work bluetooth_setting_work;
static bt_ctrl_show_passkey_cb_t show_passkey_cb;
static bt_ctrl_hide_passkey_cb_t hide_passkey_cb;

static int advertising_start(void)
{
	int err;
	const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
		BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2,
		BT_GAP_ADV_FAST_INT_MAX_2, NULL);

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed (err %d)", err);
		return err;
	}

	LOG_INF("Advertising started");
	return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_WRN("Connect failed to %s (0x%02x)", addr, err);
		return;
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			conn_mode[i].conn = conn;
			conn_mode[i].reserved = false;
			break;
		}
	}

	err = bt_hids_connected(&hids_obj, conn);
	if (err) {
		LOG_WRN("bt_hids_connected failed (err %d)", err);
	}

	LOG_INF("Connected %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;

	err = bt_hids_disconnected(&hids_obj, conn);
	if (err) {
		LOG_WRN("bt_hids_disconnected failed (err %d)", err);
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			conn_mode[i].conn = NULL;
			conn_mode[i].reserved = false;
			break;
		}
	}

	LOG_INF("Disconnected (reason 0x%02x)", reason);
	if (hide_passkey_cb != NULL) {
		hide_passkey_cb();
	}
	if (bluetooth_ready) {
		(void)advertising_start();
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_WRN("Security failed for %s level %u err %d (%s)", addr, level,
			err, bt_security_err_to_str(err));
	} else {
		LOG_INF("Security changed for %s level %u", addr, level);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void hids_pm_evt_handler(enum bt_hids_pm_evt evt, struct bt_conn *conn)
{
	ARG_UNUSED(evt);
	ARG_UNUSED(conn);
}

static void hid_init(void)
{
	int err;
	struct bt_hids_init_param hids_init_obj = { 0 };
	struct bt_hids_inp_rep *hids_inp_rep;
	static const uint8_t report_map[] = {
		0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, INPUT_REP_MEDIA_REF_ID,
		0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
		0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0
	};

	hids_init_obj.rep_map.data = report_map;
	hids_init_obj.rep_map.size = sizeof(report_map);
	hids_init_obj.info.bcd_hid = 0x0101;
	hids_init_obj.info.b_country_code = 0x00;
	hids_init_obj.info.flags = (BT_HIDS_REMOTE_WAKE |
				    BT_HIDS_NORMALLY_CONNECTABLE);

	hids_inp_rep =
		&hids_init_obj.inp_rep_group_init.reports[INPUT_REP_MEDIA_IDX];
	hids_inp_rep->size = INPUT_REPORT_MEDIA_MAX_LEN;
	hids_inp_rep->id = INPUT_REP_MEDIA_REF_ID;
	hids_init_obj.inp_rep_group_init.cnt++;

	hids_init_obj.is_kb = false;
	hids_init_obj.is_mouse = false;
	hids_init_obj.pm_evt_handler = hids_pm_evt_handler;

	err = bt_hids_init(&hids_obj, &hids_init_obj);
	if (err) {
		LOG_ERR("HIDS init failed (err %d)", err);
	} else {
		LOG_INF("HIDS initialized");
	}
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Passkey for %s: %06u", addr, passkey);
	if (show_passkey_cb != NULL) {
		show_passkey_cb(passkey);
	}
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Confirming passkey for %s: %06u", addr, passkey);
	if (show_passkey_cb != NULL) {
		show_passkey_cb(passkey);
	}

	err = bt_conn_auth_passkey_confirm(conn);
	if (err) {
		LOG_WRN("Passkey confirm failed (err %d)", err);
	}
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_WRN("Pairing cancelled: %s", addr);
	if (hide_passkey_cb != NULL) {
		hide_passkey_cb();
	}
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
	if (hide_passkey_cb != NULL) {
		hide_passkey_cb();
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_WRN("Pairing failed: %s, reason %d (%s)", addr, reason,
		bt_security_err_to_str(reason));
	if (hide_passkey_cb != NULL) {
		hide_passkey_cb();
	}
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static int send_consumer_usage(uint16_t usage)
{
	uint8_t report[INPUT_REPORT_MEDIA_MAX_LEN];
	bool has_conn = false;
	int err;

	report[0] = (uint8_t)(usage & 0xFFU);
	report[1] = (uint8_t)(usage >> 8);

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			continue;
		}
		has_conn = true;
		err = bt_hids_inp_rep_send(&hids_obj, conn_mode[i].conn,
					   INPUT_REP_MEDIA_IDX, report,
					   sizeof(report), NULL);
		if (err) {
			return err;
		}
	}

	return has_conn ? 0 : -ENOTCONN;
}

static void apply_bluetooth_enabled(bool enabled)
{
	int err;

	if (enabled) {
		if (bluetooth_ready) {
			return;
		}
		bluetooth_ready = true;
		err = advertising_start();
		if (err) {
			bluetooth_ready = false;
			LOG_WRN("Failed to enable Bluetooth advertising (err %d)", err);
		} else {
			LOG_INF("Bluetooth enabled");
		}
		return;
	}

	if (!bluetooth_ready) {
		return;
	}

	bluetooth_ready = false;
	err = bt_le_adv_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop advertising (err %d)", err);
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			continue;
		}
		err = bt_conn_disconnect(conn_mode[i].conn,
					 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err) {
			LOG_WRN("Failed to disconnect peer (err %d)", err);
		}
	}

	LOG_INF("Bluetooth disabled");
}

static void bluetooth_setting_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	apply_bluetooth_enabled(bluetooth_target_enabled);
}
#endif /* HAS_NRF_HIDS */

void bt_ctrl_init(bt_ctrl_show_passkey_cb_t show_cb,
		  bt_ctrl_hide_passkey_cb_t hide_cb)
{
#if HAS_NRF_HIDS
	show_passkey_cb = show_cb;
	hide_passkey_cb = hide_cb;
	k_work_init(&bluetooth_setting_work, bluetooth_setting_work_handler);
	bluetooth_target_enabled = true;
#else
	ARG_UNUSED(show_cb);
	ARG_UNUSED(hide_cb);
#endif
}

int bt_ctrl_enable_stack_and_start(void)
{
#if HAS_NRF_HIDS
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register auth callbacks (err %d)", err);
		return err;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		LOG_ERR("Failed to register auth info callbacks (err %d)", err);
		return err;
	}

	hid_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	bluetooth_ready = true;
	bluetooth_target_enabled = true;
	LOG_INF("Bluetooth initialized");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = advertising_start();
	if (err) {
		LOG_WRN("Initial advertising start failed (err %d)", err);
	}
	return err;
#else
	LOG_WRN("BLE HIDS is unavailable in this Zephyr workspace");
	return -ENOTSUP;
#endif
}

void bt_ctrl_request_enabled(bool enabled)
{
#if HAS_NRF_HIDS
	bluetooth_target_enabled = enabled;
	k_work_submit(&bluetooth_setting_work);
#else
	ARG_UNUSED(enabled);
#endif
}

bool bt_ctrl_is_ready(void)
{
#if HAS_NRF_HIDS
	return bluetooth_ready;
#else
	return false;
#endif
}

bool bt_ctrl_is_connected(void)
{
#if HAS_NRF_HIDS
	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn != NULL) {
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}

int bt_ctrl_send_play_pause(bool play)
{
#if HAS_NRF_HIDS
	uint16_t usage = play ? HID_CONSUMER_PLAY : HID_CONSUMER_PAUSE;
	int err;

	err = send_consumer_usage(usage);
	if (err && err != -ENOTCONN) {
		return err;
	}

	err = send_consumer_usage(0U);
	if (err && err != -ENOTCONN) {
		return err;
	}

	return 0;
#else
	ARG_UNUSED(play);
	return -ENOTSUP;
#endif
}

int bt_ctrl_send_usage(uint16_t usage)
{
#if HAS_NRF_HIDS
	int err;

	err = send_consumer_usage(usage);
	if (err && err != -ENOTCONN) {
		return err;
	}
	err = send_consumer_usage(0U);
	if (err && err != -ENOTCONN) {
		return err;
	}
	return 0;
#else
	ARG_UNUSED(usage);
	return -ENOTSUP;
#endif
}
