// ==============================================================================
//	fingerpinger.cpp
//	
//	tracking fingers on a macbook trackpad
//	
//	Authors:	Michael & Max Egger
//	Copyright:	2009 [ a n y m a ]
//	Website:	www.anyma.ch
//
//  Based on: 	http://www.steike.com/code/multitouch/
//	
//	License:	GNU GPL 2.0 www.gnu.org
//	
//	Version:	2009-06-01
// ==============================================================================

#include "ext.h"  				// you must include this - it contains the external object's link to available Max functions
#include "ext_obex.h"
#include "ext_common.h"
#include "jgraphics.h"
#include "ext_boxstyle.h"

#include <assert.h>
#include <list>
#include <map>
#include <vector>
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>
#include "MultitouchSupport.h"
#include <ext_critical.h>
#include <dlfcn.h>
#include <math.h>

using namespace std;

#define DEBUG_INSTANCE_MAP 1


// ==============================================================================
// Helper structure for one frame of finger data, with simple reference counting
// ------------------------------------------------------------------------------

typedef struct _FingeFrame {
	int refcount;
	int size;
	Finger finger[0];	// Variable size, number of elements given in 'size'
} FingerFrame;

static FingerFrame* make_frame(int nFingers, Finger* data)
{
	FingerFrame* frame = (FingerFrame*)::malloc(sizeof(FingerFrame) + nFingers*sizeof(Finger));
	if (!frame) return 0;	// allocation failed
	
	frame->refcount = 1;
	frame->size = nFingers;
	::memcpy(frame->finger, data, nFingers*sizeof(Finger));
	return frame;
}

static FingerFrame* retain_frame(FingerFrame* frame) {
	++frame->refcount;
	return frame;
}

static void release_frame(FingerFrame* frame)
{
	assert(frame->refcount >= 1);
	if (--frame->refcount <= 0) {
		::free(frame);
	}
}


// Can't use the global critical section, because sometimes it's locked from the
// outside during a free. This in turn leads to dead locks in the multitouch
// callback function. Instead, we create our own in main(), which wee will leak
// because there's no place where we can free it.
t_critical crit;

typedef void (*MTDeviceStartFn)(MTDeviceRef, int);

static MTDeviceStartFn pMTDeviceStart = nullptr;

static void ensure_mt_symbols()
{
    static bool resolved = false;
    if (resolved) return;
    resolved = true;

    void* handle = dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport", RTLD_LAZY);
    if (!handle) return;

    pMTDeviceStart = (MTDeviceStartFn)dlsym(handle, "MTDeviceStart");
}


// ==============================================================================
// RAII Helpers for critical section handling
// ------------------------------------------------------------------------------
namespace {
	struct lock_critical {
		lock_critical() { critical_enter(crit); }
		~lock_critical() { critical_exit(crit); }
	};

	struct unlock_critical {
		unlock_critical() { critical_exit(crit); }
		~unlock_critical() { critical_enter(crit); }
	};
}

// ==============================================================================
// Our External's Memory structure
// ------------------------------------------------------------------------------

typedef std::list<FingerFrame*> FingerQueue;

typedef struct _multitouch				// defines our object's internal variables for each instance in a patch
{
    t_jbox box;                 // ← replaces t_object

	void 			*out_list;			// outlet for finger data		
	void			*out_bang;			// bang out for start of frame
	void			*evt_qelem;			// qelem to trigger output of finger events
	MTDeviceRef 	dev;				// reference to the multitouch device
	FingerQueue		*evts;				// queue of finger frames we still have to report
    
    long enabled;
    char draw_circles;
    
    // UI-only state
    std::vector<Finger> draw_fingers;
    
    std::vector<long> press_order; // finger identifiers, oldest → newest
	
    long rows;     // number of rows to draw (0 = none)
    long columns;  // number of columns to draw (0 = none)

} t_multitouch;

static t_class *s_multitouch_class = NULL;	// global pointer to the object class - so max can reference the object


