// ==============================================================================
//	ll_trackpad.cpp
//
//	tracking fingers on a macbook trackpad
//
//  Based on: 	fingerpinger max external by Michael & Max Egger
// ==============================================================================

#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"
#include "jgraphics.h"
#include "ext_boxstyle.h"

#include "MultitouchEngine.h"
#include "MultitouchSupport.h"

#include <vector>
#include <algorithm>
#include <math.h>
#include <unordered_map>
#include <unordered_set>
#include <CoreFoundation/CoreFoundation.h>

#define MT_POLL_INTERVAL_MS 5

using namespace std;

enum ControlMode {
    MODE_RAW = 0,
    MODE_XY,
    MODE_PADS
};

struct RectState {
    bool active;
    float x, y;
    float pressure;
    float velocity;
    double last_time;

    // add these:
    float touch_start_x, touch_start_y;
    float start_x, start_y;
    bool  absolute;
};

typedef struct _ll_trackpad
{
    t_jbox box;

	void *out_list;			// outlet for finger data
	void *out_bang;			// bang out for start of frame
    
    void *poll_clock;
    MTDeviceRef dev;	    // reference to the Multitouch device
    
    long enabled;
	
    long rows;     // number of rows to draw (0 = none)
    long columns;  // number of columns to draw (0 = none)

    long mode;

    void* draw_fingers;
    void* rects;
    void* finger_to_rect;
    
    char is_mouse_down;
    char waiting_for_first_touch;
    char waiting_for_clear;
    
    // colors
    t_jrgba bgcolor;
    t_jrgba fgcolor;
    t_jrgba textcolor;
    t_jrgba bordercolor;   // active border
    t_jrgba labelcolor;
    t_jrgba dotcolor;
    t_jrgba linecolor;     // xy crosshair line color

    // labels
    void* labels;          // std::vector<t_symbol*>*
    char  showlabels;

    // xy options
    long  showxline;       // 0=no 1=yes 2=fill
    long  showyline;       // 0=no 1=yes 2=fill
    double dotsize;        // radius in px (0 = none)

    
} t_ll_trackpad;

static t_class *s_ll_trackpad_class = NULL;

static inline std::vector<Finger>& DRAW_FINGERS(t_ll_trackpad* x)
{
    return *static_cast<std::vector<Finger>*>(x->draw_fingers);
}

static inline std::vector<RectState>& RECTS(t_ll_trackpad* x)
{
    return *static_cast<std::vector<RectState>*>(x->rects);
}

static inline std::unordered_map<long, long>& FINGER_TO_RECT(t_ll_trackpad* x)
{
    return *static_cast<std::unordered_map<long, long>*>(x->finger_to_rect);
}

static inline std::vector<t_symbol*>& LABELS(t_ll_trackpad* x)
{
    return *static_cast<std::vector<t_symbol*>*>(x->labels);
}

extern "C" {int C74_EXPORT main(void);}

void *ll_trackpad_new(t_symbol *s, long argc, t_atom *argv);
static void ll_trackpad_free(t_ll_trackpad *x);

static void ll_trackpad_int(t_ll_trackpad *x, long n);
static void ll_trackpad_poll(t_ll_trackpad* x);

static void ll_trackpad_paint(t_ll_trackpad *x, t_object *view);

static void ll_trackpad_set_cursor(t_ll_trackpad *x, t_object *patcherview, char active);

static void ll_trackpad_mousemove(t_ll_trackpad *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                  long modifiers);

static void ll_trackpad_mousedown(t_ll_trackpad *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                  long modifiers);

static void ll_trackpad_mouseup(t_ll_trackpad *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                  long modifiers);

static void ll_trackpad_key(t_ll_trackpad *x,
                           t_object *patcherview,
                           long keycode,
                           long modifiers,
                            long textcharacter);

static void ll_trackpad_focusgained(t_ll_trackpad *x, t_object *pv);
static void ll_trackpad_focuslost(t_ll_trackpad *x, t_object *pv);

t_max_err ll_trackpad_set_labels(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv);

