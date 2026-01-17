#include "MultitouchEngine.h"

#include <list>
#include <cstring>
#include <cassert>
#include <dlfcn.h>
#include "ext_critical.h"

// ----------------------------------------------------------------------------
// Engine state
// ----------------------------------------------------------------------------
static t_critical g_crit;
static std::list<FingerFrame*> g_queue;
static MTDeviceRef g_dev = nullptr;

// ----------------------------------------------------------------------------
// Optional: private 2-arg MTDeviceStart on newer macOS
// ----------------------------------------------------------------------------
typedef void (*MTDeviceStartFn2)(MTDeviceRef, int);
static MTDeviceStartFn2 pMTDeviceStart2 = nullptr;

static void ensure_mt_symbols()
{
    static bool resolved = false;
    if (resolved) return;
    resolved = true;

    void* handle = dlopen(
        "/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
        RTLD_LAZY
    );
    if (handle)
        pMTDeviceStart2 = (MTDeviceStartFn2)dlsym(handle, "MTDeviceStart");
}

// ----------------------------------------------------------------------------
// FingerFrame helpers
// ----------------------------------------------------------------------------
static FingerFrame* make_frame(int n, Finger* data)
{
    auto* f = (FingerFrame*)malloc(sizeof(FingerFrame) + n * sizeof(Finger));
    if (!f) return nullptr;
    f->refcount = 1;
    f->size = n;
    memcpy(f->finger, data, n * sizeof(Finger));
    return f;
}

static FingerFrame* retain_frame(FingerFrame* f)
{
    ++f->refcount;
    return f;
}

void mt_engine_release_frame(FingerFrame* f)
{
    if (!f) return;
    if (--f->refcount <= 0)
        free(f);
}

// ----------------------------------------------------------------------------
// Callback (background thread)
// ----------------------------------------------------------------------------
static int callback(MTDeviceRef, Finger* data, int nFingers, double, int)
{
    critical_enter(g_crit);

    if (auto* frame = make_frame(nFingers, data))
        g_queue.push_back(frame);

    critical_exit(g_crit);
    return 0;
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void mt_engine_init()
{
    critical_new(&g_crit);
    ensure_mt_symbols();
}

void mt_engine_open_device(MTDeviceRef dev)
{
    if (!dev) return;

    // If already open, close first (prevents double-stop/release)
    if (g_dev)
        mt_engine_close_device(g_dev);

    // Retain so it stays valid after CFRelease(devlist)
    CFRetain(dev);
    g_dev = dev;

    MTRegisterContactFrameCallback(g_dev, callback);

    // Start device (2-arg private if available, else 1-arg public)
    if (pMTDeviceStart2) pMTDeviceStart2(g_dev, 0);
    else                MTDeviceStart(g_dev);
}

void mt_engine_close_device(MTDeviceRef dev)
{
    if (!dev) return;

    // Only close what we actually opened
    if (dev != g_dev)
        return;

    // Stop callbacks first, then stop device, then release
    MTRegisterContactFrameCallback(g_dev, nullptr);
    MTDeviceStop(g_dev);
    MTDeviceRelease(g_dev);   // matches CFRetain above

    g_dev = nullptr;

    // Optional: clear queued frames so UI doesn’t read stale data
    critical_enter(g_crit);
    while (!g_queue.empty()) {
        auto* f = g_queue.front();
        g_queue.pop_front();
        mt_engine_release_frame(f);
    }
    critical_exit(g_crit);
}

FingerFrame* mt_engine_pop_frame()
{
    critical_enter(g_crit);

    if (g_queue.empty()) {
        critical_exit(g_crit);
        return nullptr;
    }

    FingerFrame* f = g_queue.front();
    g_queue.pop_front();
    retain_frame(f);

    critical_exit(g_crit);
    return f;
}

