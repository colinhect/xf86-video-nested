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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Paulo Zanoni <pzanoni@mandriva.com>
 */

#include <stdlib.h>
#include <string.h>

#include <xorg/xorg-server.h>
#include <xorg/fb.h>
#include <xorg/micmap.h>
#include <xorg/mipointer.h>
#include <xorg/shadow.h>
#include <xorg/xf86.h>
#include <xorg/xf86Module.h>
#include <xorg/xf86str.h>
#include "xf86Xinput.h"

#include "config.h"

#include "client.h"

#define NESTED_VERSION 0
#define NESTED_NAME "NESTED"
#define NESTED_DRIVER_NAME "nested"

#define NESTED_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define NESTED_MINOR_VERSION PACKAGE_VERSION_MINOR
#define NESTED_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

#define TIMER_CALLBACK_INTERVAL 80

static MODULESETUPPROTO(NestedSetup);
static void NestedIdentify(int flags);
static const OptionInfoRec *NestedAvailableOptions(int chipid, int busid);
static Bool NestedProbe(DriverPtr drv, int flags);
static Bool NestedDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
                             pointer ptr);

static Bool NestedPreInit(ScrnInfoPtr pScrn, int flags);
static Bool NestedScreenInit(int scrnIndex, ScreenPtr pScreen, int argc,
                             char **argv);

static Bool NestedSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
static void NestedAdjustFrame(int scrnIndex, int x, int y, int flags);
static Bool NestedEnterVT(int scrnIndex, int flags);
static void NestedLeaveVT(int scrnIndex, int flags);
static void NestedFreeScreen(int scrnIndex, int flags);
static ModeStatus NestedValidMode(int scrnIndex, DisplayModePtr mode,
                                  Bool verbose, int flags);

static Bool NestedSaveScreen(ScreenPtr pScreen, int mode);
static Bool NestedCreateScreenResources(ScreenPtr pScreen);

static void NestedShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf);
static void *NestedShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset,
                                int mode, CARD32 *size, void *closure);
static Bool NestedCloseScreen(int scrnIndex, ScreenPtr pScreen);

static CARD32 NestedTimerCallback(OsTimerPtr timer, CARD32 time, pointer arg);

int NestedValidateModes(ScrnInfoPtr pScrn);
Bool NestedAddMode(ScrnInfoPtr pScrn, int width, int height);
void NestedPrintPscreen(ScrnInfoPtr p);
void NestedPrintMode(ScrnInfoPtr p, DisplayModePtr m);

static InputInfoPtr NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags);

static void NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo,
int flags);

typedef enum {
    OPTION_DISPLAY,
    OPTION_ORIGIN
} NestedOpts;

typedef enum {
    NESTED_CHIP
} NestedType;

static SymTabRec NestedChipsets[] = {
    { NESTED_CHIP, "nested" },
    {-1,            NULL }
};

/* XXX: Shouldn't we allow NestedClient to define options too? If some day we
 * port NestedClient to something that's not Xlib/Xcb we might need to add some
 * custom options */
static OptionInfoRec NestedOptions[] = {
    { OPTION_DISPLAY, "Display", OPTV_STRING, {0}, FALSE },
    { OPTION_ORIGIN,  "Origin",  OPTV_STRING, {0}, FALSE },
    { -1,             NULL,      OPTV_NONE,   {0}, FALSE }
};

_X_EXPORT DriverRec NESTED = {
    NESTED_VERSION,
    NESTED_DRIVER_NAME,
    NestedIdentify,
    NestedProbe,
    NestedAvailableOptions,
    NULL, /* module */
    0,    /* refCount */
    NestedDriverFunc,
    NULL, /* DeviceMatch */
    0     /* PciProbe */
};

_X_EXPORT InputDriverRec MOUSE = {
    1,
    "random",
    NULL,
    NestedMousePreInit,
    NestedMouseUnInit,
    NULL,
    0,
};

