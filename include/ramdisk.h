#ifndef _RAMDISK_H
#define _RAMDISK_H

/* Ramdisk memory area descriptor */
struct ramdisk_desc {
	char *buf;
	int len;
};

void *ramdisk_memory_area_end(void);
struct ramdisk_desc ramdisk_get_desc(void);
void ramdisk_init(void);

#endif /* _RAMDISK_H */
