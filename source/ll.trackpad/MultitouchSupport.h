// MultitouchSupport.h

#ifndef MULTITOUCH_INCLUDED
#define  MULTITOUCH_INCLUDED


#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x,y; } mtPoint;
typedef struct { mtPoint pos,vel; } mtReadout;

typedef struct {
  int frame;
  double timestamp;
  int identifier, state, foo3, foo4;
  mtReadout normalized;
  float size;
  int zero1;
  float angle, majorAxis, minorAxis; // ellipsoid
  mtReadout mm;
  int zero2[2];
  float unk2;
} Finger;

typedef void* MTDeviceRef;
typedef int (*MTContactCallbackFunction)(MTDeviceRef,Finger*,int,double,int);


MTDeviceRef MTDeviceCreateDefault();
CFArrayRef MTDeviceCreateList();
void MTRegisterContactFrameCallback(MTDeviceRef, MTContactCallbackFunction);
void MTDeviceStart(MTDeviceRef);
void MTDeviceStop(MTDeviceRef);
void MTDeviceRelease(MTDeviceRef);
long MTDeviceGetFamilyID(MTDeviceRef);

#ifdef __cplusplus
}
#endif

#endif