static XF86ModuleVersionInfo NestedVersRec = {
    NESTED_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    NESTED_MAJOR_VERSION,
    NESTED_MINOR_VERSION,
    NESTED_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0} /* checksum */
};

_X_EXPORT XF86ModuleData nestedModuleData = {
    &NestedVersRec,
    NestedSetup,
    NULL, /* teardown */
};

/* These stuff should be valid to all server generations */
typedef struct NestedPrivate {
    char                        *displayName;
    int                          originX;
    int                          originY;
    NestedClientPrivatePtr      clientData;
    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr           CloseScreen;
    OsTimerPtr                   timer;
    ShadowUpdateProc             update;
    /*ShadowWindowProc window;*/
} NestedPrivate, *NestedPrivatePtr;

#define PNESTED(p)    ((NestedPrivatePtr)((p)->driverPrivate))
#define PCLIENTDATA(p) (PNESTED(p)->clientData)

/*static ScrnInfoPtr NESTEDScrn;*/

static pointer
NestedSetup(pointer module, pointer opts, int *errmaj, int *errmin) {
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&NESTED, module, HaveDriverFuncs);
        
        return (pointer)1;
    } else {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;
        
        return NULL;
    }
}

static void
NestedIdentify(int flags) {
    xf86PrintChipsets(NESTED_NAME, "Driver for nested servers",
                      NestedChipsets);
}

static const OptionInfoRec *
NestedAvailableOptions(int chipid, int busid) {
    return NestedOptions;
}

static Bool
NestedProbe(DriverPtr drv, int flags) {
    Bool foundScreen = FALSE;
    int numDevSections;
    GDevPtr *devSections;
    int i;

    ScrnInfoPtr pScrn;
    int entityIndex;

    if (flags & PROBE_DETECT)
        return FALSE;

    if ((numDevSections = xf86MatchDevice(NESTED_DRIVER_NAME,
                                          &devSections)) <= 0) {
        return FALSE;
    }


    if (numDevSections > 0) {
        for(i = 0; i < numDevSections; i++) {
            pScrn = NULL;
            entityIndex = xf86ClaimNoSlot(drv, NESTED_CHIP, devSections[i],
                                          TRUE);
            pScrn = xf86AllocateScreen(drv, 0);
            if (pScrn) {
                xf86AddEntityToScreen(pScrn, entityIndex);
                pScrn->driverVersion = NESTED_VERSION;
                pScrn->driverName    = NESTED_DRIVER_NAME;
                pScrn->name          = NESTED_NAME;
                pScrn->Probe         = NestedProbe;
                pScrn->PreInit       = NestedPreInit;
                pScrn->ScreenInit    = NestedScreenInit;
                pScrn->SwitchMode    = NestedSwitchMode;
                pScrn->AdjustFrame   = NestedAdjustFrame;
                pScrn->EnterVT       = NestedEnterVT;
                pScrn->LeaveVT       = NestedLeaveVT;
                pScrn->FreeScreen    = NestedFreeScreen;
                pScrn->ValidMode     = NestedValidMode;
                foundScreen = TRUE;
            }
        }
    }

    return foundScreen;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
NestedDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr) {
    CARD32 *flag;
    xf86Msg(X_INFO, "NestedDriverFunc\n");

    /* XXX implement */
    switch(op) {
        case GET_REQUIRED_HW_INTERFACES:
            flag = (CARD32*)ptr;
            (*flag) = HW_SKIP_CONSOLE;
            return TRUE;

        case RR_GET_INFO:
        case RR_SET_CONFIG:
        case RR_GET_MODE_MM:
        default:
            return FALSE;
    }
}

static Bool NestedAllocatePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate != NULL) {
        xf86Msg(X_WARNING, "NestedAllocatePrivate called for an already "
                "allocated private!\n");
        return FALSE;
    }

    pScrn->driverPrivate = xnfcalloc(sizeof(NestedPrivate), 1);
    if (pScrn->driverPrivate == NULL)
        return FALSE;
    return TRUE;
}

