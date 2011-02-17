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

#include "config.h"

#include "client.h"

#include "xNestedMouse.h"

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

static pointer NestedMousePlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void NestedMouseUnplug(pointer p);
static void NestedMouseReadInput(InputInfoPtr pInfo);
static int NestedMouseControl(DeviceIntPtr    device,int what);
static int _nested_mouse_init_buttons(DeviceIntPtr device);
static int _nested_mouse_init_axes(DeviceIntPtr device);

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

InputInfoPtr 
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
                                            "/dev/random");

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
    
    pInfo->flags |= XI86_OPEN_ON_INIT;
    pInfo->flags |= XI86_CONFIGURED;
    return pInfo;
}

void
NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags) {
}

static pointer
NestedMousePlug(pointer module, pointer options, int *errmaj, int  *errmin) {
    //xf86AddInputDriver(&NESTEDMOUSE, module, 0);
    return module;
}

static void
NestedMouseUnplug(pointer p) {
}

static int
_nested_mouse_init_buttons(DeviceIntPtr device) {
        
InputInfoPtr        pInfo = device->public.devicePrivate;
    CARD8               *map;
    int                 i;
    int                 ret = Success;
    const int           num_buttons = 2;

    Atom btn_labels[2] = {0};

    btn_labels[0] = "left";
    btn_labels[1] = "right";

    map = xcalloc(num_buttons, sizeof(CARD8));

    for (i = 0; i < num_buttons; i++)
        map[i] = i;

    if (!InitButtonClassDeviceStruct(device, num_buttons, btn_labels,  map)) {
            xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
            ret = BadAlloc;
    }


    if (!InitKeyboardDeviceStruct(device, NULL, NULL, NULL)) {
            xf86Msg(X_ERROR, "%s: Failed to register keyboard.\n", pInfo->name);
            ret = BadAlloc;
    } 

    xfree(map);
    return ret;
return -1;
}

static int
_nested_mouse_init_axes(DeviceIntPtr device) {
   InputInfoPtr        pInfo = device->public.devicePrivate;
    int                 i;
    const int           num_axes = 2;

    if (!InitValuatorClassDeviceStruct(device,
                num_axes,
                GetMotionHistory,
                GetMotionHistorySize(),
                0))
        return BadAlloc;

    pInfo->dev->valuator->mode = Relative;
    if (!InitAbsoluteClassDeviceStruct(device))
            return BadAlloc;

    for (i = 0; i < 2; i++) {
            xf86InitValuatorAxisStruct(device, i, "", -1, -1, 1, 1, 1);
            xf86InitValuatorDefaults(device, i);
    }

    return Success; 
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
void Load_Nested_Mouse(NestedClientPrivatePtr clientData) {
    xf86Msg(X_INFO, "NESTED MOUSE LOADING\n");

    //xf86AddInputDriver(&NESTEDMOUSE, module, 0);

    // Create input options for our invocation to NewInputDeviceRequest().   
    InputOption* options = (InputOption*)xalloc(sizeof(InputOption));
    
    options->key = "driver";
    options->value = "nestedmouse";

    options->next = (InputOption*)xalloc(sizeof(InputOption));
    
    options->next->key = "identifier";
    options->next->value = "nestedmouse";
    options->next->next = NULL;

    DeviceIntPtr dev;
    int ret = NewInputDeviceRequest(options, NULL, &dev);
    
    if (ret != Success) {
        xf86Msg(X_ERROR, "Failed to load input driver.\n");
    }

    NestedMouseControl(dev, DEVICE_ON); 

    InputInfoPtr pInfo = dev->public.devicePrivate;

    NestedClientSetDevicePtr(clientData, pInfo->dev);

    xf86Msg(X_INFO, "NESTED MOUSE LOADING DONE\n");
}
    
void NestedPostMouseMotion(void* dev, int x, int y) {
    xf86PostMotionEvent(dev, TRUE, 0, 2, x, y);
}

void NestedPostMouseButton(void* dev, int button, int isDown) {
    xf86PostButtonEvent(dev, 0, button, isDown, 0, 0);
}

void NestedPostKey(void* dev, unsigned int keycode, int isDown) {
    xf86PostKeyboardEvent(dev, keycode, isDown);
}
