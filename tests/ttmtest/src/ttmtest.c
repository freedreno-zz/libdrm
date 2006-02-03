
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <drm/drm.h>
#include "xf86dri.h"
#include "xf86drm.h"
#include "stdio.h"
#include "sys/types.h"
#include <unistd.h>
#include <string.h>
#include "sys/mman.h"

typedef struct
{
    enum
    {
	haveNothing,
	haveDisplay,
	haveConnection,
	haveDriverName,
	haveDeviceInfo,
	haveDRM,
	haveContext
    }
    state;

    Display *display;
    int screen;
    drm_handle_t sAreaOffset;
    char *curBusID;
    char *driverName;
    int drmFD;
    XVisualInfo visualInfo;
    XID id;
    drm_context_t hwContext;
    void *driPriv;
    int driPrivSize;
    int fbSize;
    int fbOrigin;
    int fbStride;
    drm_handle_t fbHandle;
    int ddxDriverMajor;
    int ddxDriverMinor;
    int ddxDriverPatch;
} TinyDRIContext;

#ifndef __x86_64__
static unsigned
fastrdtsc(void)
{
    unsigned eax;
    __asm__ volatile ("\t"
	"pushl  %%ebx\n\t"
	"cpuid\n\t" ".byte 0x0f, 0x31\n\t" "popl %%ebx\n":"=a" (eax)
	:"0"(0)
	:"ecx", "edx", "cc");

    return eax;
}
#else
static unsigned
fastrdtsc(void)
{
    unsigned eax;
    __asm__ volatile ("\t"
	"cpuid\n\t" ".byte 0x0f, 0x31\n\t" :"=a" (eax)
	:"0"(0)
		      :"ecx", "edx", "ebx", "cc");

    return eax;
}
#endif

static unsigned
time_diff(unsigned t, unsigned t2)
{
    return ((t < t2) ? t2 - t : 0xFFFFFFFFU - (t - t2 - 1));
}

static int
releaseContext(TinyDRIContext * ctx)
{
    switch (ctx->state) {
    case haveContext:
	uniDRIDestroyContext(ctx->display, ctx->screen, ctx->id);
    case haveDRM:
	drmClose(ctx->drmFD);
    case haveDeviceInfo:
	XFree(ctx->driPriv);
    case haveDriverName:
	XFree(ctx->driverName);
    case haveConnection:
	XFree(ctx->curBusID);
	uniDRICloseConnection(ctx->display, ctx->screen);
    case haveDisplay:
	XCloseDisplay(ctx->display);
    default:
	break;
    }
    return -1;
}

#define TTMSIZE (100*1024*1024)
#define BINDPAGES (512)
#define USESIZE (512 * 1024)