typedef std::list<t_multitouch*> InstanceList;
typedef std::map<MTDeviceRef, InstanceList> InstanceMap;

/*! Maps each open multitouch device to a list of fingerpinger instances.
    It is used in the callback function to find out which objects need to
    output the data. Declared as a pointer so we don't have to rely on Max
	calling the constructor. */
static InstanceMap* instances = NULL;	
													

// ==============================================================================
// Function Prototypes
// ------------------------------------------------------------------------------

extern "C" {int C74_EXPORT main(void);}

void *multitouch_new(t_symbol *s, long argc, t_atom *argv);
static void multitouch_free(t_multitouch *x);
static void multitouch_int(t_multitouch *x,long n);
static void multitouch_output(t_multitouch *x);

static void multitouch_open_device(t_multitouch* x, MTDeviceRef dev);
static void multitouch_close_device(t_multitouch* x, MTDeviceRef dev);

// ==============================================================================
// Implementation
// ------------------------------------------------------------------------------


//--------------------------------------------------------------------------
// - Callback
//--------------------------------------------------------------------------

static int callback(MTDeviceRef device, Finger *data, int nFingers, double timestamp, int frame) {
//    post("fingerpinger: callback dev=%p fingers=%d", device, nFingers);
	// critical section 0 must be locked at any time we access 'instances' or a t_multitouch
	lock_critical lock;
	if (instances == NULL) return 0;	// May happen if we are currently closing the last device
	
	InstanceMap::iterator it = instances->find(device);
	if (it == instances->end()) return 0;	// May happen if we are currently closing the device
	
	// Copy the finger data into a new frame structure
	FingerFrame* new_frame = make_frame(nFingers, data);
	if (!new_frame) return 0;
	
	// Append the frame data to the queues of all concerned fingerpinger instances
	for (InstanceList::iterator ob = it->second.begin(); ob != it->second.end(); ++ob) {
		(*ob)->evts->push_back(retain_frame(new_frame));
		qelem_set((*ob)->evt_qelem);
	}
	
	// release our reference to the frame we got from make_frame()
	release_frame(new_frame);
	
	return 0;
}

static void multitouch_output(t_multitouch *x)
{
    t_atom myList[14];

    while (true) {
        FingerFrame* frame = nullptr;

        // --- locked: only touch the queue here ---
        {
            lock_critical lock;
            if (x->evts->empty())
                break;

            frame = x->evts->front();
            x->evts->pop_front();
            x->draw_fingers.assign(frame->finger, frame->finger + frame->size);
        }
        

        // --- unlocked: safe to output ---
        outlet_bang(x->out_bang);

        for (int i = 0; i < frame->size; ++i) {
            Finger& f = frame->finger[i];
            
            long col = -1;
            long row = -1;

            if (x->columns > 0) {
                col = (long)floor(f.normalized.pos.x * x->columns);
                if (col < 0) col = 0;
                if (col >= x->columns) col = x->columns - 1;
            }

            if (x->rows > 0) {
                // Y is inverted visually, but grid math should match drawing
                row = (long)floor((1.0 - f.normalized.pos.y) * x->rows);
                if (row < 0) row = 0;
                if (row >= x->rows) row = x->rows - 1;
            }

            atom_setfloat(myList + 0, (float)i);
            atom_setfloat(myList + 1, (float)f.frame);
            atom_setfloat(myList + 2, f.angle);
            atom_setfloat(myList + 3, f.majorAxis);
            atom_setfloat(myList + 4, f.minorAxis);
            atom_setfloat(myList + 5, f.normalized.pos.x);
            atom_setfloat(myList + 6, f.normalized.pos.y);
            atom_setfloat(myList + 7, f.normalized.vel.x);
            atom_setfloat(myList + 8, f.normalized.vel.y);
            atom_setfloat(myList + 9, (float)f.identifier);
            atom_setfloat(myList +10, (float)f.state);
            atom_setfloat(myList +11, f.size);
            
            atom_setfloat(myList + 12, (float)row);
            atom_setfloat(myList + 13, (float)col);

            outlet_list(x->out_list, 0L, 14, myList);
        }

        release_frame(frame);
        jbox_invalidate_layer((t_object *)x, NULL, gensym("fingerlayer"));
        jbox_redraw(&x->box);
    }
}

