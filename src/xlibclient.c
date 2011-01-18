/*
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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* Author: Paulo Zanoni <pzanoni@mandriva.com>
*/

#include <stdlib.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <xorg/xf86.h>

#include "client.h"

struct NestedvClientPrivate {
    Display *display;
    int screenNumber;
    Screen *screen;
    Window rootWindow;
    Window window;
    XImage *img;
    GC gc;
    Bool usingShm;
    XShmSegmentInfo shminfo;
    int scrnIndex; /* stored only for xf86DrvMsg usage */
    Cursor mycursor; /* Test cursor */
    Pixmap bitmapNoData;
    XColor color1;
};

/* Checks if a display is open */
Bool
NestedvClientCheckDisplay(char *displayName)
{
    Display *d;

    d = XOpenDisplay(displayName);
    if (!d) {
return FALSE;
    } else {
XCloseDisplay(d);
return TRUE;
    }
}

Bool
NestedvClientValidDepth(int depth)
{
    /* XXX: implement! */
    return TRUE;
}

NestedvClientPrivatePtr
NestedvClientCreateScreen(int scrnIndex,
char *displayName,
int width,
int height,
int originX,
int originY,
int depth,
int bitsPerPixel,
Pixel *retRedMask,
Pixel *retGreenMask,
Pixel *retBlueMask)
{
    NestedvClientPrivatePtr pPriv;
    XSizeHints sizeHints;

    int shmMajor, shmMinor;
    Bool hasSharedPixmaps;

    pPriv = malloc(sizeof(struct NestedvClientPrivate));
    pPriv->scrnIndex = scrnIndex;

    pPriv->display = XOpenDisplay(displayName);
    if (!pPriv->display)
return NULL;

    pPriv->screenNumber = DefaultScreen(pPriv->display);
    pPriv->screen = ScreenOfDisplay(pPriv->display, pPriv->screenNumber);
    pPriv->rootWindow = RootWindow(pPriv->display, pPriv->screenNumber);
    pPriv->gc = DefaultGC(pPriv->display, pPriv->screenNumber);

    pPriv->window = XCreateSimpleWindow(pPriv->display, pPriv->rootWindow,
originX, originY, width, height,
0, 0, 0);

    sizeHints.flags = PPosition | PSize | PMinSize | PMaxSize;
    sizeHints.min_width = width;
    sizeHints.max_width = width;
    sizeHints.min_height = height;
    sizeHints.max_height = height;
    XSetWMNormalHints(pPriv->display, pPriv->window, &sizeHints);

    XStoreName(pPriv->display, pPriv->window, "TTCCWN#124");

    XMapWindow(pPriv->display, pPriv->window);

    XSelectInput(pPriv->display, pPriv->window, ExposureMask | PointerMotionMask);

    if (XShmQueryExtension(pPriv->display)) {
if (XShmQueryVersion(pPriv->display, &shmMajor, &shmMinor,
&hasSharedPixmaps)) {
xf86DrvMsg(scrnIndex, X_INFO,
"XShm extension version %d.%d %s shared pixmaps\n",
shmMajor, shmMinor,
(hasSharedPixmaps) ? "with" : "without");
}
pPriv->img = XShmCreateImage(pPriv->display,
DefaultVisualOfScreen(pPriv->screen),
depth,
ZPixmap,
NULL, /* data */
&pPriv->shminfo,
width,
height);
if (!pPriv->img)
return NULL;

/* XXX: change the 0777 mask? */
pPriv->shminfo.shmid = shmget(IPC_PRIVATE,
pPriv->img->bytes_per_line *
pPriv->img->height,
IPC_CREAT | 0777);
if (pPriv->shminfo.shmid == -1) {
XDestroyImage(pPriv->img);
return NULL;
}

pPriv->shminfo.shmaddr = (char *)shmat(pPriv->shminfo.shmid, NULL, 0);
if (pPriv->shminfo.shmaddr == (char *) -1) {
XDestroyImage(pPriv->img);
return NULL;
}

pPriv->img->data = pPriv->shminfo.shmaddr;
pPriv->shminfo.readOnly = FALSE;
XShmAttach(pPriv->display, &pPriv->shminfo);

pPriv->usingShm = TRUE;

    } else {
xf86DrvMsg(scrnIndex, X_INFO, "XShm not supported\n");
pPriv->img = XCreateImage(pPriv->display,
DefaultVisualOfScreen(pPriv->screen),
depth,
ZPixmap,
0, /* offset */
NULL, /* data */
width,
height,
32, /* XXX: bitmap_pad */
0 /* XXX: bytes_per_line */);
if (!pPriv->img)
return NULL;
pPriv->img->data = malloc(pPriv->img->bytes_per_line *
pPriv->img->height);

pPriv->usingShm = FALSE;
    }
    if (!pPriv->img->data)
return NULL;

    NestedvClientHideCursor(pPriv); /* Hide cursor */

#if 0
xf86DrvMsg(scrnIndex, X_INFO, "width: %d\n", pPriv->img->width);
xf86DrvMsg(scrnIndex, X_INFO, "height: %d\n", pPriv->img->height);
xf86DrvMsg(scrnIndex, X_INFO, "xoffset: %d\n", pPriv->img->xoffset);
xf86DrvMsg(scrnIndex, X_INFO, "depth: %d\n", pPriv->img->depth);
xf86DrvMsg(scrnIndex, X_INFO, "bpp: %d\n", pPriv->img->bits_per_pixel);
xf86DrvMsg(scrnIndex, X_INFO, "red_mask: 0x%lx\n", pPriv->img->red_mask);
xf86DrvMsg(scrnIndex, X_INFO, "gre_mask: 0x%lx\n", pPriv->img->green_mask);
xf86DrvMsg(scrnIndex, X_INFO, "blu_mask: 0x%lx\n", pPriv->img->blue_mask);
#endif
    *retRedMask = pPriv->img->red_mask;
    *retGreenMask = pPriv->img->green_mask;
    *retBlueMask = pPriv->img->blue_mask;

    XEvent ev;
    while (1) {
XNextEvent(pPriv->display, &ev);
if (ev.type == Expose) {
break;
}
    }

    return pPriv;
}

