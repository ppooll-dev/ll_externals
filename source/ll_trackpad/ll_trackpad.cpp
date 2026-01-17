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
#include <CoreFoundation/CoreFoundation.h>

#define MT_POLL_INTERVAL_MS 5

using namespace std;

typedef struct _ll_trackpad
{
    t_jbox box;

	void *out_list;			// outlet for finger data
	void *out_bang;			// bang out for start of frame
    
    void *poll_clock;
    MTDeviceRef dev;	    // reference to the ll_trackpad device
    
    long enabled;
    char draw_circles;
    
    // UI-only state
    std::vector<Finger> draw_fingers;
	
    long rows;     // number of rows to draw (0 = none)
    long columns;  // number of columns to draw (0 = none)

} t_ll_trackpad;


static t_class *s_ll_trackpad_class = NULL;	

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

static void ll_trackpad_key(t_ll_trackpad *x,
                           t_object *patcherview,
                           long keycode,
                           long modifiers,
                            long textcharacter);

static void ll_trackpad_focusgained(t_ll_trackpad *x, t_object *pv);
static void ll_trackpad_focuslost(t_ll_trackpad *x, t_object *pv);

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

    jbox_initclass(c, JBOX_DRAWFIRSTIN);

    class_addmethod(c, (method)ll_trackpad_int, "int", A_LONG, 0);
    class_addmethod(c, (method)ll_trackpad_paint, "paint", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_mousemove, "mousemove", A_CANT, 0);

    class_addmethod(c, (method)ll_trackpad_key, "key", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_focusgained, "focusgained", A_CANT, 0);
    class_addmethod(c, (method)ll_trackpad_focuslost,   "focuslost",   A_CANT, 0);


    CLASS_ATTR_DEFAULT(c,"patching_rect",0, "0. 0. 200. 100.");
    
    CLASS_ATTR_LONG(c, "rows", 0, t_ll_trackpad, rows);
    CLASS_ATTR_FILTER_CLIP(c, "rows", 0, 1024);
    CLASS_ATTR_DEFAULT(c, "rows", 0, "0");
    CLASS_ATTR_SAVE(c, "rows", 0);

    CLASS_ATTR_LONG(c, "columns", 0, t_ll_trackpad, columns);
    CLASS_ATTR_FILTER_CLIP(c, "columns", 0, 1024);
    CLASS_ATTR_DEFAULT(c, "columns", 0, "0");
    CLASS_ATTR_SAVE(c, "columns", 0);
    
    CLASS_ATTR_CHAR(c,              "drawcircles", 0, t_ll_trackpad, draw_circles);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c,"drawcircles", 0, "0");
    CLASS_ATTR_STYLE_LABEL(c,       "drawcircles", 0, "onoff", "Draw circles where fingers pressed");

    class_register(CLASS_BOX, c);
    s_ll_trackpad_class = c;
    
    return 1;
}

void *ll_trackpad_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ll_trackpad *x = (t_ll_trackpad *)object_alloc(s_ll_trackpad_class);
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
    
    x->poll_clock = clock_new(x, (method)ll_trackpad_poll);
    
    jbox_ready(&x->box);
    return x;
}

static void ll_trackpad_free(t_ll_trackpad *x)
{
    ll_trackpad_int(x, 0);
    clock_unset(x->poll_clock);
    object_free(x->poll_clock);
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

        x->draw_fingers.assign(frame->finger,
                               frame->finger + frame->size);

        outlet_bang(x->out_bang);

        t_atom list[14];

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
            atom_setfloat(list + 12, 0);
            atom_setfloat(list + 13, 0);

            outlet_list(x->out_list, nullptr, 14, list);
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
    if (!x->enabled) {
        jbox_grabfocus(&x->box);
        ll_trackpad_int(x, 1);
        ll_trackpad_set_cursor(x, patcherview, 1); // hide cursor
    }
    else {
        ll_trackpad_int(x, 0);
        ll_trackpad_set_cursor(x, patcherview, 0); // show cursor
    }
}


static void ll_trackpad_key(t_ll_trackpad *x,
                           t_object *patcherview,
                           long keycode,
                           long modifiers,
                           long textcharacter)
{
    // ESC by keycode or textcharacter depending on what you want:
    if (textcharacter == 27) {
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
