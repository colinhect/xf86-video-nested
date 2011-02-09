
#define NestedMouseMotion 1
#define NestedKeyPress 2

typedef struct {
    int x;
    int y;
} NestedMouseMotionData;

typedef struct {
    char key;
} NestedKeyPressData;

typedef union {
    NestedMouseMotionData mouseMotion;
    NestedKeyPressData keyPress;
} NestedInputData;

typedef struct {
    int type;
    NestedInputData data;
} NestedInputEvent;

void Load_Nested_Mouse(pointer module);

void NestedPostInputEvent(NestedInputEvent event);