void NestedvClientHideCursor(NestedvClientPrivatePtr pPriv)
{
    char noData[]= {0,0,0,0,0,0,0,0};
    pPriv->color1.red = pPriv->color1.green = pPriv->color1.blue = 0;
    pPriv->bitmapNoData = XCreateBitmapFromData(pPriv->display, pPriv->window, noData, 7, 7);
    pPriv->mycursor = XCreatePixmapCursor(pPriv->display, pPriv->bitmapNoData, pPriv->bitmapNoData,
                             &pPriv->color1, &pPriv->color1, 0, 0);
    XDefineCursor(pPriv->display, pPriv->window, pPriv->mycursor);
    XFreeCursor(pPriv->display, pPriv->mycursor);
}

char *
NestedvClientGetFrameBuffer(NestedvClientPrivatePtr pPriv)
{
    return pPriv->img->data;
}

void
NestedvClientUpdateScreen(NestedvClientPrivatePtr pPriv, int16_t x1, int16_t y1,
int16_t x2, int16_t y2)
{
    if (pPriv->usingShm) {
XShmPutImage(pPriv->display, pPriv->window, pPriv->gc, pPriv->img,
x1, y1, x1, y1, x2 - x1, y2 - y1, FALSE);
/* Without this sync we get some freezes, probably due to some lock in
* the shm usage */
XSync(pPriv->display, FALSE);
    } else {
XPutImage(pPriv->display, pPriv->window, pPriv->gc, pPriv->img,
x1, y1, x1, y1, x2 - x1, y2 - y1);
    }
}

void
NestedvClientTimerCallback(NestedvClientPrivatePtr pPriv)
{
    XEvent ev;
    char *msg = "Cursor";
    char *msg2 = "Root";

    while(XCheckMaskEvent(pPriv->display, ~0, &ev)) {
if (ev.type == Expose) {
NestedvClientUpdateScreen(pPriv,
((XExposeEvent*)&ev)->x,
((XExposeEvent*)&ev)->y,
((XExposeEvent*)&ev)->x + ((XExposeEvent*)&ev)->width,
((XExposeEvent*)&ev)->y + ((XExposeEvent*)&ev)->height);
}
if (ev.type == MotionNotify) {
XDrawString(pPriv->display, pPriv->window, DefaultGC(pPriv->display, pPriv->screenNumber), ((XMotionEvent*)&ev)->x, ((XMotionEvent*)&ev)->y, msg, strlen(msg));
XDrawString(pPriv->display, pPriv->window, DefaultGC(pPriv->display, pPriv->screenNumber), ((XMotionEvent*)&ev)->x_root, ((XMotionEvent*)&ev)->y_root, msg2, strlen(msg2));
}
    }
}

void
NestedvClientCloseScreen(NestedvClientPrivatePtr pPriv)
{
    if (pPriv->usingShm) {
XShmDetach(pPriv->display, &pPriv->shminfo);
shmdt(pPriv->shminfo.shmaddr);
    } else {
free(pPriv->img->data);
    }
    XDestroyImage(pPriv->img);
    XCloseDisplay(pPriv->display);
}