void ll_trackpad_getdrawparams(t_ll_trackpad *x, t_object *patcherview, t_jboxdrawparams *params) {}


// Attribute setters
t_max_err ll_trackpad_set_rows(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv);
t_max_err ll_trackpad_set_columns(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv);
t_max_err ll_trackpad_set_mode(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv);

t_max_err ll_trackpad_notify(
    t_ll_trackpad *x,
    t_symbol *s,
    t_symbol *msg,
    void *sender,
    void *data
);

static inline long finger_to_rect_index(
    const Finger& f,
    long rows,
    long columns
) {
    if (rows <= 0 || columns <= 0)
        return -1;

    long col = (long)floor(f.normalized.pos.x * columns);
    long row = (long)floor((1.0 - f.normalized.pos.y) * rows);

    col = CLAMP(col, 0, columns - 1);
    row = CLAMP(row, 0, rows - 1);

    return row * columns + col; // 0-based
}

static void ll_trackpad_resize_rects(t_ll_trackpad* x)
{
    auto& rects = RECTS(x);
    auto& map   = FINGER_TO_RECT(x);

    long count = x->rows * x->columns;
    if (count < 0) count = 0;

    rects.resize((size_t)count);

    for (auto& r : rects) {
        r.active = false;
        r.x = 0.5f;
        r.y = 0.5f;
        r.pressure = 0.f;
        r.velocity = 0.f;
        r.start_x = r.x;
        r.start_y = r.y;
        r.touch_start_x = 0.f;
        r.touch_start_y = 0.f;
        r.absolute = false;
    }

    map.clear();
}