//--------------------------------------------------------------------------
// - On / Off
//--------------------------------------------------------------------------
#ifdef DEBUG_INSTANCE_MAP
static void print_instances(t_multitouch const* x)
{
	post("instances: ");
	if (!instances) {
		post("NULL");
		return;
	}
	for (InstanceMap::const_iterator it = instances->begin(); it != instances->end(); ++it) {
		post("%p ->", it->first);
		for (InstanceList::const_iterator ob = it->second.begin(); ob != it->second.end(); ++ob)
			post(" %p,", (*ob));
	}
}
#endif

static void multitouch_open_device(t_multitouch* x, MTDeviceRef dev)
{
	// NOTE: We assume that we are in critical section 0 when entering this function!
	
	assert(dev);
	// Create instance map if there is none.
	if (!instances) instances = new InstanceMap;
	
	// Check if this device is already in the map.
	InstanceMap::iterator i = instances->find(dev);
	if (i == instances->end()) {
		// If not, open the device and insert it into the map
		CFRetain(dev);
		(*instances)[dev] = InstanceList(1, x);
		x->dev = dev;
		
		// Exit critical section during open to avoid potential deadlocks in callback;
		// WARNING: we must assume that all iterators become invalid at this point
		// because another thread might modify 'instances' at any time!
		{
			unlock_critical unlock;		
			MTRegisterContactFrameCallback(dev, callback);
            ensure_mt_symbols();
            if (pMTDeviceStart) {
                pMTDeviceStart(dev, 0);
            }


            post("fingerpinger: started device %p", dev);
		}
	}
	else {
		// If the device is already in the list, just add the fingerpinger instance
		// to its list of client devices.
		i->second.push_back(x);
		x->dev = dev;
	}
//#ifdef DEBUG_INSTANCE_MAP
//	print_instances(x);
//#endif
}

static void multitouch_close_device(t_multitouch* x, MTDeviceRef dev)
{
	// NOTE: We assume that we are in critical section 0 when entering this function!
	
	x->dev = NULL;
	if (!dev) return;
	assert(instances);
	if (!instances) return;
	
	// Find this instance in the device map.
	InstanceMap::iterator i = instances->find(dev);
	assert(i != instances->end());
	
	if (i != instances->end()) {
		// Find this instance in the list of clients of the device.
		InstanceList& obs = i->second;
		InstanceList::iterator ob = std::find(obs.begin(), obs.end(), x);
		assert(ob != obs.end());
		
		if (ob != obs.end()) {
			if (obs.size() > 1) {
				// If other instances still use this device, just remove
				// the instance from the client list.
				obs.erase(ob);
			}
			else {
				// If this is the last instance still using this device, close
				// the device and remove it completely from the instance map.
				instances->erase(i);

				// Exit critical section during close to avoid potential deadlocks in callback;
				// WARNING: we must assume that all iterators become invalid at this point,
				// because another thread might modify 'instances' at any time!
				{
					unlock_critical unlock;
					MTRegisterContactFrameCallback(dev, NULL);
					MTDeviceStop(dev);
					MTDeviceRelease(dev);
				}
			}
		}
	}
#ifdef DEBUG_INSTANCE_MAP
	print_instances(x);
#endif
	if (instances->empty()) {
		delete instances;
		instances = NULL;
	}
}


// everything above this in a separate file/struct/.c


