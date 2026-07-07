#include "webserver_service.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>

#include "filesystem_service.h"

LOG_MODULE_REGISTER(webserver_service, LOG_LEVEL_INF);

static struct webserver_status_provider status_provider;
static uint16_t http_port = 80;

static int api_status_handler(struct http_client_ctx *client, enum http_data_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t payload[320];
	char ip[NET_IPV4_ADDR_LEN] = "0.0.0.0";
	const char *ssid = "unknown";
	int cpu_load_percent = -1;
	int ram_util_percent = -1;
	int len;

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		return 0;
	}

	if (status == HTTP_SERVER_DATA_FINAL) {
		if (status_provider.get_ipv4_addr != NULL) {
			(void)status_provider.get_ipv4_addr(ip, sizeof(ip));
		}

		if (status_provider.get_ssid != NULL) {
			ssid = status_provider.get_ssid();
		}

		if (status_provider.get_cpu_util_percent != NULL) {
			cpu_load_percent = status_provider.get_cpu_util_percent();
		}

		if (status_provider.get_ram_util_percent != NULL) {
			ram_util_percent = status_provider.get_ram_util_percent();
		}

		len = snprintk((char *)payload, sizeof(payload),
			       "{\"uptime_ms\":%lld,\"ip\":\"%s\",\"ssid\":\"%s\",\"cpu_load_percent\":%d,\"ram_util_percent\":%d}",
			       (long long)k_uptime_get(), ip, ssid, cpu_load_percent, ram_util_percent);
		if (len < 0) {
			return len;
		}

		if (len >= (int)sizeof(payload)) {
			len = sizeof(payload) - 1;
		}

		response_ctx->body = payload;
		response_ctx->body_len = (size_t)len;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_dynamic api_status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = api_status_handler,
	.user_data = NULL,
};

#if defined(CONFIG_APP_WEB_CONTENT_FROM_FILESYSTEM)
static struct http_resource_detail_static_fs web_fs_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC_FS,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.fs_path = FILESYSTEM_WEB_FS_PATH,
};
#else
static const uint8_t web_index_html[] = {
#include "web_index_html.inc"
};

static const uint8_t web_styles_css[] = {
#include "web_styles_css.inc"
};

static const uint8_t web_app_js[] = {
#include "web_app_js.inc"
};

static const uint8_t web_bootstrap_min_css_gz[] = {
#include "web_bootstrap_min_css_gz.inc"
};

static const uint8_t web_bootstrap_bundle_min_js_gz[] = {
#include "web_bootstrap_bundle_min_js_gz.inc"
};

static struct http_resource_detail_static web_index_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/html",
	},
	.static_data = web_index_html,
	.static_data_len = sizeof(web_index_html),
};

static struct http_resource_detail_static web_styles_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/css",
	},
	.static_data = web_styles_css,
	.static_data_len = sizeof(web_styles_css),
};

static struct http_resource_detail_static web_app_js_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/javascript",
	},
	.static_data = web_app_js,
	.static_data_len = sizeof(web_app_js),
};

static struct http_resource_detail_static web_bootstrap_css_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/css",
		.content_encoding = "gzip",
	},
	.static_data = web_bootstrap_min_css_gz,
	.static_data_len = sizeof(web_bootstrap_min_css_gz),
};

static struct http_resource_detail_static web_bootstrap_js_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/javascript",
		.content_encoding = "gzip",
	},
	.static_data = web_bootstrap_bundle_min_js_gz,
	.static_data_len = sizeof(web_bootstrap_bundle_min_js_gz),
};
#endif

HTTP_SERVICE_DEFINE(web_http_service, NULL, &http_port, 4, 8, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(api_status_resource, web_http_service, "/api/status", &api_status_detail);
#if defined(CONFIG_APP_WEB_CONTENT_FROM_FILESYSTEM)
HTTP_RESOURCE_DEFINE(web_fs_resource, web_http_service, "/*", &web_fs_detail);
#else
HTTP_RESOURCE_DEFINE(web_index_resource, web_http_service, "/", &web_index_detail);
HTTP_RESOURCE_DEFINE(web_styles_resource, web_http_service, "/styles.css", &web_styles_detail);
HTTP_RESOURCE_DEFINE(web_app_js_resource, web_http_service, "/app.js", &web_app_js_detail);
HTTP_RESOURCE_DEFINE(web_bootstrap_css_resource, web_http_service,
		     "/vendor/bootstrap/css/bootstrap.min.css", &web_bootstrap_css_detail);
HTTP_RESOURCE_DEFINE(web_bootstrap_js_resource, web_http_service,
		     "/vendor/bootstrap/js/bootstrap.bundle.min.js", &web_bootstrap_js_detail);
#endif

int webserver_service_init(const struct webserver_status_provider *provider)
{
	if (provider == NULL) {
		return -EINVAL;
	}

	status_provider = *provider;
	return 0;
}

int webserver_service_start(void)
{
	return http_server_start();
}
