#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

#include "xNestedMouse.h"

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

static InputInfoPtr NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags);
static void NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);
static pointer NestedMousePlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void NestedMouseUnplug(pointer p);
static void NestedMouseReadInput(InputInfoPtr pInfo);
static int NestedMouseControl(DeviceIntPtr    device,int what);
static int _nested_mouse_init_buttons(DeviceIntPtr device);
static int _nested_mouse_init_axes(DeviceIntPtr device);

_X_EXPORT InputDriverRec NESTEDMOUSE = {
    1,
    "nestedmouse",
    NULL,
    NestedMousePreInit,
    NestedMouseUnInit,
    NULL,
    0,
};

typedef struct _NestedMouseDeviceRec {
    char *device;
    int version;        /* Driver version */
    Atom* labels;
    int num_vals;
    int axes;
} NestedMouseDeviceRec, *NestedMouseDevicePtr;

static XF86ModuleVersionInfo NestedMouseVersionRec = {
    "nestedmouse",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData nestedMouseModuleData = {
    &NestedMouseVersionRec,
    &NestedMousePlug,
    &NestedMouseUnplug
};

static InputInfoPtr 
NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags) {
    InputInfoPtr            pInfo;
    NestedMouseDevicePtr    pNestedMouse;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    pNestedMouse = xcalloc(1, sizeof(NestedMouseDeviceRec));

    if (!pNestedMouse) {
        pInfo->private = NULL;
        xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    pInfo->private = pNestedMouse;
    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = XI_MOUSE; /* see XI.h */
    pInfo->conf_idev = dev;
    pInfo->read_input = NestedMouseReadInput; /* new data avl */
    pInfo->switch_mode = NULL; /* toggle absolute/relative mode */
    pInfo->device_control = NestedMouseControl; /* enable/disable dev */

    /* process driver specific options */
    pNestedMouse->device = xf86SetStrOption(dev->commonOptions,
                                         "Device",
                                         "/dev/nestedmouse");

    xf86Msg(X_INFO, "%s: Using device %s.\n", pInfo->name, pNestedMouse->device);

    /* process generic options */
    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);
    
    /* Open sockets, init device files, etc. */
    SYSCALL(pInfo->fd = open(pNestedMouse->device, O_RDWR | O_NONBLOCK));
    
    if (pInfo->fd == -1) {
        xf86Msg(X_ERROR, "%s: failed to open %s.",
                pInfo->name, pNestedMouse->device);
        pInfo->private = NULL;
        xfree(pNestedMouse);
        xf86DeleteInput(pInfo, 0);
        return NULL;
    }
    
    /* do more funky stuff */
    close(pInfo->fd);
    pInfo->fd = -1;
    pInfo->flags |= XI86_OPEN_ON_INIT;
    pInfo->flags |= XI86_CONFIGURED;
    return pInfo;
}

static void
NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags) {
}

static pointer
NestedMousePlug(pointer module, pointer options, int *errmaj, int  *errmin) {
    xf86AddInputDriver(&NESTEDMOUSE, module, 0);
    return module;
}

static void
NestedMouseUnplug(pointer p) {
}

static int
_nested_mouse_init_buttons(DeviceIntPtr device) {
    return -1;
}

static int
_nested_mouse_init_axes(DeviceIntPtr device) {
    return -1;
}

static int 
NestedMouseControl(DeviceIntPtr device, int what) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    NestedMouseDevicePtr pNestedMouse = pInfo->private;

    switch (what) {
        case DEVICE_INIT:
            _nested_mouse_init_buttons(device);
            _nested_mouse_init_axes(device);
            break;
        case DEVICE_ON:
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
            if (device->public.on)
                break;
            xf86FlushInput(pInfo->fd);
            xf86AddEnabledDevice(pInfo);
            device->public.on = TRUE;
            break;
        case DEVICE_OFF:
            xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);
            if (!device->public.on)
                break;
            xf86RemoveEnabledDevice(pInfo);
            pInfo->fd = -1;
            device->public.on = FALSE;
            break;
        case DEVICE_CLOSE:
            /* free what we have to free */
            break;
    }

    return Success;
}

static void 
NestedMouseReadInput(InputInfoPtr pInfo) {
}

//Helper func to load mouse driver at the init of nested video driver
void Load_Nested_Mouse(pointer module) {
    xf86Msg(X_INFO, "NESTED MOUSE LOADING\n");
    xf86AddInputDriver(&NESTEDMOUSE, module, 0);
}