static void finger_color(long id, double &r, double &g, double &b)
{
    double hue = fmod((double)(id * 137), 360.0); // golden-angle spread
    double s = 0.75;
    double v = 0.95;

    double c = v * s;
    double x = c * (1 - fabs(fmod(hue / 60.0, 2) - 1));
    double m = v - c;

    if (hue < 60)       { r = c; g = x; b = 0; }
    else if (hue < 120) { r = x; g = c; b = 0; }
    else if (hue < 180) { r = 0; g = c; b = x; }
    else if (hue < 240) { r = 0; g = x; b = c; }
    else if (hue < 300) { r = x; g = 0; b = c; }
    else                { r = c; g = 0; b = x; }

    r += m;
    g += m;
    b += m;
}

static void multitouch_paint(t_multitouch *x, t_object *view)
{
    t_rect r;
    jbox_get_rect_for_view((t_object *)&x->box, view, &r);

    t_jgraphics *g =
        jbox_start_layer((t_object *)x, view,
                         gensym("fingerlayer"),
                         r.width, r.height);

    if (g) {
        // background
        jgraphics_set_source_rgba(g, 0.12, 0.12, 0.12, 1.0);
        jgraphics_rectangle(g, 0, 0, r.width, r.height);
        jgraphics_fill(g);
        
        // ---------- grid ----------
        if (x->rows > 0 || x->columns > 0) {
            jgraphics_set_source_rgba(g, 1, 1, 1, 0.15);
            jgraphics_set_line_width(g, 1.0);

            if (x->columns > 0) {
                double dx = r.width / (double)x->columns;
                for (long c = 1; c < x->columns; ++c) {
                    double xpx = dx * c;
                    jgraphics_move_to(g, xpx, 0);
                    jgraphics_line_to(g, xpx, r.height);
                }
            }

            if (x->rows > 0) {
                double dy = r.height / (double)x->rows;
                for (long r0 = 1; r0 < x->rows; ++r0) {
                    double ypx = dy * r0;
                    jgraphics_move_to(g, 0, ypx);
                    jgraphics_line_to(g, r.width, ypx);
                }
            }

            jgraphics_stroke(g);
        }

        // draw fingers
        for (auto &f : x->draw_fingers) {
            double xpx = f.normalized.pos.x * r.width;
            double ypx = (1.0 - f.normalized.pos.y) * r.height;
            double rad = 6 + f.size * 20;

            double cr, cg, cb;
            finger_color(f.identifier, cr, cg, cb);
            
            
            if (x->rows > 0 && x->columns > 0) {
                double cell_w = r.width  / (double)x->columns;
                double cell_h = r.height / (double)x->rows;

                // compute column (0 … columns-1)
                long col = (long)floor(f.normalized.pos.x * x->columns);
                if (col < 0) col = 0;
                if (col >= x->columns) col = x->columns - 1;

                // compute row (0 … rows-1), note Y inversion
                long row = (long)floor((1.0 - f.normalized.pos.y) * x->rows);
                if (row < 0) row = 0;
                if (row >= x->rows) row = x->rows - 1;

                double cx = col * cell_w;
                double cy = row * cell_h;

                jgraphics_set_source_rgba(g, cr, cg, cb, f.size);
                jgraphics_rectangle(g, cx, cy, cell_w, cell_h);
                jgraphics_fill(g);
            }
            
            if(x->draw_circles) {
                jgraphics_set_source_rgba(g, cr, cg, cb, 0.9);
                jgraphics_arc(g, xpx, ypx, rad, 0, JGRAPHICS_2PI);
                jgraphics_fill(g);
                
                if(true) { // draw angles
                    
                    // angle "clock hand"
                    double len = rad;
                    double dx = cos(f.angle) * len;
                    double dy = -sin(f.angle) * len;
                    
                    jgraphics_set_source_rgba(g, 0.12, 0.12, 0.12, 1.0);
                    jgraphics_set_line_width(g, 2.0);
                    jgraphics_move_to(g, xpx, ypx);
                    jgraphics_line_to(g, xpx + dx, ypx + dy);
                    jgraphics_stroke(g);
                }
            }
        }
        
        // ---------- connecting lines (index order) ----------
//        if (x->draw_fingers.size() > 1) {
//            jgraphics_set_source_rgba(g, 1.0, 1.0, 1.0, 0.6);
//            jgraphics_set_line_width(g, 3.0);
//
//            // start at finger 0
//            {
//                auto &f0 = x->draw_fingers[0];
//                double x0 = f0.normalized.pos.x * r.width;
//                double y0 = (1.0 - f0.normalized.pos.y) * r.height;
//                jgraphics_move_to(g, x0, y0);
//            }
//
//            // connect subsequent fingers
//            for (size_t i = 1; i < x->draw_fingers.size(); ++i) {
//                auto &f = x->draw_fingers[i];
//                double x = f.normalized.pos.x * r.width;
//                double y = (1.0 - f.normalized.pos.y) * r.height;
//                jgraphics_line_to(g, x, y);
//            }
//
//            jgraphics_stroke(g);
//        }

        
        // focus border
        if (x->enabled) {
            const double border = 5.0;
            const double inset = border * 0.5;

            jgraphics_set_source_rgba(g, 1.0, 0.0, 0.0, 1.0);
            jgraphics_set_line_width(g, border);

            jgraphics_rectangle(
                g,
                inset,
                inset,
                r.width  - border,
                r.height - border
            );

            jgraphics_stroke(g);
        }

        jbox_end_layer((t_object *)x, view, gensym("fingerlayer"));
    }

    jbox_paint_layer((t_object *)x, view,
                     gensym("fingerlayer"), 0, 0);
}

