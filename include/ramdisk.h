#ifndef _RAMDISK_H
#define _RAMDISK_H

int ramdisk_get_len(void);
char *ramdisk_get_buf(void);
void *ramdisk_memory_area_end(void);

void ramdisk_init(void);

#endif /* _RAMDISK_H */
