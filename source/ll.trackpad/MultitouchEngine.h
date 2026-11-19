#pragma once

#include <list>
#include <CoreFoundation/CoreFoundation.h>
#include "MultitouchSupport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Raw frame (engine-owned)
typedef struct _FingerFrame {
    int refcount;
    int size;
    Finger finger[0];
} FingerFrame;

// Engine lifecycle
void mt_engine_init();
void mt_engine_shutdown();

// Device control
void mt_engine_open_device(MTDeviceRef dev);
void mt_engine_close_device(MTDeviceRef dev);

// Frame queue access (UI thread)
FingerFrame* mt_engine_pop_frame();   // returns retained frame or NULL
void         mt_engine_release_frame(FingerFrame* frame);

#ifdef __cplusplus
}
#endif
