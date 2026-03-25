#include "wifi_service.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "wifi_secrets.h"

LOG_MODULE_REGISTER(wifi_service, LOG_LEVEL_INF);

static struct net_if *wifi_iface;
static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback dhcp_mgmt_cb;
static int wifi_connect_result = -EAGAIN;
static bool reconnect_requested;
static bool wifi_ready;
static int64_t last_reconnect_attempt;
static struct wifi_connect_req_params wifi_params;

K_SEM_DEFINE(wifi_connected_sem, 0, 1);
K_SEM_DEFINE(ipv4_addr_sem, 0, 1);

int wifi_service_get_ipv4_addr(char *buf, size_t buf_len)
{
	struct in_addr *addr;

	if (wifi_iface == NULL) {
		return -ENODEV;
	}

	addr = net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED);
	if (addr == NULL) {
		return -EADDRNOTAVAIL;
	}

	if (net_addr_ntop(AF_INET, addr, buf, buf_len) == NULL) {
		return -EINVAL;
	}

	return 0;
}

const char *wifi_service_get_ssid(void)
{
	return WIFI_SSID;
}

static void wifi_mgmt_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			      struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status != NULL) {
			wifi_connect_result = status->status;
		} else {
			wifi_connect_result = -EIO;
		}

		if (wifi_connect_result == 0) {
			LOG_INF("Wi-Fi connected to SSID: %s", WIFI_SSID);
			reconnect_requested = false;
			wifi_ready = false;
			net_dhcpv4_start(wifi_iface);
		} else {
			LOG_ERR("Wi-Fi connection failed (%d)", wifi_connect_result);
			reconnect_requested = true;
		}
		k_sem_give(&wifi_connected_sem);
		return;
	}

	if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected, scheduling reconnect");
		reconnect_requested = true;
		wifi_ready = false;
	}
}

static void dhcp_mgmt_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			      struct net_if *iface)
{
	char ip[NET_IPV4_ADDR_LEN] = { 0 };

	ARG_UNUSED(cb);

	if (iface != wifi_iface) {
		return;
	}

	if ((mgmt_event & NET_EVENT_IPV4_DHCP_BOUND) || (mgmt_event & NET_EVENT_IPV4_ADDR_ADD)) {
		if (wifi_service_get_ipv4_addr(ip, sizeof(ip)) == 0) {
			LOG_INF("IPv4 ready: %s", ip);
		}
		wifi_ready = true;
		reconnect_requested = false;
		if (k_sem_count_get(&ipv4_addr_sem) == 0) {
			k_sem_give(&ipv4_addr_sem);
		}
	}
}

static int request_wifi_connect(void)
{
	int ret;

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &wifi_params, sizeof(wifi_params));
	if (ret == -EALREADY) {
		LOG_INF("Wi-Fi connect request already active");
		return 0;
	}

	if (ret < 0) {
		LOG_ERR("NET_REQUEST_WIFI_CONNECT failed (%d)", ret);
	}

	return ret;
}

static int connect_to_wifi(void)
{
	int ret;
	int attempt;
	const size_t ssid_len = strlen(WIFI_SSID);
	const size_t psk_len = strlen(WIFI_PSK);

	if (ssid_len == 0U) {
		LOG_ERR("WIFI_SSID is empty in src/wifi_secrets.h");
		return -EINVAL;
	}

	memset(&wifi_params, 0, sizeof(wifi_params));
	wifi_params.ssid = (const uint8_t *)WIFI_SSID;
	wifi_params.ssid_length = ssid_len;
	wifi_params.channel = WIFI_CHANNEL_ANY;
	wifi_params.band = WIFI_FREQ_BAND_2_4_GHZ;
	if (psk_len == 0U) {
		wifi_params.security = WIFI_SECURITY_TYPE_NONE;
		wifi_params.psk = NULL;
		wifi_params.psk_length = 0U;
	} else {
		wifi_params.security = WIFI_SECURITY_TYPE_PSK;
		wifi_params.psk = (const uint8_t *)WIFI_PSK;
		wifi_params.psk_length = psk_len;
	}

	for (attempt = 1; attempt <= 5; attempt++) {
		wifi_connect_result = -EAGAIN;
		wifi_ready = false;
		k_sem_reset(&wifi_connected_sem);
		k_sem_reset(&ipv4_addr_sem);

		LOG_INF("Connecting to Wi-Fi SSID: %s (attempt %d/5)", WIFI_SSID, attempt);
		ret = request_wifi_connect();
		if (ret < 0) {
			k_sleep(K_SECONDS(2));
			continue;
		}

		if (wifi_service_get_ipv4_addr((char[NET_IPV4_ADDR_LEN]){0}, NET_IPV4_ADDR_LEN) == 0) {
			wifi_connect_result = 0;
			wifi_ready = true;
			return 0;
		}

		ret = k_sem_take(&wifi_connected_sem, K_SECONDS(20));
		if (ret < 0) {
			LOG_ERR("Timed out waiting for Wi-Fi connect result");
			k_sleep(K_SECONDS(2));
			continue;
		}

		if (wifi_connect_result != 0) {
			LOG_ERR("Wi-Fi association/authentication failed (%d)", wifi_connect_result);
			k_sleep(K_SECONDS(2));
			continue;
		}

		LOG_INF("Waiting for DHCP IPv4 lease");
		ret = k_sem_take(&ipv4_addr_sem, K_SECONDS(30));
		if (ret == 0 && wifi_ready) {
			return 0;
		}

		if (wifi_service_get_ipv4_addr((char[NET_IPV4_ADDR_LEN]){0}, NET_IPV4_ADDR_LEN) == 0) {
			wifi_ready = true;
			return 0;
		}

		LOG_ERR("Timed out waiting for IPv4 address");
		k_sleep(K_SECONDS(2));
	}

	return -ETIMEDOUT;
}

int wifi_service_init_and_connect(void)
{
	int ret;

	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		wifi_iface = net_if_get_default();
	}

	if (wifi_iface == NULL) {
		LOG_ERR("No Wi-Fi network interface found");
		return -ENODEV;
	}

	if (!net_if_is_wifi(wifi_iface)) {
		LOG_ERR("Selected default interface is not a Wi-Fi interface");
		return -ENODEV;
	}

	if (!net_if_is_up(wifi_iface)) {
		ret = net_if_up(wifi_iface);
		if ((ret < 0) && (ret != -EALREADY)) {
			LOG_ERR("Failed to bring Wi-Fi interface up (%d)", ret);
			return ret;
		}
	}

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	net_mgmt_init_event_callback(&dhcp_mgmt_cb, dhcp_mgmt_handler,
				     NET_EVENT_IPV4_DHCP_BOUND | NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&dhcp_mgmt_cb);

	return connect_to_wifi();
}

void wifi_service_process(void)
{
	int64_t now_ms = k_uptime_get();

	if (!reconnect_requested) {
		return;
	}

	if ((now_ms - last_reconnect_attempt) < 3000) {
		return;
	}

	LOG_INF("Attempting Wi-Fi reconnect");
	(void)request_wifi_connect();
	last_reconnect_attempt = now_ms;
}
