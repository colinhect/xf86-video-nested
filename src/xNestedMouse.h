
typedef struct {
    int x;
    int y;
} NestedMouseMotionData;

typedef union {
    NestedMouseMotionData mouseMotion;
} NestedInputData;

typedef union {
    int type;
    NestedInputData data;
} NestedInputEvent;

void Load_Nested_Mouse(pointer module);

void NestedPostInputEvent(NestedInputEvent event);
