
extern int NestedMouseMotion;
extern int NestedKeyPress;

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

typedef union {
    int type;
    NestedInputData data;
} NestedInputEvent;

void Load_Nested_Mouse(pointer module);

void NestedPostInputEvent(NestedInputEvent event);
