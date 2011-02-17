#include "xf86Xinput.h"

void Load_Nested_Mouse(NestedClientPrivatePtr clientData);

InputInfoPtr NestedMousePreInit(InputDriverPtr drv, IDevPtr dev, int flags);
void NestedMouseUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);

void NestedPostMouseMotion(void* dev, int x, int y);
void NestedPostMouseButton(void* dev, int button, int isDown);