static void multitouch_set_cursor(t_multitouch *x, t_object *patcherview, char active)
{
    if (!patcherview)
        return;

    if (active)
        jmouse_setcursor(patcherview, (t_object *)x, JMOUSE_CURSOR_NONE);
    else
        jmouse_setcursor(patcherview, (t_object *)x, JMOUSE_CURSOR_ARROW);
}

static void multitouch_mousemove(t_multitouch *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                 long modifiers)
{
    if (!x->enabled)
        return;

    t_rect r;
    jbox_get_rect_for_view((t_object *)&x->box, patcherview, &r);

    double lx = CLAMP(pt.x - r.x, 0, r.width);
    double ly = CLAMP(pt.y - r.y, 0, r.height);


    jmouse_setposition_box(patcherview, (t_object *)x, r.width / 2, r.height / 2);
}


static void multitouch_mousedown(t_multitouch *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                 long modifiers)
{
    if (!x->enabled) {
        jbox_grabfocus(&x->box);
        multitouch_int(x, 1);
        multitouch_set_cursor(x, patcherview, 1); // hide cursor
    }
    else {
        multitouch_int(x, 0);
        multitouch_set_cursor(x, patcherview, 0); // show cursor
    }
}


static void multitouch_key(t_multitouch *x,
                           t_object *patcherview,
                           long keycode,
                           long modifiers,
                           long textcharacter)
{
    // ESC by keycode or textcharacter depending on what you want:
    if (textcharacter == 27) {
        multitouch_int(x, 0);
        multitouch_set_cursor(x, patcherview, 0); // show cursor
        // return 1;   // <-- CRITICAL
    }
    // return 0;
}

static void multitouch_int(t_multitouch *x, long n) {
	if (n < 0) n = 0;
	lock_critical lock;
	if (x->dev) multitouch_close_device(x, x->dev);
	if (n) {
        CFArrayRef devlist = MTDeviceCreateList();
        if (devlist) {
            CFIndex cnt = CFArrayGetCount(devlist);
//            post("fingerpinger: seen %d multitouch devices", (int)cnt);

            if (cnt > 0) {
                CFIndex index = (n > 0) ? (n - 1) : (cnt - 1);
                if (index < 0) index = 0;
                if (index >= cnt) index = cnt - 1;

                multitouch_open_device(x, (MTDeviceRef)CFArrayGetValueAtIndex(devlist, index));
            }

            CFRelease(devlist);
        }
	}
    
    x->enabled = n > 0;
    
    jbox_invalidate_layer((t_object *)x, NULL, gensym("fingerlayer"));
    jbox_redraw(&x->box);
}