static void NestedFreePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Double freeing NestedPrivate!\n");
        return;
    }

    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* Data from here is valid to all server generations */
static Bool NestedPreInit(ScrnInfoPtr pScrn, int flags) {
    NestedPrivatePtr pNested;
    char *originString = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NestedPreInit\n");

    if (flags & PROBE_DETECT)
        return FALSE;

    if (!NestedAllocatePrivate(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to allocate private\n");
        return FALSE;
    }

    pNested = PNESTED(pScrn);

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support24bppFb | Support32bppFb))
        return FALSE;
 
    xf86PrintDepthBpp(pScrn);

    if (pScrn->depth > 8) {
        rgb zeros = {0, 0, 0};
        if (!xf86SetWeight(pScrn, zeros, zeros)) {
            return FALSE;
        }
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor; /* XXX */

    xf86CollectOptions(pScrn, NULL);
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, NestedOptions);

    if (xf86IsOptionSet(NestedOptions, OPTION_DISPLAY)) {
        pNested->displayName = xf86GetOptValString(NestedOptions,
                                                   OPTION_DISPLAY);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using display \"%s\"\n",
                   pNested->displayName);
    } else {
        pNested->displayName = NULL;
    }

    if (xf86IsOptionSet(NestedOptions, OPTION_ORIGIN)) {
        originString = xf86GetOptValString(NestedOptions, OPTION_ORIGIN);
        if (sscanf(originString, "%d %d", &pNested->originX,
            &pNested->originY) != 2) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Invalid value for option \"Origin\"\n");
            return FALSE;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using origin x:%d y:%d\n",
                   pNested->originX, pNested->originY);
    } else {
        pNested->originX = 0;
        pNested->originY = 0;
    }

    xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (!NestedClientCheckDisplay(pNested->displayName)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Can't open display: %s\n",
                   pNested->displayName);
        return FALSE;
    }

    if (!NestedClientValidDepth(pScrn->depth)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Invalid depth: %d\n",
                   pScrn->depth);
        return FALSE;
    }

    /*if (pScrn->depth > 1) {
        Gamma zeros = {0.0, 0.0, 0.0};
        if (!xf86SetGamma(pScrn, zeros))
            return FALSE;
    }*/

    if (NestedValidateModes(pScrn) < 1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes\n");
        return FALSE;
    }


    if (!pScrn->modes) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
        return FALSE;
    }
    xf86SetCrtcForModes(pScrn, 0);

    pScrn->currentMode = pScrn->modes;

    xf86SetDpi(pScrn, 0, 0);

    if (!xf86LoadSubModule(pScrn, "shadow"))
        return FALSE;
    if (!xf86LoadSubModule(pScrn, "fb"))
        return FALSE;
    if (!xf86LoadSubModule(pScrn, "ramdac"))
        return FALSE;

    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    return TRUE;
}

static InputInfoPtr 
NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    return NULL;
}

static void
NestedMouseUnInit(InputDriverPtr       drv,
             InputInfoPtr         pInfo,
             int                  flags)
{
}