int C74_EXPORT main(void){

    mt_engine_init();
    
    t_class *c;

    c = class_new("ll_trackpad",
                  (method)ll_trackpad_new,
                  (method)ll_trackpad_free,
                  sizeof(t_ll_trackpad),
                  0L,
                  A_GIMME,
                  0);

    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;

    jbox_initclass(c, JBOX_DRAWFIRSTIN | JBOX_COLOR);

    class_addmethod(c, (method)ll_trackpad_int, "int", A_LONG, 0);
    class_addmethod(c, (method)ll_trackpad_paint, "paint", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_mousemove, "mousemove", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_mouseup, "mouseup", A_CANT, 0);

    class_addmethod(c, (method)ll_trackpad_key, "key", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_focusgained, "focusgained", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_focuslost,   "focuslost",   A_CANT, 0);
    
    class_addmethod(c, (method)ll_trackpad_getdrawparams, "getdrawparams", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_notify, "notify", A_CANT, 0);
    
    CLASS_ATTR_DEFAULT(c,"patching_rect",0, "0. 0. 200. 100.");

    // Behavior
    CLASS_STICKY_ATTR(c,            "category",  0, "ll_trackpad");
    
    CLASS_ATTR_LONG(c, "mode", 0, t_ll_trackpad, mode);
    CLASS_ATTR_ENUMINDEX(c, "mode", 0, "raw xy pads");
    CLASS_ATTR_ACCESSORS(c, "mode", NULL, ll_trackpad_set_mode);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "mode", 0, "0");
    
    CLASS_ATTR_LONG(c, "rows", 0, t_ll_trackpad, rows);
    CLASS_ATTR_FILTER_CLIP(c, "rows", 0, 1024);
    CLASS_ATTR_ACCESSORS(c, "rows", NULL, ll_trackpad_set_rows);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "rows", 0, "0");

    CLASS_ATTR_LONG(c, "columns", 0, t_ll_trackpad, columns);
    CLASS_ATTR_FILTER_CLIP(c, "columns", 0, 1024);
    CLASS_ATTR_ACCESSORS(c, "columns", NULL, ll_trackpad_set_columns);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "columns", 0, "0");
    
    CLASS_ATTR_CHAR(c, "showlabels", 0, t_ll_trackpad, showlabels);
    CLASS_ATTR_STYLE_LABEL(c, "showlabels", 0, "onoff", "Show Labels");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "showlabels", 0, "1");

    CLASS_ATTR_LONG(c, "showxline", 0, t_ll_trackpad, showxline);
    CLASS_ATTR_ENUMINDEX(c, "showxline", 0, "no yes fill");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "showxline", 0, "1");

    CLASS_ATTR_LONG(c, "showyline", 0, t_ll_trackpad, showyline);
    CLASS_ATTR_ENUMINDEX(c, "showyline", 0, "no yes fill");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "showyline", 0, "1");

    CLASS_ATTR_DOUBLE(c, "dotsize", 0, t_ll_trackpad, dotsize);
    CLASS_ATTR_FILTER_CLIP(c, "dotsize", 0.0, 200.0);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "dotsize", 0, "4.0");
    
    CLASS_ATTR_ATOM_VARSIZE(c, "labels", 0, t_ll_trackpad, box, box, 0);
    CLASS_ATTR_ACCESSORS(c, "labels", NULL, ll_trackpad_set_labels);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "labels", 0, "");
    
    // Colors
    CLASS_STICKY_ATTR(c,                "category", 0, "Color");

    CLASS_ATTR_RGBA(c, "bgcolor", 0, t_ll_trackpad, bgcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "bgcolor", 0, "0.12 0.12 0.12 1.");
    CLASS_ATTR_STYLE_LABEL(c,            "bgcolor", 0, "rgba", "Background Color");
    
    CLASS_ATTR_RGBA(c, "fgcolor", 0, t_ll_trackpad, fgcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "fgcolor", 0, "1. 1. 1. 1.");
    CLASS_ATTR_STYLE_LABEL(c, "fgcolor", 0, "rgba", "Foreground Color");


    CLASS_ATTR_RGBA(c, "textcolor", 0, t_ll_trackpad, textcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "textcolor", 0, "1. 1. 1. 1.");
    CLASS_ATTR_STYLE_LABEL(c,            "textcolor", 0, "rgba", "Text Color");

    CLASS_ATTR_RGBA(c, "bordercolor", 0, t_ll_trackpad, bordercolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "bordercolor", 0, "1. 0. 0. 1.");
    CLASS_ATTR_STYLE_LABEL(c,            "bordercolor", 0, "rgba", "Border Color");

    CLASS_ATTR_RGBA(c, "labelcolor", 0, t_ll_trackpad, labelcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "labelcolor", 0, "1. 1. 1. 0.4");
    CLASS_ATTR_STYLE_LABEL(c,            "labelcolor", 0, "rgba", "Label Color");

    CLASS_ATTR_RGBA(c, "dotcolor", 0, t_ll_trackpad, dotcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "dotcolor", 0, "1. 1. 1. 0.9");
    CLASS_ATTR_STYLE_LABEL(c,            "dotcolor", 0, "rgba", "Dot Color");

    CLASS_ATTR_RGBA(c, "linecolor", 0, t_ll_trackpad, linecolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "linecolor", 0, "1. 1. 1. 0.9");
    CLASS_ATTR_STYLE_LABEL(c,            "linecolor", 0, "rgba", "Line Color");
    
    class_register(CLASS_BOX, c);
    s_ll_trackpad_class = c;
    
    return 1;
}