static void multitouch_focusgained(t_multitouch *x, t_object *pv) {
//    post("fingerpinger: focus gained");
}

static void multitouch_focuslost(t_multitouch *x, t_object *pv) {
//    post("fingerpinger: focus lost");
    multitouch_set_cursor(x, pv, 0);
}


//--------------------------------------------------------------------------
// - Object creation and setup
//--------------------------------------------------------------------------

int C74_EXPORT main(void){
	critical_new(&crit);
    
    
    t_class *c;
    

    c = class_new("ll_trackpad",
                  (method)multitouch_new,
                  (method)multitouch_free,
                  sizeof(t_multitouch),
                  0L,
                  A_GIMME,
                  0);

    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;

    // enable drawing + mouse/key
    jbox_initclass(c, JBOX_DRAWFIRSTIN);

    class_addmethod(c, (method)multitouch_int, "int", A_LONG, 0);
    class_addmethod(c, (method)multitouch_paint, "paint", A_CANT, 0);
    class_addmethod(c, (method)multitouch_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)multitouch_mousemove, "mousemove", A_CANT, 0);

    class_addmethod(c, (method)multitouch_key, "key", A_CANT, 0);
    class_addmethod(c, (method)multitouch_focusgained, "focusgained", A_CANT, 0);
    class_addmethod(c, (method)multitouch_focuslost,   "focuslost",   A_CANT, 0);


    CLASS_ATTR_DEFAULT(c,"patching_rect",0, "0. 0. 200. 100.");
    
    CLASS_ATTR_LONG(c, "rows", 0, t_multitouch, rows);
    CLASS_ATTR_FILTER_CLIP(c, "rows", 0, 1024);
    CLASS_ATTR_DEFAULT(c, "rows", 0, "0");
    CLASS_ATTR_SAVE(c, "rows", 0);

    CLASS_ATTR_LONG(c, "columns", 0, t_multitouch, columns);
    CLASS_ATTR_FILTER_CLIP(c, "columns", 0, 1024);
    CLASS_ATTR_DEFAULT(c, "columns", 0, "0");
    CLASS_ATTR_SAVE(c, "columns", 0);
    
    CLASS_ATTR_CHAR(c,              "drawcircles", 0, t_multitouch, draw_circles);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c,"drawcircles", 0, "0");
    CLASS_ATTR_STYLE_LABEL(c,       "drawcircles", 0, "onoff", "Draw circles where fingers pressed");

    class_register(CLASS_BOX, c);
    s_multitouch_class = c;

    
    post("fingerpinger version 20170812 - (cc) 2017 [ a n y m a ]",0);    // post any important info to the max window when our object is loaded
return 1;
}

//--------------------------------------------------------------------------
// - Object creation
//--------------------------------------------------------------------------

void *multitouch_new(t_symbol *s, long argc, t_atom *argv)
{
    t_multitouch *x = (t_multitouch *)object_alloc(s_multitouch_class);
    if (!x) return NULL;

    x->enabled = 0;
    x->draw_circles = 1;
    x->rows = 0;
    x->columns = 0;
    
    x->dev = NULL;

    long flags = JBOX_DRAWFIRSTIN | JBOX_GROWY | JBOX_GROWBOTH | JBOX_HILITE;
    jbox_new(&x->box, flags, argc, argv);
    x->box.b_firstin = (t_object *)x;

    x->out_bang = bangout(x);
    x->out_list = listout(x);
    x->dev = NULL;
    x->evt_qelem = qelem_new(x, (method)multitouch_output);
    x->evts = new FingerQueue;
    
    jbox_ready(&x->box);
    return x;
}

//--------------------------------------------------------------------------
// - Object destruction
//--------------------------------------------------------------------------

static void multitouch_free(t_multitouch *x)
{
	multitouch_int(x, 0);										// make sure callback is released before deleting the object
	delete x->evts;
	qelem_free(x->evt_qelem);
    
    jbox_free(&x->box);
}




