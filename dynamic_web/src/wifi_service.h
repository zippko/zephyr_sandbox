#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stddef.h>

int wifi_service_init_and_connect(void);
void wifi_service_process(void);
int wifi_service_get_ipv4_addr(char *buf, size_t buf_len);
const char *wifi_service_get_ssid(void);

#endif
