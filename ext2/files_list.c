/*			-*- mode: Fundamental; -*-
 *
 * List of file paths for testing the file system driver ('UNIX path' to inode)
 * pathname translation code.  Put as much file pathes as you like here!
 *
 * You can build a comprehensive list by going to FS root, then typing:
 *	$ find -type f -o -type d | sed -e 's/^\.//' -e 's/^/\t"/' -e 's/$/",/'
 */

#include <kernel.h>

const char *ext2_files_list[] = {
	NULL,				/* DO NOT REMOVE - End of List mark */
};
