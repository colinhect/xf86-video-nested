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
 * Authors: Paulo Zanoni <pzanoni@mandriva.com>, Timothy Fleck,
 *          Colin Cornaby, Weseung Hwang, Colin Hill, Nathaniel Way,
 *          Tuan Thong Bui.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
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
#include <xorg/xf86_OSproc.h>

#include "config.h"

#include "client.h"
#include "input.h"

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

#define NUM_MOUSE_BUTTONS 6
#define NUM_MOUSE_AXES 2

static pointer
NestedInputPlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void
NestedInputUnplug(pointer p);

static void
NestedInputReadInput(InputInfoPtr pInfo);
static int
NestedInputControl(DeviceIntPtr    device,int what);

static int
_nested_input_init_keyboard(DeviceIntPtr device);
static int
_nested_input_init_buttons(DeviceIntPtr device);
static int
_nested_input_init_axes(DeviceIntPtr device);

typedef struct _NestedInputDeviceRec {
    char *device;
    int   version;
} NestedInputDeviceRec, *NestedInputDevicePtr;

static XF86ModuleVersionInfo NestedInputVersionRec = {
    "nestedinput",
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

_X_EXPORT XF86ModuleData nestedInputModuleData = {
    &NestedInputVersionRec,
    &NestedInputPlug,
    &NestedInputUnplug
};

InputInfoPtr 
NestedInputPreInit(InputDriverPtr drv, IDevPtr dev, int flags) {
    InputInfoPtr         pInfo;
    NestedInputDevicePtr pNestedInput;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    pNestedInput = calloc(1, sizeof(NestedInputDeviceRec));

    if (!pNestedInput) {
        pInfo->private = NULL;
        xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    pInfo->private = pNestedInput;
    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = XI_MOUSE; // This is really both XI_MOUSE and XI_KEYBOARD... but oh well.
    pInfo->conf_idev = dev;
    pInfo->read_input = NestedInputReadInput; // new data available.
    pInfo->switch_mode = NULL; // Toggle absolute/relative mode.
    pInfo->device_control = NestedInputControl; // Enable/disable device.

    // Process driver specific options.
    pNestedInput->device = xf86SetStrOption(dev->commonOptions,
                                            "Device",
                                            "/dev/null");

    xf86Msg(X_INFO, "%s: Using device %s.\n", pInfo->name, pNestedInput->device);

    // Process generic options.
    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);
    
    // Open sockets, init device files, etc.
    SYSCALL(pInfo->fd = open(pNestedInput->device, O_RDWR | O_NONBLOCK));
    
    if (pInfo->fd == -1) {
        xf86Msg(X_ERROR, "%s: failed to open %s.",
                pInfo->name, pNestedInput->device);

        pInfo->private = NULL;

        free(pNestedInput);
        xf86DeleteInput(pInfo, 0);

        return NULL;
    }
    
    pInfo->flags |= XI86_OPEN_ON_INIT;
    pInfo->flags |= XI86_CONFIGURED;
    
    return pInfo;
}

void
NestedInputUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags) {
}

static pointer
NestedInputPlug(pointer module, pointer options, int *errmaj, int  *errmin) {
    return NULL;
}

static void
NestedInputUnplug(pointer p) {
}

static int
_nested_input_init_keyboard(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    
    if (!InitKeyboardDeviceStruct(device, NULL, NULL, NULL)) {
        xf86Msg(X_ERROR, "%s: Failed to register keyboard.\n", pInfo->name);
        return BadAlloc;
    }

    return Success;
}
static int
_nested_input_init_buttons(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    CARD8       *map;
    Atom         buttonLabels[NUM_MOUSE_BUTTONS] = {0};

    map = calloc(NUM_MOUSE_BUTTONS, sizeof(CARD8));

    int i;
    for (i = 0; i < NUM_MOUSE_BUTTONS; i++)
        map[i] = i;

    if (!InitButtonClassDeviceStruct(device, NUM_MOUSE_BUTTONS, buttonLabels, map)) {
        xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
        
        free(map);
        return BadAlloc;
    }

    return Success;
}

static int
_nested_input_init_axes(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;

    if (!InitValuatorClassDeviceStruct(device,
                                       NUM_MOUSE_AXES,
                                       (Atom*)GetMotionHistory, // Not sure about this.
                                       GetMotionHistorySize(),
                                       (Atom)0)) {
        return BadAlloc;
    }

    pInfo->dev->valuator->mode = Relative;
    if (!InitAbsoluteClassDeviceStruct(device))
        return BadAlloc;

    int i;
    for (i = 0; i < NUM_MOUSE_AXES; i++) {
        xf86InitValuatorAxisStruct(device, i, (Atom)0, -1, -1, 1, 1, 1);
        xf86InitValuatorDefaults(device, i);
    }

    return Success; 
}

static int 
NestedInputControl(DeviceIntPtr device, int what) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    NestedInputDevicePtr pNestedInput = pInfo->private;

    switch (what) {
        case DEVICE_INIT:
            _nested_input_init_keyboard(device);
            _nested_input_init_buttons(device);
            _nested_input_init_axes(device);
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
            break;
    }

    return Success;
}

static void 
NestedInputReadInput(InputInfoPtr pInfo) {
}

void
NestedInputLoadDriver(NestedClientPrivatePtr clientData) {
    
    // Create input options for our invocation to NewInputDeviceRequest.   
    InputOption* options = (InputOption*)malloc(sizeof(InputOption));
    
    options->key = "driver";
    options->value = "nestedinput";

    options->next = (InputOption*)malloc(sizeof(InputOption));
    
    options->next->key = "identifier";
    options->next->value = "nestedinput";
    options->next->next = NULL;

    // Invoke NewInputDeviceRequest to call the PreInit function of
    // the driver.
    DeviceIntPtr dev;
    int ret = NewInputDeviceRequest(options, NULL, &dev);
    
    if (ret != Success) {
        xf86Msg(X_ERROR, "Failed to load input driver.\n");
    }

    // Send the device to the client so that the client can send the
    // device back to the input driver when events are being posted.
    NestedClientSetDevicePtr(clientData, dev);
}
    
void
NestedInputPostMouseMotionEvent(DeviceIntPtr dev, int x, int y) {
    xf86PostMotionEvent(dev, TRUE, 0, 2, x, y);
}

void
NestedInputPostButtonEvent(DeviceIntPtr dev, int button, int isDown) {
    xf86PostButtonEvent(dev, 0, button, isDown, 0, 0);
}

void
NestedInputPostKeyboardEvent(DeviceIntPtr dev, unsigned int keycode, int isDown) {
    xf86PostKeyboardEvent(dev, keycode, isDown);
}