void *ll_trackpad_new(t_symbol *s, long argc, t_atom *argv)
{
    t_dictionary *d = NULL;
    if (!(d = object_dictionaryarg(argc, argv)))
        return NULL;

    t_ll_trackpad *x = (t_ll_trackpad *)object_alloc(s_ll_trackpad_class);
    if (!x) return NULL;

    // ---- defaults (safe even if overwritten later) ----
    x->enabled = 0;
    x->is_mouse_down = 0;
    x->waiting_for_clear = 0;
    x->waiting_for_first_touch = 0;
    
    x->rows = 0;
    x->columns = 0;
    x->mode = MODE_RAW;
    x->dev = NULL;
    
    x->labels = new std::vector<t_symbol*>();
    x->showlabels = 1;

    x->showxline = 1; // yes
    x->showyline = 1; // yes
    x->dotsize = 4.0;

    long flags = JBOX_DRAWFIRSTIN | JBOX_GROWY | JBOX_GROWBOTH | JBOX_HILITE | JBOX_COLOR;
    jbox_new(&x->box, flags, argc, argv);
    x->box.b_firstin = (t_object *)x;

    // ---- allocate storage BEFORE attributes ----
    x->draw_fingers   = new std::vector<Finger>();
    x->rects          = new std::vector<RectState>();
    x->finger_to_rect = new std::unordered_map<long, long>();

    // ---- now safely restore attributes ----
    attr_dictionary_process(x, d);

    // ---- now that rows/columns exist, resize ----
    ll_trackpad_resize_rects(x);

    x->out_bang = bangout(x);
    x->out_list = listout(x);

    x->poll_clock = clock_new(x, (method)ll_trackpad_poll);

    jbox_ready(&x->box);
    return x;
}

static void ll_trackpad_free(t_ll_trackpad *x)
{
    ll_trackpad_int(x, 0);
    clock_unset(x->poll_clock);
    object_free(x->poll_clock);
    
    delete static_cast<std::vector<Finger>*>(x->draw_fingers);
    delete static_cast<std::vector<RectState>*>(x->rects);
    delete static_cast<std::unordered_map<long, long>*>(x->finger_to_rect);
    delete static_cast<std::vector<t_symbol*>*>(x->labels);
    
    x->draw_fingers = nullptr;
    x->rects = nullptr;
    x->finger_to_rect = nullptr;
    x->labels = nullptr;
    
    jbox_free(&x->box);
}


static void ll_trackpad_int(t_ll_trackpad *x, long n)
{
    n = (n > 0);

    if (x->enabled == n)
        return;

    if (!n) {
        if (x->dev) {
            mt_engine_close_device(x->dev);
            x->dev = NULL;
        }
        FINGER_TO_RECT(x).clear();
        for (auto& r : RECTS(x))
            r.active = false;
        
        clock_unset(x->poll_clock);
    }
    else {
        CFArrayRef devlist = MTDeviceCreateList();
        if (devlist && CFArrayGetCount(devlist) > 0) {
            x->dev = (MTDeviceRef)CFArrayGetValueAtIndex(devlist, 0);
            mt_engine_open_device(x->dev);
        }
        if (devlist)
            CFRelease(devlist);

        clock_delay(x->poll_clock, MT_POLL_INTERVAL_MS);
    }

    x->enabled = n;

    jbox_invalidate_layer((t_object *)x, NULL, gensym("fingerlayer"));
    jbox_redraw(&x->box);
}

