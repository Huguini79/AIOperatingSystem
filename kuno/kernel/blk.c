#include "kernel.h"
static int ram_read(struct blkdev *d, u64 lba, u32 n, void *buf) {
    if (lba + n > d->nsec) return -1;
    memcpy(buf, (u8 *)d->priv + lba * d->secsz, (size_t)n * d->secsz);
    return 0;
}
void ramdisk_init(struct blkdev *d, void *base, u64 bytes) {
    d->name = "ram0"; d->secsz = 512; d->nsec = bytes / 512; d->priv = base; d->read = ram_read;
}
