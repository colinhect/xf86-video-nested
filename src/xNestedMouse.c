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

static XF86ModuleVersionInfo NestedMouseVersionRec =
{
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

_X_EXPORT XF86ModuleData nestedMouseModuleData =
{
    &NestedMouseVersionRec,
    &NestedMouseVersionRecPlug,
    &NestedMouseVersionRecUnplug
};

static InputInfoPtr 
NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr        pInfo;
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
    if (pInfo->fd == -1)
    {
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
NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
}

static pointer
NestedMousePlug(pointer module, pointer options, int *errmaj, int  *errmin)
{
    xf86AddInputDriver(&NESTEDMOUSE, module, 0);
    return module;
};

static void
NestedMouseUnplug(pointer p)
{
};

static int
_nested_mouse_init_buttons(DeviceIntPtr device)
{
    return NULL;
}

static int
_nested_mouse_init_axes(DeviceIntPtr device)
{
    return NULL;
}

static int 
NestedMouseControl(DeviceIntPtr device, int what)
{
    return NULL;
}

static void 
NestedMouseReadInput(InputInfoPtr pInfo)
{
}