static void ll_trackpad_poll(t_ll_trackpad* x)
{
    while (true) {
        FingerFrame* frame = mt_engine_pop_frame();
        if (!frame)
            break;

        DRAW_FINGERS(x).assign(frame->finger, frame->finger + frame->size);
        outlet_bang(x->out_bang);

        if ((x->mode == MODE_XY || x->mode == MODE_PADS) && x->rows > 0 && x->columns > 0) {
            // ------------------------------------------------
            // ARMING LOGIC
            // ------------------------------------------------

            // Phase 1: wait until NO fingers are touching
            if (x->waiting_for_clear) {
                bool any_touching = false;

                for (int i = 0; i < frame->size; ++i) {
                    if (frame->finger[i].size > 0.0001f) {
                        any_touching = true;
                        break;
                    }
                }

                if (!any_touching) {
                    // Surface is now clear
                    x->waiting_for_clear = 0;
                    x->waiting_for_first_touch = 1;

                    // Clean slate
                    FINGER_TO_RECT(x).clear();
                    for (auto& r : RECTS(x))
                        r.active = false;
                }

                // Ignore this frame
                mt_engine_release_frame(frame);
                continue;
            }

            // Phase 2: wait for first NEW touch
            if (x->waiting_for_first_touch) {
                bool new_touch = false;

                for (int i = 0; i < frame->size; ++i) {
                    if (frame->finger[i].size > 0.0001f) {
                        new_touch = true;
                        break;
                    }
                }

                if (new_touch) {
                    x->waiting_for_first_touch = 0;
                    // fall through → normal processing starts NOW
                } else {
                    mt_engine_release_frame(frame);
                    continue;
                }
            }
            
            // Track which finger ids exist in this frame (for "disappeared" cleanup)
            std::unordered_set<long> alive;
            alive.reserve((size_t)frame->size);

            for (int i = 0; i < frame->size; ++i) {
                Finger& f = frame->finger[i];
                
                long fid = f.identifier;

                alive.insert(fid);

                long rect = finger_to_rect_index(f, x->rows, x->columns);
                if (rect < 0 || rect >= (long)RECTS(x).size())
                    continue;

                // -----------------------------
                // RELEASE detection (reliable):
                // macOS sends size==0.0 when finger lifts
                // -----------------------------
                if (f.size <= 0.0001f) {
                    auto it = FINGER_TO_RECT(x).find(fid);
                    if (it != FINGER_TO_RECT(x).end()) {
                        long owned = it->second;
                        if (owned >= 0 && owned < (long)RECTS(x).size()) {
                            RectState& rs = RECTS(x)[owned];

                            if (x->mode == MODE_PADS && rs.active) {
                                t_atom out[2];
                                atom_setlong(out + 0, owned);
                                atom_setlong(out + 1, 0);
                                outlet_list(x->out_list, nullptr, 2, out);
                            }

                            rs.active = false;
                        }

                        FINGER_TO_RECT(x).erase(it);
                    }
                    continue; // don't process position on release frames
                }

                // -----------------------------
                // OWNERSHIP (claim) — only when size>0
                // Initialize smoothing ONCE when the finger claims a rect.
                // -----------------------------
                if (FINGER_TO_RECT(x).find(fid) == FINGER_TO_RECT(x).end()) {

                    // refuse if rect already taken by another finger
                    bool rect_taken = false;
                    for (auto& kv : FINGER_TO_RECT(x)) {
                        if (kv.second == rect) {
                            rect_taken = true;
                            break;
                        }
                    }

                    if (rect_taken)
                        continue;

                    // claim it
                    FINGER_TO_RECT(x)[fid] = rect;

                    // one-time init for this rect
                    RectState& rs_init = RECTS(x)[rect];

                    long col0 = rect % x->columns;
                    long row0 = rect / x->columns;

                    float cell_x0 = f.normalized.pos.x * x->columns - col0;
                    float cell_y0 = 1.0f - ((1.0f - f.normalized.pos.y) * x->rows - row0);

                    rs_init.touch_start_x = CLAMP(cell_x0, 0.f, 1.f);
                    rs_init.touch_start_y = CLAMP(cell_y0, 0.f, 1.f);

                    rs_init.start_x = rs_init.x;
                    rs_init.start_y = rs_init.y;

                    // click => absolute jump; no click => relative takeover
                    rs_init.absolute = (x->is_mouse_down != 0);

                    // consume the click so it only affects the first claimed rect
                    x->is_mouse_down = 0;
                }

                // Must have an owned rect by now
                auto it = FINGER_TO_RECT(x).find(fid);
                if (it == FINGER_TO_RECT(x).end())
                    continue;

                rect = it->second;
                RectState& rs = RECTS(x)[rect];
                
                // local normalized coords inside the cell
                long col = rect % x->columns;
                long row = rect / x->columns;

                float cell_x = f.normalized.pos.x * x->columns - col;
                float cell_y = 1.0f - ((1.0f - f.normalized.pos.y) * x->rows - row);

                // =============================
                // PADS MODE
                // =============================
                if (x->mode == MODE_PADS) {
                    if (!rs.active) {
                        rs.active = true;

                        // velocity from pressure
                        float velocity = CLAMP(f.size * 127.f, 1.f, 127.f);
                        rs.velocity = velocity;

                        t_atom out[2];
                        atom_setlong(out + 0, rect);
                        atom_setlong(out + 1, (long)velocity);

                        outlet_list(x->out_list, nullptr, 2, out);
                    }

                    continue;
                }

                // =============================
                // XY MODE
                // =============================
                if (rs.absolute) {
                    // absolute jump
                    rs.x = CLAMP(cell_x, 0.f, 1.f);
                    rs.y = CLAMP(cell_y, 0.f, 1.f);
                } else {
                    // relative movement
                    float dx = cell_x - rs.touch_start_x;
                    float dy = cell_y - rs.touch_start_y;

                    rs.x = CLAMP(rs.start_x + dx, 0.f, 1.f);
                    rs.y = CLAMP(rs.start_y + dy, 0.f, 1.f);
                }
                rs.pressure = f.size;

                // ---- output ----
                t_atom out[3];
                atom_setlong(out + 0, rect);
                atom_setfloat(out + 1, rs.x);
                atom_setfloat(out + 2, rs.y);
                outlet_list(x->out_list, nullptr, 3, out);
            }

            // -----------------------------
            // Cleanup: if a finger disappears without a size==0 frame,
            // release its owned rect.
            // -----------------------------
            for (auto it = FINGER_TO_RECT(x).begin(); it != FINGER_TO_RECT(x).end(); ) {
                if (alive.find(it->first) == alive.end()) {
                    long owned = it->second;
                    if (owned >= 0 && owned < (long)RECTS(x).size()) {
                        RectState& rs = RECTS(x)[owned];

                        if (x->mode == MODE_PADS && rs.active) {
                            t_atom out[2];
                            atom_setlong(out + 0, owned);
                            atom_setlong(out + 1, 0);
                            outlet_list(x->out_list, nullptr, 2, out);
                        }

                        rs.active = false;
                    }

                    it = FINGER_TO_RECT(x).erase(it);
                } else {
                    ++it;
                }
            }
            
        } else {
            // =============================
            // RAW MODE
            // =============================
            t_atom list[12];

            for (int i = 0; i < frame->size; ++i) {
                Finger& f = frame->finger[i];

                atom_setfloat(list + 0,  (float)i);
                atom_setfloat(list + 1,  (float)f.frame);
                atom_setfloat(list + 2,  f.angle);
                atom_setfloat(list + 3,  f.majorAxis);
                atom_setfloat(list + 4,  f.minorAxis);
                atom_setfloat(list + 5,  f.normalized.pos.x);
                atom_setfloat(list + 6,  f.normalized.pos.y);
                atom_setfloat(list + 7,  f.normalized.vel.x);
                atom_setfloat(list + 8,  f.normalized.vel.y);
                atom_setfloat(list + 9,  (float)f.identifier);
                atom_setfloat(list + 10, (float)f.state);
                atom_setfloat(list + 11, f.size);

                outlet_list(x->out_list, nullptr, 12, list);
            }
        }
        mt_engine_release_frame(frame);
    }
    
    if (x->enabled)
        clock_delay(x->poll_clock, MT_POLL_INTERVAL_MS);
    
    jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));
    jbox_redraw(&x->box);
}

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

