/* dristat.c -- 
 * Created: Mon Jan 15 05:05:07 2001 by faith@acm.org
 *
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * Authors: Rickard E. (Rik) Faith <faith@valinux.com>
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../../xf86drm.h"
#include "../xf86drmRandom.c"
#include "../xf86drmHash.c"
#include "../xf86drm.c"

#define DRM_DIR_NAME "/dev/dri"
#define DRM_DEV_NAME "%s/card%d"

#define DRM_VERSION 0x00000001
#define DRM_MEMORY  0x00000002
#define DRM_CLIENTS 0x00000004

static void getversion(int fd)
{
    drmVersionPtr version;
    
    version = drmGetVersion(fd);
    if (version) {
	printf("  Version information:\n");
	printf("    Name: %s\n", version->name ? version->name : "?");
	printf("    Version: %d.%d.%d\n",
	       version->version_major,
	       version->version_minor,
	       version->version_patchlevel);
	printf("    Date: %s\n", version->date ? version->date : "?");
	printf("    Desc: %s\n", version->desc ? version->desc : "?");
	drmFreeVersion(version);
    } else {
	printf("  No version information available\n");
    }
}

typedef struct {
    unsigned long	offset;	 /* Requested physical address (0 for SAREA)*/
    unsigned long	size;	 /* Requested physical size (bytes)	    */
    drm_map_type_t	type;	 /* Type of memory to map		    */
    drm_map_flags_t flags;	 /* Flags				    */
    void		*handle; /* User-space: "Handle" to pass to mmap    */
    /* Kernel-space: kernel-virtual address    */
    int		mtrr;	 /* MTRR slot used			    */
				 /* Private data			    */
} drmVmRec, *drmVmPtr;

int drmGetMap(int fd, int idx, drmHandle *offset, drmSize *size,
	      drmMapType *type, drmMapFlags *flags, drmHandle *handle,
	      int *mtrr)
{
    drm_map_t map;

    map.offset = idx;
    if (ioctl(fd, DRM_IOCTL_GET_MAP, &map)) return -errno;
    *offset = map.offset;
    *size   = map.size;
    *type   = map.type;
    *flags  = map.flags;
    *handle = (unsigned long)map.handle;
    *mtrr   = map.mtrr;
    return 0;
}

int drmGetClient(int fd, int idx, int *auth, int *pid, int *uid,
		 unsigned long *magic, unsigned long *iocs)
{
    drm_client_t client;

    client.idx = idx;
    if (ioctl(fd, DRM_IOCTL_GET_CLIENT, &client)) return -errno;
    *auth      = client.auth;
    *pid       = client.pid;
    *uid       = client.uid;
    *magic     = client.magic;
    *iocs      = client.iocs;
    return 0;
}

static void getvm(int fd)
{
    int             i;
    const char      *typename;
    char            flagname[33];
    drmHandle       offset;
    drmSize         size;
    drmMapType      type;
    drmMapFlags     flags;
    drmHandle       handle;
    int             mtrr;

    printf("  VM map information:\n");
    printf("    slot     offset       size type flags    address mtrr\n");

    for (i = 0;
	 !drmGetMap(fd, i, &offset, &size, &type, &flags, &handle, &mtrr);
	 i++) {
	
	switch (type) {
	case DRM_FRAME_BUFFER: typename = "FB";  break;
	case DRM_REGISTERS:    typename = "REG"; break;
	case DRM_SHM:          typename = "SHM"; break;
	case DRM_AGP:          typename = "AGP"; break;
	default:               typename = "???"; break;
	}

	flagname[0] = (flags & DRM_RESTRICTED)      ? 'R' : ' ';
	flagname[1] = (flags & DRM_READ_ONLY)       ? 'r' : 'w';
	flagname[2] = (flags & DRM_LOCKED)          ? 'l' : ' ';
	flagname[3] = (flags & DRM_KERNEL)          ? 'k' : ' ';
	flagname[4] = (flags & DRM_WRITE_COMBINING) ? 'W' : ' ';
	flagname[5] = (flags & DRM_CONTAINS_LOCK)   ? 'L' : ' ';
	flagname[6] = '\0';
	
	printf("    %4d 0x%08lx 0x%08lx %3.3s %6.6s 0x%08lx ",
	       i, offset, (unsigned long)size, typename, flagname, handle);
	if (mtrr < 0) printf("none\n");
	else          printf("%4d\n", mtrr);
    }
}