static void
testAGP(TinyDRIContext * ctx)
{

    drm_handle_t ttmHandle;
    drmAddress ttmAddress;
    drmAddress agpAddress;
    pid_t pid;
    drm_ttm_arg_t arg;
    drm_ttm_buf_arg_t buf_arg;
    int i, j;
    unsigned t1, t2;
    unsigned a;
    int bufoffs;

    arg.op = ttm_add;
    arg.size = TTMSIZE;
    arg.num_bufs = 0;
    arg.first = NULL;
    t1 = fastrdtsc();
    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
	perror("We were not allowed to allocate");
	return;
    }
    t2 = fastrdtsc();
    printf("Creating took %u clocks\n", time_diff(t1, t2));

    ttmHandle = arg.handle;
    if (drmMap(ctx->drmFD, ttmHandle, TTMSIZE, &ttmAddress) == 0) {
	arg.num_bufs = 1;
	arg.first = &buf_arg;
	buf_arg.ttm_page_offset = 0;
	for (i = 0; i < 5; ++i) {
	    arg.op = ttm_bufs;
	    buf_arg.op = ttm_validate;
	    buf_arg.ttm_page_offset += BINDPAGES;
	    buf_arg.num_pages = BINDPAGES;
	    buf_arg.next = NULL;
	    buf_arg.ttm_handle = arg.handle;
	    buf_arg.flags = DRM_TTM_FLAG_NEW | DRM_TTM_FLAG_CACHED;
	    bufoffs = buf_arg.ttm_page_offset * 1024;

	    /*
	     * Bind at 128 MB into AGP aperture.
	     */

	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)
		    [j + bufoffs];
	    }

	    t1 = fastrdtsc();
	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)[j + bufoffs];
	    }
	    t2 = fastrdtsc();
	    printf("Non page-faulting cached read took %u clocks\n",
		time_diff(t1, t2));

	    drmGetLock(ctx->drmFD, ctx->hwContext, 0);

	    t1 = fastrdtsc();
	    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
		perror("Could not bind.");
	    } else {
		printf("Bound region is %d\n", buf_arg.region_handle);
		printf("Aperture offset is 0x%x\n", buf_arg.aper_offset);
	    }
	    t2 = fastrdtsc();

	    drmUnlock(ctx->drmFD, ctx->hwContext);
	    printf("Binding took %u clocks\n", time_diff(t1, t2));

	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)[j + bufoffs];
	    }
	    t1 = fastrdtsc();
	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)[j + bufoffs];
	    }
	    t2 = fastrdtsc();
	    printf("Uncached read took %u clocks\n", time_diff(t1, t2));

#if 0
	    /*
	     * Change to read-only.
	     */
	    if (mprotect(ttmAddress, TTMSIZE, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "Memprotect failed\n");
	    }
	    if (mprotect(ttmAddress, TTMSIZE, PROT_WRITE)) {
		fprintf(stderr, "Memprotect failed\n");
	    }
	    if (mprotect(ttmAddress, TTMSIZE, PROT_READ)) {
		fprintf(stderr, "Memprotect failed\n");
	    }
	    t1 = fastrdtsc();
	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)[j + bufoffs];
	    }
	    t2 = fastrdtsc();

	    printf("Mprotected read took %u clocks\n", time_diff(t1, t2));
#endif

	    drmGetLock(ctx->drmFD, ctx->hwContext, 0);

	    t1 = fastrdtsc();
	    buf_arg.op = ttm_evict;
	    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
		perror("Could not evict.");
	    }
	    t2 = fastrdtsc();
	    printf("Evict took %u clocks.\n", time_diff(t1, t2));
	    t1 = fastrdtsc();
	    buf_arg.op = ttm_validate;
	    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
		perror("Could not Rebind.");
	    }
	    t2 = fastrdtsc();
	    printf("Rebind took %u clocks.\n", time_diff(t1, t2));
	    printf("Aperture offset is 0x%x\n", buf_arg.aper_offset);

	    t1 = fastrdtsc();
	    buf_arg.op = ttm_unbind;
	    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
		perror("Could not unbind.");
	    }
	    t2 = fastrdtsc();
	    printf("Unbind took %u clocks.\n", time_diff(t1, t2));

	    drmUnlock(ctx->drmFD, ctx->hwContext);
	    t1 = fastrdtsc();
	    a = 0;
	    for (j = 0; j < USESIZE; ++j) {
		a += ((volatile unsigned *)ttmAddress)[j + bufoffs];
	    }
	    t2 = fastrdtsc();
	    printf("Page-faulting cached read took %u clocks\n\n\n",
		time_diff(t1, t2));
	}
	drmUnmap(ttmAddress, TTMSIZE);  

	ttmAddress = malloc(TTMSIZE);
	memset(ttmAddress, 0, USESIZE*4); 
	buf_arg.op = ttm_validate_user;
	buf_arg.user_addr = ttmAddress;
	buf_arg.user_size = USESIZE*4;
	buf_arg.flags = DRM_TTM_FLAG_NEW;
	drmGetLock(ctx->drmFD, ctx->hwContext, 0);

	t1 = fastrdtsc();
	if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
	    perror("Could not user bind");
	} else {
	    printf("User bound region is %d\n", buf_arg.region_handle);
	}
	t2 = fastrdtsc();

	drmUnlock(ctx->drmFD, ctx->hwContext);
	printf("User binding took %u clocks\n", time_diff(t1, t2));
   
	drmGetLock(ctx->drmFD, ctx->hwContext, 0);

	
	t1 = fastrdtsc();
	buf_arg.op = ttm_destroy;
	if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg)) {
	    perror("Could not user unbind");
	}
	t2 = fastrdtsc();
	printf("User unbind took %u clocks\n", time_diff(t1, t2));
	
	drmUnlock(ctx->drmFD, ctx->hwContext);
	free(ttmAddress);

    } else {
	perror("Could not map");
    }
    arg.op = ttm_remove;
    if (ioctl(ctx->drmFD, DRM_IOCTL_TTM, &arg) != 0) {
	perror("Could not remove map");
    }

}

