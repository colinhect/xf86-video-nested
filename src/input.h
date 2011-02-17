#include "xf86Xinput.h"

// Loads the nested input driver.
void
NestedInputLoadDriver(NestedClientPrivatePtr clientData);

// Driver init functions.
InputInfoPtr
NestedInputPreInit(InputDriverPtr drv, IDevPtr dev, int flags);
void
NestedInputUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);

// Input event posting functions.
void
NestedInputPostMouseMotionEvent(void* dev, int x, int y);
void
NestedInputPostButtonEvent(void* dev, int button, int isDown);
void 
NestedInputPostKeyboardEvent(void* dev, unsigned int keycode, int isDown);

