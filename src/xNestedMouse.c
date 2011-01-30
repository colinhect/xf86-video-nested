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

static void NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo,
int flags);

_X_EXPORT InputDriverRec MOUSE = {
    1,
    "random",
    NULL,
    NestedMousePreInit,
    NestedMouseUnInit,
    NULL,
    0,
};

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
