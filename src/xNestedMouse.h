#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

typedef struct _NestedMouseDeviceRec
{
    char *device;
    int version;        /* Driver version */
    Atom* labels;
    int num_vals;
    int axes;
} NestedMouseDeviceRec, *NestedMouseDevicePtr ;


