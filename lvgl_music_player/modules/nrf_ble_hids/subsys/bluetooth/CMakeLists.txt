# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

zephyr_sources_ifdef(CONFIG_BT_GATT_POOL gatt_pool.c)
zephyr_sources_ifdef(CONFIG_BT_CONN_CTX conn_ctx.c)
add_subdirectory(services)