int
NestedValidateModes(ScrnInfoPtr pScrn) {
    DisplayModePtr mode;
    int i, width, height, ret = 0;
    int maxX = 0, maxY = 0;

    /* Print useless stuff */
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Monitor wants these modes:\n");
    for(mode = pScrn->monitor->Modes; mode != NULL; mode = mode->next) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s (%dx%d)\n", mode->name,
                   mode->HDisplay, mode->VDisplay);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Too bad for it...\n");

    /* If user requested modes, add them. If not, use 640x480 */
    if (pScrn->display->modes != NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "User wants these modes:\n");
        for(i = 0; pScrn->display->modes[i] != NULL; i++) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s\n",
                       pScrn->display->modes[i]);
            if (sscanf(pScrn->display->modes[i], "%dx%d", &width,
                       &height) != 2) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                           "This is not the mode name I was expecting...\n");
                return 0;
            }
            if (!NestedAddMode(pScrn, width, height)) {
                return 0;
            }
        }
    } else {
        if (!NestedAddMode(pScrn, 640, 480)) {
            return 0;
        }
    }

    pScrn->modePool = NULL;

    /* Now set virtualX, virtualY, displayWidth and virtualFrom */

    if (pScrn->display->virtualX >= pScrn->modes->HDisplay &&
        pScrn->display->virtualY >= pScrn->modes->VDisplay) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        /* XXX: if not specified, make virtualX and virtualY as big as the max X
         * and Y. I'm not sure this is correct */
        mode = pScrn->modes;
        while (mode != NULL) {
            if (mode->HDisplay > maxX)
                maxX = mode->HDisplay;
       
            if (mode->VDisplay > maxY)
                maxY = mode->VDisplay;
          
            mode = mode->next;
        }
        pScrn->virtualX = maxX;
        pScrn->virtualY = maxY;
    }
    pScrn->virtualFrom = X_DEFAULT;
    pScrn->displayWidth = pScrn->virtualX;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Virtual size: %dx%d\n",
               pScrn->virtualX, pScrn->virtualY);

    /* Calculate the return value */
    mode = pScrn->modes;
    while (mode != NULL) {
        mode = mode->next;
        ret++;
    }

    /* Finally, make the mode list circular */
    pScrn->modes->prev->next = pScrn->modes;

    return ret;

#if 0
    /* Use xf86ValidateModes */
    ClockRangePtr clockRanges;
    int rc;

    pScrn->videoRam = 1024*128; /* kByte */
    pScrn->progClock = TRUE;
    clockRanges = (ClockRangePtr)xnfcalloc(sizeof(ClockRange), 1);
    clockRanges->next = NULL;
    clockRanges->ClockMulFactor = 1;
    clockRanges->minClock = 1;
    clockRanges->maxClock = 3000000;
    clockRanges->clockIndex = 1;
    clockRanges->interlaceAllowed = TRUE;
    clockRanges->doubleScanAllowed = TRUE;
    rc = xf86ValidateModes(pScrn,                    /* scrp        */
                          pScrn->monitor->Modes,    /* availModes  */
                          pScrn->display->modes,    /* modeNames   */
                          clockRanges,              /* cockRanges  */
                          NULL,                     /* linePitches (opt) */
                          256,                      /* minPitch    (opt) */
                          2048,                     /* maxPitch    (opt) */
                          (8*pScrn->bitsPerPixel),  /* pitchInc    */
                          0,                        /* minHeight   (opt) */
                          0,                        /* maxHeight   (opt) */
                          pScrn->display->virtualX, /* virualX     (opt) */
                          pScrn->display->virtualY, /* virtualY    (opt) */
                          pScrn->videoRam * 1024,   /* apertureSize */
                          LOOKUP_BEST_REFRESH       /* strategy */);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pScrn->modePool: 0x%x\n",
               pScrn->modePool);

    int rounds = 0;
    for (mode = pScrn->modes;
         (mode != pScrn->modes) && (rounds == 1);
         mode = mode->next) {
        rounds = 1;
        NestedValidMode(pScrn->scrnIndex, mode, FALSE, 0);

    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pScrn->virtualX: %d\n",
               pScrn->virtualX);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pScrn->virtualY: %d\n",
               pScrn->virtualY);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pScrn->displayWidth: %d\n",
               pScrn->displayWidth);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pScrn->virtualFrom: %d\n",
               pScrn->virtualFrom);
    if (rc < 1) {
        return rc;
    } else {
        xf86PruneDriverModes(pScrn);
        return rc;
    }
#endif
}