static void getclients(int fd)
{
    int           i;
    int           auth;
    int           pid;
    int           uid;
    unsigned long magic;
    unsigned long iocs;
    char          buf[64];
    char          cmd[40];
    int           procfd;
    
    printf("  DRI client information:\n");
    printf("    a   pid    uid      magic     ioctls  prog\n");

    for (i = 0; !drmGetClient(fd, i, &auth, &pid, &uid, &magic, &iocs); i++) {
	sprintf(buf, "/proc/%d/cmdline", pid);
	memset(cmd, sizeof(cmd), 0);
	if ((procfd = open(buf, O_RDONLY, 0)) >= 0) {
	    read(procfd, cmd, sizeof(cmd)-1);
	    close(procfd);
	}
	if (*cmd)
	    printf("    %c %5d %5d %10lu %10lu   %s\n",
		   auth ? 'y' : 'n', pid, uid, magic, iocs, cmd);
	else
	    printf("    %c %5d %5d %10lu %10lu\n",
		   auth ? 'y' : 'n', pid, uid, magic, iocs);
    }
}

static int drmOpenMinor(int minor, uid_t user, gid_t group,
			mode_t dirmode, mode_t devmode, int force)
{
    struct stat st;
    char        buf[64];
    long        dev    = makedev(DRM_MAJOR, minor);
    int         setdir = 0;
    int         setdev = 0;
    int         fd;

    if (stat(DRM_DIR_NAME, &st) || !S_ISDIR(st.st_mode)) {
	remove(DRM_DIR_NAME);
	mkdir(DRM_DIR_NAME, dirmode);
	++setdir;
    }

    if (force || setdir) {
	chown(DRM_DIR_NAME, user, group);
	chmod(DRM_DIR_NAME, dirmode);
    }

    sprintf(buf, DRM_DEV_NAME, DRM_DIR_NAME, minor);
    if (stat(buf, &st) || st.st_rdev != dev) {
	remove(buf);
	mknod(buf, S_IFCHR, dev);
	++setdev;
    }

    if (force || setdev) {
	chown(buf, user, group);
	chmod(buf, devmode);
    }

    if ((fd = open(buf, O_RDWR, 0)) >= 0) return fd;
    if (setdev) remove(buf);
    return -errno;
}
	
int main(int argc, char **argv)
{
    int  c;
    int  mask  = 0;
    int  minor = 0;
    int  fd;
    char buf[64];
    int  i;

    while ((c = getopt(argc, argv, "vmcM:")) != EOF)
	switch (c) {
	case 'v': mask |= DRM_VERSION;             break;
	case 'm': mask |= DRM_MEMORY;              break;
	case 'c': mask |= DRM_CLIENTS;             break;
	case 'M': minor = strtol(optarg, NULL, 0); break;
	default:
	    fprintf( stderr, "Usage: dristat [options]\n" );
	    return 1;
	}

    for (i = 0; i < 16; i++) if (!minor || i == minor) {
	sprintf(buf, DRM_DEV_NAME, DRM_DIR_NAME, i);
	fd = drmOpenMinor(i, 0, 0, 0700, 0600, 0);
	if (fd >= 0) {
	    printf("%s\n", buf);
	    if (mask & DRM_VERSION) getversion(fd);
	    if (mask & DRM_MEMORY)  getvm(fd);
	    if (mask & DRM_CLIENTS) getclients(fd);
	    close(fd);
	}
    }

    return 0; 
}
