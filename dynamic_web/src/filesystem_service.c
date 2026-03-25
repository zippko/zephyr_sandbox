#include "filesystem_service.h"

#include <errno.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(filesystem_service, LOG_LEVEL_INF);

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t web_fs_mount = {
	.type = FS_LITTLEFS,
	.mnt_point = FILESYSTEM_WEB_MOUNT_POINT,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.fs_data = &storage,
};

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

struct web_asset {
	const char *path;
	const uint8_t *data;
	size_t len;
};

static const struct web_asset web_assets[] = {
	{ FILESYSTEM_WEB_FS_PATH "/index.html", web_index_html, sizeof(web_index_html) },
	{ FILESYSTEM_WEB_FS_PATH "/styles.css", web_styles_css, sizeof(web_styles_css) },
	{ FILESYSTEM_WEB_FS_PATH "/app.js", web_app_js, sizeof(web_app_js) },
	{ FILESYSTEM_WEB_FS_PATH "/vendor/bootstrap/css/bootstrap.min.css.gz", web_bootstrap_min_css_gz,
	  sizeof(web_bootstrap_min_css_gz) },
	{ FILESYSTEM_WEB_FS_PATH "/vendor/bootstrap/js/bootstrap.bundle.min.js.gz",
	  web_bootstrap_bundle_min_js_gz, sizeof(web_bootstrap_bundle_min_js_gz) },
};

static int write_asset_to_fs(const struct web_asset *asset)
{
	struct fs_file_t file;
	ssize_t bytes_written;
	int ret;

	fs_file_t_init(&file);

	ret = fs_open(&file, asset->path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		LOG_ERR("Failed to open %s (%d)", asset->path, ret);
		return ret;
	}

	bytes_written = fs_write(&file, asset->data, asset->len);
	if (bytes_written < 0) {
		LOG_ERR("Write failed for %s (%d)", asset->path, (int)bytes_written);
		(void)fs_close(&file);
		return (int)bytes_written;
	}

	if ((size_t)bytes_written != asset->len) {
		LOG_ERR("Short write for %s (%d/%d)", asset->path, (int)bytes_written,
			(int)asset->len);
		(void)fs_close(&file);
		return -EIO;
	}

	ret = fs_close(&file);
	if (ret < 0) {
		LOG_ERR("Failed to close %s (%d)", asset->path, ret);
		return ret;
	}

	return 0;
}

static int ensure_directory(const char *path)
{
	struct fs_dirent entry;
	int ret;

	ret = fs_stat(path, &entry);
	if (ret < 0) {
		ret = fs_mkdir(path);
		if (ret < 0) {
			LOG_ERR("Failed to create %s (%d)", path, ret);
			return ret;
		}
		return 0;
	}

	if (entry.type != FS_DIR_ENTRY_DIR) {
		LOG_ERR("%s exists but is not a directory", path);
		return -ENOTDIR;
	}

	return 0;
}

int filesystem_service_mount_or_format(void)
{
	int ret;

	ret = fs_mount(&web_fs_mount);
	if (ret == 0) {
		LOG_INF("Mounted LittleFS at %s", FILESYSTEM_WEB_MOUNT_POINT);
		return 0;
	}

	LOG_WRN("Mount failed (%d), formatting storage partition", ret);
	ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)web_fs_mount.storage_dev, web_fs_mount.fs_data, 0);
	if (ret < 0) {
		LOG_ERR("LittleFS format failed (%d)", ret);
		return ret;
	}

	ret = fs_mount(&web_fs_mount);
	if (ret < 0) {
		LOG_ERR("Mount after format failed (%d)", ret);
		return ret;
	}

	LOG_INF("Mounted formatted LittleFS at %s", FILESYSTEM_WEB_MOUNT_POINT);
	return 0;
}

int filesystem_service_sync_web_assets(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_APP_SYNC_WEB_FILES_ON_BOOT)) {
		LOG_INF("Web asset sync disabled (CONFIG_APP_SYNC_WEB_FILES_ON_BOOT=n)");
		return 0;
	}

	ret = ensure_directory(FILESYSTEM_WEB_FS_PATH);
	if (ret < 0) {
		return ret;
	}

	ret = ensure_directory(FILESYSTEM_WEB_FS_PATH "/vendor");
	if (ret < 0) {
		return ret;
	}

	ret = ensure_directory(FILESYSTEM_WEB_FS_PATH "/vendor/bootstrap");
	if (ret < 0) {
		return ret;
	}

	ret = ensure_directory(FILESYSTEM_WEB_FS_PATH "/vendor/bootstrap/css");
	if (ret < 0) {
		return ret;
	}

	ret = ensure_directory(FILESYSTEM_WEB_FS_PATH "/vendor/bootstrap/js");
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < ARRAY_SIZE(web_assets); i++) {
		ret = write_asset_to_fs(&web_assets[i]);
		if (ret < 0) {
			return ret;
		}
	}

	LOG_INF("Web assets synced to %s", FILESYSTEM_WEB_FS_PATH);
	return 0;
}
