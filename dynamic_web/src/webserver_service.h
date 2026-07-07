#ifndef WEBSERVER_SERVICE_H
#define WEBSERVER_SERVICE_H

#include <stddef.h>

struct webserver_status_provider {
	int (*get_ipv4_addr)(char *buf, size_t buf_len);
	const char *(*get_ssid)(void);
	int (*get_cpu_util_percent)(void);
	int (*get_ram_util_percent)(void);
};

int webserver_service_init(const struct webserver_status_provider *provider);
int webserver_service_start(void);

#endif
