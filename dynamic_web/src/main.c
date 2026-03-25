#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>

#include "app_utils.h"
#include "filesystem_service.h"
#include "webserver_service.h"
#include "wifi_service.h"

LOG_MODULE_REGISTER(dynamic_web, LOG_LEVEL_INF);

#if defined(CONFIG_BOARD_M5STACK_CORE2_ESP32_APPCPU)
#error "This app requires Wi-Fi and must be built for m5stack_core2/esp32/procpu (not appcpu)."
#endif

int main(void)
{
	int ret;
	char ip[NET_IPV4_ADDR_LEN] = "0.0.0.0";
	struct webserver_status_provider provider = {
		.get_ipv4_addr = wifi_service_get_ipv4_addr,
		.get_ssid = wifi_service_get_ssid,
		.get_cpu_util_percent = app_utils_get_cpu_util_percent,
		.get_ram_util_percent = app_utils_get_ram_util_percent,
	};

	ret = wifi_service_init_and_connect();
	if (ret < 0) {
		return 0;
	}

#if defined(CONFIG_APP_WEB_CONTENT_FROM_FILESYSTEM)
	ret = filesystem_service_mount_or_format();
	if (ret < 0) {
		return 0;
	}

	ret = filesystem_service_sync_web_assets();
	if (ret < 0) {
		return 0;
	}
#endif

	ret = webserver_service_init(&provider);
	if (ret < 0) {
		LOG_ERR("Failed to init webserver service (%d)", ret);
		return 0;
	}

	ret = webserver_service_start();
	if (ret < 0) {
		LOG_ERR("Failed to start HTTP server (%d)", ret);
		return 0;
	}

	(void)wifi_service_get_ipv4_addr(ip, sizeof(ip));
	LOG_INF("HTTP server running at: http://%s/", ip);

	while (1) {
		wifi_service_process();
		k_sleep(K_MSEC(500));
	}
}
