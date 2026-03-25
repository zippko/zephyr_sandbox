#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#define FILESYSTEM_WEB_MOUNT_POINT "/lfs"
#define FILESYSTEM_WEB_FS_PATH     FILESYSTEM_WEB_MOUNT_POINT "/www"

int filesystem_service_mount_or_format(void);
int filesystem_service_sync_web_assets(void);

#endif
