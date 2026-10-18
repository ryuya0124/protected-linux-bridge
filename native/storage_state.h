/* MIT. Durable promotion of verified disks; the previous disk stays recoverable. */
#ifndef PLB_STORAGE_STATE_H
#define PLB_STORAGE_STATE_H
#define STORAGE_WORK BASE "/storage-work.qcow2"
#define STORAGE_NEW BASE "/storage-new.qcow2"
#define STORAGE_BACKUP BASE "/storage-previous.qcow2"
#define STORAGE_JOURNAL BASE "/storage-promotion"
static int storage_remove(const char *path) {
    if (!disk_path_exists(path))
        return errno == ENOENT ? 0 : -1;
    return disk_regular(path) ? unlink(path) : -1;
}
/* Caller holds the cross-profile owner lock. Unknown files are never removed. */
static int storage_recover(void) {
    if (disk_path_exists(STORAGE_JOURNAL)) {
        if (!disk_regular(STORAGE_JOURNAL))
            return -1;
        struct resource_settings r;
        if (resources_read(&r))
            return -1;
        r.disk_gib = 0;
        if (resources_save(&r) || disk_dir_sync())
            return -1;
        if (disk_path_exists(STORAGE_BACKUP)) {
            if (!disk_regular(STORAGE_BACKUP))
                return -1;
            if (disk_path_exists(OVERLAY) &&
                (!disk_regular(OVERLAY) || storage_remove(STORAGE_NEW) ||
                 rename(OVERLAY, STORAGE_NEW)))
                return -1;
            if (rename(STORAGE_BACKUP, OVERLAY) || disk_dir_sync())
                return -1;
        } else if (!disk_regular(OVERLAY))
            return -1;
        if (unlink(STORAGE_JOURNAL) || disk_dir_sync())
            return -1;
    }
    /* A lone new/work disk may be the only remaining data: refuse to remove it. */
    if (!disk_regular(OVERLAY))
        return -1;
    return storage_remove(STORAGE_WORK) || storage_remove(STORAGE_NEW) || disk_dir_sync() ? -1 : 0;
}
static int storage_promote(int target_gib) {
    if (!disk_regular(OVERLAY) || !disk_regular(STORAGE_NEW) || disk_path_exists(STORAGE_BACKUP) ||
        disk_path_exists(STORAGE_JOURNAL))
        return -1;
    struct resource_settings r;
    if (resources_read(&r))
        return -1;
    int fd = open(STORAGE_NEW, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    int error = fsync(fd);
    close(fd);
    if (error)
        return -1;
    fd = open(STORAGE_JOURNAL, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    const char marker[] = "PLB_STORAGE_1\n";
    error = plb_write_all(fd, marker, sizeof(marker) - 1) || fsync(fd);
    close(fd);
    if (error || disk_dir_sync() || rename(OVERLAY, STORAGE_BACKUP) || disk_dir_sync() ||
        rename(STORAGE_NEW, OVERLAY) || disk_dir_sync())
        return -1;
    r.disk_gib = target_gib;
    if (resources_save(&r) || disk_dir_sync() || unlink(STORAGE_JOURNAL) || disk_dir_sync())
        return -1;
    return 0;
}
#endif