static void ll_trackpad_paint(t_ll_trackpad *x, t_object *view)
{
    t_rect r;
    jbox_get_rect_for_view((t_object *)&x->box, view, &r);

    t_jgraphics *g =
        jbox_start_layer((t_object *)x, view,
                         gensym("fingerlayer"),
                         r.width, r.height);

    if (g) {
        // background
        jgraphics_set_source_jrgba(g, &x->bgcolor);
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
        
        if ((x->mode == MODE_XY || x->mode == MODE_PADS) && x->rows > 0 && x->columns > 0) {
            double cell_w = r.width  / (double)x->columns;
            double cell_h = r.height / (double)x->rows;

            for (long i = 0; i < (long)RECTS(x).size(); ++i) {
                long col = i % x->columns;
                long row = i / x->columns;

                double cx = col * cell_w;
                double cy = row * cell_h;
                
                RectState& rs = RECTS(x)[i];

                if (x->mode == MODE_PADS) {
                    if (rs.active) {

                        // velocity is 1..127 → map to visible alpha
                        double alpha = CLAMP(rs.velocity / 127.0, 0.25, 1.0);

                        jgraphics_set_source_rgba(g, 1.0, 0.6, 0.2, alpha);
                        jgraphics_rectangle(g,
                                            cx,
                                            cy,
                                            cell_w,
                                            cell_h);
                        jgraphics_fill(g);
                    }
                    continue;
                }
                
                
                // compute intersection px
                double ix = cx + rs.x * cell_w;
                double iy = cy + (1.0 - rs.y) * cell_h;

                // optional fill regions first (so lines/dot draw on top)å
                if (rs.active) {
                    if (x->showxline == 2) { // fill (x axis => fill left->ix)
                        jgraphics_set_source_jrgba(g, &x->linecolor);
                        jgraphics_rectangle(g, cx, cy, (ix - cx), cell_h);
                        jgraphics_fill(g);
                    }
                    if (x->showyline == 2) { // fill (y axis => fill bottom->iy)
                        jgraphics_set_source_jrgba(g, &x->linecolor);
                        jgraphics_rectangle(g, cx, iy, cell_w, (cy + cell_h - iy));
                        jgraphics_fill(g);
                    }
                }

                // fader lines
                jgraphics_set_source_jrgba(g, &x->linecolor);
                jgraphics_set_line_width(g, 2.0);

                // vertical
                if (x->showyline == 1) { // vertical line
                    
                    jgraphics_move_to(g,
                                      cx + rs.x * cell_w,
                                      cy);
                    jgraphics_line_to(g,
                                      cx + rs.x * cell_w,
                                      cy + cell_h);
                    jgraphics_stroke(g);

                }

                // horizontal
                if (x->showxline == 1) { // horizontal line
                    
                    jgraphics_move_to(g,
                                      cx,
                                      cy + (1.0 - rs.y) * cell_h);
                    jgraphics_line_to(g,
                                      cx + cell_w,
                                      cy + (1.0 - rs.y) * cell_h);
                    
                    jgraphics_stroke(g);
                }
                
                // dot at intersection
                if (x->dotsize > 0.0) {
                    jgraphics_set_source_jrgba(g, &x->dotcolor);
                    jgraphics_arc(g, ix, iy, x->dotsize, 0, JGRAPHICS_2PI);
                    jgraphics_fill(g);
                }

                if (x->showlabels) {
                    const char* txt = nullptr;

                    auto& labs = LABELS(x);
                    if (!labs.empty() && i < (long)labs.size()) {
                        txt = labs[i]->s_name;
                    } else {
                        // default numeric
                        static char buf[16];
                        snprintf(buf, 16, "%ld", i);
                        txt = buf;
                    }

                    jgraphics_set_source_jrgba(g, &x->labelcolor);
                    jgraphics_move_to(g, cx + 6, cy + cell_h - 6);
                    jgraphics_show_text(g, txt);
                }

            }
        } else {
            
            // draw fingers
            for (auto &f : DRAW_FINGERS(x)) {
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
        
        // focus border
        if (x->enabled) {
            const double border = 5.0;
            const double inset = border * 0.5;

            jgraphics_set_source_rgba(g,
                x->bordercolor.red, x->bordercolor.green, x->bordercolor.blue, x->bordercolor.alpha);
            jgraphics_set_line_width(g, border);
            jgraphics_rectangle(g, inset, inset, r.width - border, r.height - border);
            jgraphics_stroke(g);
        }

        jbox_end_layer((t_object *)x, view, gensym("fingerlayer"));
    }

    jbox_paint_layer((t_object *)x, view,
                     gensym("fingerlayer"), 0, 0);
}

static void ll_trackpad_set_cursor(t_ll_trackpad *x, t_object *patcherview, char active)
{
    if (!patcherview)
        return;

    if (active)
        jmouse_setcursor(patcherview, (t_object *)x, JMOUSE_CURSOR_NONE);
    else
        jmouse_setcursor(patcherview, (t_object *)x, JMOUSE_CURSOR_ARROW);
}

static void ll_trackpad_mousemove(t_ll_trackpad *x,
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


static void ll_trackpad_mousedown(t_ll_trackpad *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                 long modifiers)
{
    if (!x->enabled)
        x->is_mouse_down = 1;
}

static void ll_trackpad_mouseup(t_ll_trackpad *x,
                                 t_object *patcherview,
                                 t_pt pt,
                                 long modifiers)
{
    if (!x->enabled && x->is_mouse_down) {
        jbox_grabfocus(&x->box);
        ll_trackpad_int(x, 1);
        ll_trackpad_set_cursor(x, patcherview, 1); // hide cursor
        
        x->waiting_for_clear = 1;
        x->waiting_for_first_touch = 0;
    }
    x->is_mouse_down = 0;
}


static void ll_trackpad_key(t_ll_trackpad *x,
                           t_object *patcherview,
                           long keycode,
                           long modifiers,
                           long textcharacter)
{
    // ESC, space, delete by keycode or textcharacter depending on what you want:
    if (textcharacter == 27 || textcharacter == 32 || textcharacter == 127) {
        ll_trackpad_int(x, 0);
        ll_trackpad_set_cursor(x, patcherview, 0); // show cursor
    }
}

static void ll_trackpad_focusgained(t_ll_trackpad *x, t_object *pv) {
//    post("ll_trackpad: focus gained");
}

static void ll_trackpad_focuslost(t_ll_trackpad *x, t_object *pv) {
//    post("ll_trackpad: focus lost");
    ll_trackpad_set_cursor(x, pv, 0);
}

t_max_err ll_trackpad_set_rows(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv)
{
    if (argc && argv) {
        long v = atom_getlong(argv);
        v = CLAMP(v, 0, 1024);

        if (x->rows != v) {
            x->rows = v;
            ll_trackpad_resize_rects(x);   // <-- critical
            jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));
            jbox_redraw(&x->box);
        }
    }
    return MAX_ERR_NONE;
}

t_max_err ll_trackpad_set_columns(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv)
{
    if (argc && argv) {
        long v = atom_getlong(argv);
        v = CLAMP(v, 0, 1024);

        if (x->columns != v) {
            x->columns = v;
            ll_trackpad_resize_rects(x);
            jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));
            jbox_redraw(&x->box);
        }
    }
    return MAX_ERR_NONE;
}