Bool
NestedAddMode(ScrnInfoPtr pScrn, int width, int height) {
    DisplayModePtr mode;
    char nameBuf[64];
    int rc;
    size_t len;

    if (snprintf(nameBuf, 64, "%dx%d", width, height) >= 64)
        return FALSE;

    mode = XNFcalloc(sizeof(DisplayModeRec));
    mode->status = MODE_OK;
    mode->type = M_T_DRIVER;
    mode->HDisplay = width;
    mode->VDisplay = height;

    len = strlen(nameBuf);
    mode->name = XNFalloc(len+1);
    strcpy(mode->name, nameBuf);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Adding mode %s\n", mode->name);

    /* Now add mode to pScrn->modes. We'll keep the list non-circular for now,
     * but we'll maintain pScrn->modes->prev to know the last element */
    mode->next = NULL;
    if (!pScrn->modes) {
        pScrn->modes = mode;
        mode->prev = mode;
    } else {
        mode->prev = pScrn->modes->prev;
        pScrn->modes->prev->next = mode;
        pScrn->modes->prev = mode;
    }

    return TRUE;
}

/* Called at each server generation */
static Bool NestedScreenInit(int scrnIndex, ScreenPtr pScreen, int argc,
                             char **argv) {
    ScrnInfoPtr pScrn;
    NestedPrivatePtr pNested;
    Pixel redMask, greenMask, blueMask;

    xf86DrvMsg(scrnIndex, X_INFO, "NestedScreenInit\n");

    pScrn = xf86Screens[pScreen->myNum];
    pNested = PNESTED(pScrn);
    /*NESTEDScrn = pScrn;*/

    NestedPrintPscreen(pScrn);

    /* Save state:
     * NestedSave(pScrn); */

    pNested->clientData = NestedClientCreateScreen(scrnIndex,
                                                   pNested->displayName,
                                                   pScrn->virtualX,
                                                   pScrn->virtualY,
                                                   pNested->originX,
                                                   pNested->originY,
                                                   pScrn->depth,
                                                   pScrn->bitsPerPixel,
                                                   &redMask, &greenMask, &blueMask);
    if (!pNested->clientData) {
        xf86DrvMsg(scrnIndex, X_ERROR, "Failed to create client screen\n");
        return FALSE;
    }

    miClearVisualTypes();
    if (!miSetVisualTypesAndMasks(pScrn->depth,
                                  miGetDefaultVisualMask(pScrn->depth),
                                  pScrn->rgbBits, pScrn->defaultVisual,
                                  redMask, greenMask, blueMask))
        return FALSE;
    
    if (!miSetPixmapDepths())
        return FALSE;

    if (!fbScreenInit(pScreen, NestedClientGetFrameBuffer(PCLIENTDATA(pScrn)),
                      pScrn->virtualX, pScrn->virtualY, pScrn->xDpi,
                      pScrn->yDpi, pScrn->displayWidth, pScrn->bitsPerPixel))
        return FALSE;

    fbPictureInit(pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());
    
    if (!miCreateDefColormap(pScreen))
        return FALSE;

    pNested->update = NestedShadowUpdate;
    /*pNested->window = NestedShadowWindow;*/
    pScreen->SaveScreen = NestedSaveScreen;

    if (!shadowSetup(pScreen))
        return FALSE;

    pNested->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = NestedCreateScreenResources;

    pNested->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = NestedCloseScreen;

    pNested->timer = TimerSet(NULL, 0, TIMER_CALLBACK_INTERVAL,
                              NestedTimerCallback, (pointer)pScrn);

    return TRUE;

}

static Bool
NestedCreateScreenResources(ScreenPtr pScreen) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "NestedCreateScreenResources\n");
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    NestedPrivatePtr pNested = PNESTED(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = pNested->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = NestedCreateScreenResources;

    shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen), pNested->update,
              /*pNested->window*/ 0, 0, 0);
}

static void
NestedShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf) {
    RegionPtr pRegion = DamageRegion(pBuf->pDamage);
    NestedClientUpdateScreen(PCLIENTDATA(xf86Screens[pScreen->myNum]),
                             pRegion->extents.x1, pRegion->extents.y1,
                             pRegion->extents.x2, pRegion->extents.y2);
}