int
main()
{
    int ret, screen, isCapable;
    char *displayName = ":0";
    TinyDRIContext ctx;
    unsigned magic;

    ctx.screen = 0;
    ctx.state = haveNothing;
    ctx.display = XOpenDisplay(displayName);
    if (!ctx.display) {
	fprintf(stderr, "Could not open display\n");
	return releaseContext(&ctx);
    }
    ctx.state = haveDisplay;

    ret =
	uniDRIQueryDirectRenderingCapable(ctx.display, ctx.screen,
	&isCapable);
    if (!ret || !isCapable) {
	fprintf(stderr, "No DRI on this display:sceen\n");
	return releaseContext(&ctx);
    }

    if (!uniDRIOpenConnection(ctx.display, ctx.screen, &ctx.sAreaOffset,
	    &ctx.curBusID)) {
	fprintf(stderr, "Could not open DRI connection.\n");
	return releaseContext(&ctx);
    }
    ctx.state = haveConnection;

    if (!uniDRIGetClientDriverName(ctx.display, ctx.screen,
	    &ctx.ddxDriverMajor, &ctx.ddxDriverMinor,
	    &ctx.ddxDriverPatch, &ctx.driverName)) {
	fprintf(stderr, "Could not get DRI driver name.\n");
	return releaseContext(&ctx);
    }
    ctx.state = haveDriverName;

    if (!uniDRIGetDeviceInfo(ctx.display, ctx.screen,
	    &ctx.fbHandle, &ctx.fbOrigin, &ctx.fbSize,
	    &ctx.fbStride, &ctx.driPrivSize, &ctx.driPriv)) {
	fprintf(stderr, "Could not get DRI device info.\n");
	return releaseContext(&ctx);
    }
    ctx.state = haveDriverName;

    if ((ctx.drmFD = drmOpen(NULL, ctx.curBusID)) < 0) {
	perror("DRM Device could not be opened");
	return releaseContext(&ctx);
    }
    ctx.state = haveDRM;

    drmGetMagic(ctx.drmFD, &magic);
    if (!uniDRIAuthConnection(ctx.display, ctx.screen, magic)) {
	fprintf(stderr, "Could not get X server to authenticate us.\n");
	return releaseContext(&ctx);
    }

    ret = XMatchVisualInfo(ctx.display, ctx.screen, 24, TrueColor,
	&ctx.visualInfo);
    if (!ret) {
	ret = XMatchVisualInfo(ctx.display, ctx.screen, 16, TrueColor,
	    &ctx.visualInfo);
	if (!ret) {
	    fprintf(stderr, "Could not find a matching visual.\n");
	    return releaseContext(&ctx);
	}
    }

    if (!uniDRICreateContext(ctx.display, ctx.screen, ctx.visualInfo.visual,
	    &ctx.id, &ctx.hwContext)) {
	fprintf(stderr, "Could not create DRI context.\n");
	return releaseContext(&ctx);
    }
    ctx.state = haveContext;

    testAGP(&ctx);

    releaseContext(&ctx);
    printf("Terminating normally\n");
    return 0;
}