t_max_err ll_trackpad_set_mode(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv)
{
    if (argc && argv) {
        long v = atom_getlong(argv);

        if (x->mode != v) {
            x->mode = v;
            jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));
            jbox_redraw(&x->box);
        }
    }
    return MAX_ERR_NONE;
}

t_max_err ll_trackpad_set_labels(t_ll_trackpad* x, t_object* attr, long argc, t_atom* argv)
{
    auto& v = LABELS(x);
    v.clear();

    for (long i = 0; i < argc; ++i) {
        if (atom_gettype(argv + i) == A_SYM) {
            v.push_back(atom_getsym(argv + i));
        } else if (atom_gettype(argv + i) == A_LONG) {
            // allow numeric labels: 1 2 3
            char buf[32];
            snprintf(buf, 32, "%ld", atom_getlong(argv + i));
            v.push_back(gensym(buf));
        } else if (atom_gettype(argv + i) == A_FLOAT) {
            char buf[32];
            snprintf(buf, 32, "%.3g", atom_getfloat(argv + i));
            v.push_back(gensym(buf));
        }
    }

    jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));
    jbox_redraw(&x->box);
    return MAX_ERR_NONE;
}

t_max_err ll_trackpad_notify(
    t_ll_trackpad *x,
    t_symbol *s,
    t_symbol *msg,
    void *sender,
    void *data
){
    jbox_invalidate_layer((t_object*)x, nullptr, gensym("fingerlayer"));

    return jbox_notify((t_jbox *)x, s, msg, sender, data);
}