static Bool
NestedCloseScreen(int scrnIndex, ScreenPtr pScreen) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];

    xf86DrvMsg(scrnIndex, X_INFO, "NestedCloseScreen\n");

    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));

    TimerFree(PNESTED(pScrn)->timer);
    NestedClientCloseScreen(PCLIENTDATA(pScrn));

    pScreen->CloseScreen = PNESTED(pScrn)->CloseScreen;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static CARD32 NestedTimerCallback(OsTimerPtr timer, CARD32 time, pointer arg) {
    ScrnInfoPtr pScrn = (ScrnInfoPtr) arg;
    NestedClientTimerCallback(PCLIENTDATA(pScrn));
    return TIMER_CALLBACK_INTERVAL;
}

static void *NestedShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset,
                                int mode, CARD32 *size, void *closure) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "NestedShadowWindow\n");
}

static Bool NestedSaveScreen(ScreenPtr pScreen, int mode) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "NestedSaveScreen\n");
}

static Bool NestedSwitchMode(int scrnIndex, DisplayModePtr mode, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedSwitchMode\n");
}

static void NestedAdjustFrame(int scrnIndex, int x, int y, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedAdjustFrame\n");
}

static Bool NestedEnterVT(int scrnIndex, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedEnterVT\n");
}

static void NestedLeaveVT(int scrnIndex, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedLeaveVT\n");
}

static void NestedFreeScreen(int scrnIndex, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedFreeScreen\n");
}

static ModeStatus NestedValidMode(int scrnIndex, DisplayModePtr mode,
                                  Bool verbose, int flags) {
    xf86DrvMsg(scrnIndex, X_INFO, "NestedValidMode:\n");

    if (!mode)
        xf86DrvMsg(scrnIndex, X_ERROR, "NULL MODE!\n");

    xf86DrvMsg(scrnIndex, X_INFO, "  name: %s\n", mode->name);
    xf86DrvMsg(scrnIndex, X_INFO, "  HDisplay: %d\n", mode->HDisplay);
    xf86DrvMsg(scrnIndex, X_INFO, "  VDisplay: %d\n", mode->VDisplay);
    return MODE_OK;
}

void NestedPrintPscreen(ScrnInfoPtr p) {
    /* XXX: finish implementing this someday? */
    xf86DrvMsg(p->scrnIndex, X_INFO, "Printing pScrn:\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverVersion: %d\n", p->driverVersion);
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverName:    %s\n", p->driverName);
    xf86DrvMsg(p->scrnIndex, X_INFO, "pScreen:       %p\n", p->pScreen);
    xf86DrvMsg(p->scrnIndex, X_INFO, "scrnIndex:     %d\n", p->scrnIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "configured:    %d\n", p->configured);
    xf86DrvMsg(p->scrnIndex, X_INFO, "origIndex:     %d\n", p->origIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "imageByteOrder: %d\n", p->imageByteOrder);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlineUnit: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlinePad: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapBitOrder: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "numFormats: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "formats[]: 0x%x\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "fbFormat: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitsPerPixel: %d\n", p->bitsPerPixel);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "pixmap24: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "depth: %d\n", p->depth);
    NestedPrintMode(p, p->currentMode);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "depthFrom: %\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "\n");*/
}

void NestedPrintMode(ScrnInfoPtr p, DisplayModePtr m) {
    xf86DrvMsg(p->scrnIndex, X_INFO, "HDisplay   %d\n",   m->HDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncStart %d\n", m->HSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncEnd   %d\n",   m->HSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HTotal     %d\n",     m->HTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSkew      %d\n",      m->HSkew);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VDisplay   %d\n",   m->VDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncStart %d\n", m->VSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncEnd   %d\n",   m->VSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VTotal     %d\n",     m->VTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VScan      %d\n",      m->VScan);
}
