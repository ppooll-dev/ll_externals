/*
    ll.preset version 1.7.0
    for ppooll

    Preset grid for pattrstorage: a C port of ll.pattr_v8ui, so that preset
    recall, interpolation and ramps run in the scheduler (high-priority) thread.

    based on:
    pattr_ui: A preset-like user interface for pattrstorage.
    (c) 2005 by Charlie DeTar
    Licensed under the GNU Public License (GPL). There is no warranty whatsoever.

    In its patcher ll.preset uses the objects named
      pat            [pattrstorage] (required)
      preset-ramp    ramp time in ms (anything with a value)
      prepare_dump   banged before every store (plugin@ refreshes its state)
      plugin_state, param_select   inactive during ramps / scrubs
      pattrmarker    its name becomes the act name
    and in a ppooll act the "act" bpatcher's actui (active_set notices) and
    pres_menu (the right-click menu lists the act's preset files).

    Inlet
      int N               recall slot N (ramps if preset-ramp > 0)
      float F             interpolate between stored slots (1-based, by order)
      list P N A          recall P N A (interpolation, no ramp)
      next, prev          next / previous stored slot (ramps; @wrap 1 wraps)
      store N             store slot N
      copy SRC DST        copy a stored slot
      bang                redraw and output the current slot
      slotlist ...        stored slots (pattrstorage getslotlist)
      text ..., set_TEXT_data <dict>, fontsize F   slot labels
      actname <sym>, refresh, clear, read, q_size N, scrubRate F, about
      write, recall, ramp, splitarg   accepted and ignored (js compatibility)

    From the act (ll.act_main.js / act.maxpat)
      changed <client> ...      a parameter moved; ll.preset's own recalls,
                                ramps and stores don't count
      clean [file]              preset file loaded / cleared (clean) or written
                                (clean file): clears the change marks
      clients <name> <0|1> ...  parameters and whether each is included in the
                                presets; only included ones count as changes
      activestore <0|1>         active_store: per-slot parameter sets
      ignore <names...>         never count as changes (default: act ll.blues
                                presets preset-ramp; "x" also covers "x::...")

    Outlet
      store N, delete N, copy A B, getslotlist (after read), current slot (bang)

    Threading
      - Inlet messages and setvalueof run in the thread they arrive in; ramps run
        on a Max clock. Painting, mouse and menu are main-thread.
      - Stored slots and the current value are shared between threads and
        guarded by a critical region (never held while calling out).
      - Notices to actui go through a qelem (low priority, cancelled on free).
        With active_store on they must come BEFORE the recall: main-thread
        recalls call actui directly, scheduler recalls are handed to the main
        thread. Ramps keep the parameter set of the last full recall.
      - A recall nested more than 8 deep in ll.preset's own recall is a loop and
        is stopped with an error.

    Drawing
      red / green         current / stored slot; interpolation fills two slots
      dot (top right)     current slot's parameters moved since recall / store
      corner (bottom right)  slot changed since the preset file was loaded/written

    Mouse
      click: store an empty slot, recall a stored one    shift-click: store again
      cmd-click (ctrl on Windows): delete                drag: recall slots passed
      option-drag: scrub through the stored slots (endless drag)
      right-click (ctrl-click on Mac): slot menu, parameters, the act's presets

    Attributes: boxsize, wrap, vbrgb, vfrgb, vrgb2, vrgb3
*/

#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"
#include "ext_dictobj.h"
#include "ext_critical.h"
#include "ext_dictionary.h"
#include "ext_systhread.h"
#include "jpatcher_api.h"
#include "jgraphics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define LL_PRESET_VERSION "1.7.0"
#define LL_PRESET_MAX_SLOTS 4096
#define LL_PRESET_RAMP_INTERVAL 20.0 // ms, ~50 fps
#define LL_PRESET_RAND_CELLS 9       // NxN sub-cells for the interpolation fill
#define LL_PRESET_NOTICE_QUEUE 32    // pending actui notices
#define LL_PRESET_MENU_FONT "Arial"
#define LL_PRESET_MENU_FONTSIZE 11
#define LL_PRESET_MAX_IGNORE 32

// Platform-dependent modifiers (as in ll.number)
#ifdef MAC_VERSION
#define LL_PRESET_KEY_DELETE eCommandKey
#define LL_PRESET_MENU_MOD (eRightButton | ePopupMenu) // right-click or ctrl-click
#else
#define LL_PRESET_KEY_DELETE (eCommandKey | eControlKey)
#define LL_PRESET_MENU_MOD eRightButton
#endif

#define LL_LOCK(x) critical_enter((x)->lock)
#define LL_UNLOCK(x) critical_exit((x)->lock)

enum {
    MENU_NONE = 0,
    MENU_RECALL,
    MENU_RECALL_NORAMP,
    MENU_STORE,
    MENU_COPY,
    MENU_PASTE,
    MENU_DELETE,
    MENU_NEXT,
    MENU_PREV,
    MENU_PRES = 1000,  // + item index in the act's pres_menu
    MENU_PARAM = 5000  // + index in the clients list
};

typedef struct _ll_preset_notice {
    t_symbol *what; // recall / store
    long slot;
} t_ll_preset_notice;

typedef struct _ll_preset {
    t_jbox j_box;
    void *outlet;

    // attributes
    long boxsize;
    char wrap; // next / prev wrap around
    t_jrgba vbrgb; // background (and text / icon colour)
    t_jrgba vfrgb; // empty square
    t_jrgba vrgb2; // current slot / interpolation fill
    t_jrgba vrgb3; // stored slot

    // geometry cache
    double g_square, g_margin, g_inner, g_step, g_startX, g_startY;
    long num_cols, num_rows;
    double last_w, last_h;
    char geom_dirty;
    char ready;

    // shared between threads, guarded by lock
    t_critical lock;
    char slots[LL_PRESET_MAX_SLOTS + 1]; // stored slots (1-based)
    char val_interp;                     // value: discrete → val_slot, interpolating → [val_a, val_b]
    double val_slot;
    double val_a, val_b;
    t_ll_preset_notice notices[LL_PRESET_NOTICE_QUEUE];
    long notice_head, notice_count;

    // hover
    long hover_index;
    char hover_add, hover_remove, hover_replace;

    // mouse
    long click;
    double drag_start;
    char option_down;
    char option_scrub_active;
    char drag_delta;    // endless-drag scrub (mousedragdelta) active
    double scrub_accum;
    double last_drag_x;
    char have_last_drag_x;
    double scrub_rate;
    long copy_slot; // for the menu's copy / paste

    // ramp
    void *ramp_clock;
    char ramp_running;
    long ramp_prev, ramp_next;
    double ramp_time, ramp_start;

    // low-priority work, cancelled on free
    void *init_qelem;
    void *actui_qelem;
    void *resize_qelem;

    // text overlay
    t_critical text_lock;
    t_symbol **text;
    long text_count;
    double fontsize;

    t_symbol *act_name;

    // cached objects in our patcher
    t_object *patcher;
    t_object *pat;          // [pattrstorage] named "pat"
    t_object *preset_ramp;  // named "preset-ramp"
    t_object *prepare_dump; // named "prepare_dump"
    char has_plugin_state, has_param_select;
    int discrete_active; // -1 unknown, 0, 1

    char menu_open; // keep the hover highlight while the right-click menu is up

    // change tracking
    long own_change;                             // > 0 while ll.preset itself is recalling / storing
    char cur_dirty;                              // current slot's parameters moved since recall/store
    char file_dirty;                             // slots changed since the preset file was loaded/written
    char slot_changed[LL_PRESET_MAX_SLOTS + 1];  // which ones
    t_symbol *ignore[LL_PRESET_MAX_IGNORE];
    long ignore_count;

    // the act's parameters (pattrstorage clients) and their active flags; guarded by lock
    t_symbol **client_names;
    char *client_active;
    long client_count;
    char have_clients;

    // active_store: per-slot parameter sets, applied by actui before a recall
    char active_store;
    void *recall_qelem; // scheduler-thread recalls handed to the main thread
    long deferred_slot;
} t_ll_preset;

static t_class *s_ll_preset_class = NULL;

static t_symbol *llp_sym_pat, *llp_sym_preset_ramp, *llp_sym_prepare_dump, *llp_sym_plugin_state,
    *llp_sym_param_select, *llp_sym_pattrmarker, *llp_sym_act, *llp_sym_actui, *llp_sym_recall,
    *llp_sym_store, *llp_sym_delete, *llp_sym_copy, *llp_sym_getslotlist, *llp_sym_active,
    *llp_sym_active_set, *llp_sym_name,
    *llp_sym_text, *llp_sym_fontsize, *llp_sym_subpatcher, *llp_sym_bringtofront, *llp_sym_v8,
    *llp_sym_change_TEXT, *llp_sym_refresh, *llp_sym_no, *llp_sym_empty,
    *llp_sym_pres_menu, *llp_sym_items, *llp_sym_comma,
    *llp_sym_int, *llp_sym_boxsize, *llp_sym_file;

#define LLP_MAX_RECALL_DEPTH 8 // deeper nested recalls are a loop: stopped

// ------------------------------------------------------------------------
// prototypes
// ------------------------------------------------------------------------

void *ll_preset_new(t_symbol *s, long argc, t_atom *argv);
void ll_preset_free(t_ll_preset *x);
t_max_err ll_preset_notify(t_ll_preset *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void ll_preset_assist(t_ll_preset *x, void *b, long m, long a, char *s);
void ll_preset_paint(t_ll_preset *x, t_object *view);
void ll_preset_mousedown(t_ll_preset *x, t_object *view, t_pt pt, long mod);
void ll_preset_mousedrag(t_ll_preset *x, t_object *view, t_pt pt, long mod);
void ll_preset_mousedragdelta(t_ll_preset *x, t_object *view, t_pt pt, long mod);
void ll_preset_mouseup(t_ll_preset *x, t_object *view, t_pt pt, long mod);
void ll_preset_mousemove(t_ll_preset *x, t_object *view, t_pt pt, long mod);
void ll_preset_mouseleave(t_ll_preset *x, t_object *view, t_pt pt, long mod);
t_max_err ll_preset_getvalueof(t_ll_preset *x, long *ac, t_atom **av);
t_max_err ll_preset_setvalueof(t_ll_preset *x, long ac, t_atom *av);
t_max_err ll_preset_boxsize_set(t_ll_preset *x, void *attr, long ac, t_atom *av);

void ll_preset_loadbang(t_ll_preset *x);
void ll_preset_int(t_ll_preset *x, long n);
void ll_preset_float(t_ll_preset *x, double f);
void ll_preset_list(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_anything(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_bang(t_ll_preset *x);
void ll_preset_next(t_ll_preset *x);
void ll_preset_prev(t_ll_preset *x);
void ll_preset_copy(t_ll_preset *x, long src, long dst);
void ll_preset_store_msg(t_ll_preset *x, long slot);
void ll_preset_slotlist(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_clear(t_ll_preset *x);
void ll_preset_read(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_noop(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_fontsize(t_ll_preset *x, double v);
void ll_preset_text(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_set_TEXT_data(t_ll_preset *x, t_symbol *dictname);
void ll_preset_actname(t_ll_preset *x, t_symbol *s);
void ll_preset_q_size(t_ll_preset *x, long v);
void ll_preset_scrubrate(t_ll_preset *x, double v);
void ll_preset_refresh(t_ll_preset *x);
void ll_preset_about(t_ll_preset *x);
void ll_preset_changed(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_clean(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_ignore(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_clients(t_ll_preset *x, t_symbol *s, long ac, t_atom *av);
void ll_preset_activestore(t_ll_preset *x, long on);
static void ll_preset_recall_qfn(t_ll_preset *x);
static char ll_preset_client_inactive(t_ll_preset *x, t_symbol *client);

static void ll_preset_ramp_tick(t_ll_preset *x);
static void ll_preset_init_qfn(t_ll_preset *x);
static void ll_preset_actui_qfn(t_ll_preset *x);
static void ll_preset_resize_qfn(t_ll_preset *x);

// ------------------------------------------------------------------------
// class setup
// ------------------------------------------------------------------------

void ext_main(void *r)
{
    t_class *c;

    common_symbols_init(); // _sym_free, _sym_modified, _sym_bang live in a table set up here

    c = class_new("ll.preset", (method)ll_preset_new, (method)ll_preset_free,
                  sizeof(t_ll_preset), (method)NULL, A_GIMME, 0);

    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;
    jbox_initclass(c, 0);

    class_addmethod(c, (method)ll_preset_paint, "paint", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mousedrag, "mousedrag", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mousedragdelta, "mousedragdelta", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mouseup, "mouseup", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mousemove, "mousemove", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_mouseleave, "mouseleave", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_notify, "notify", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_getvalueof, "getvalueof", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_setvalueof, "setvalueof", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_loadbang, "loadbang", A_CANT, 0);
    class_addmethod(c, (method)ll_preset_assist, "assist", A_CANT, 0);

    class_addmethod(c, (method)ll_preset_int, "int", A_LONG, 0);
    class_addmethod(c, (method)ll_preset_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)ll_preset_list, "list", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_anything, "anything", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_bang, "bang", 0);
    class_addmethod(c, (method)ll_preset_next, "next", 0);
    class_addmethod(c, (method)ll_preset_prev, "prev", 0);
    class_addmethod(c, (method)ll_preset_copy, "copy", A_LONG, A_LONG, 0);
    class_addmethod(c, (method)ll_preset_store_msg, "store", A_LONG, 0);
    class_addmethod(c, (method)ll_preset_slotlist, "slotlist", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_clear, "clear", 0);
    class_addmethod(c, (method)ll_preset_read, "read", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_noop, "write", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_noop, "recall", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_noop, "ramp", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_noop, "splitarg", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_fontsize, "fontsize", A_FLOAT, 0);
    class_addmethod(c, (method)ll_preset_text, "text", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_set_TEXT_data, "set_TEXT_data", A_SYM, 0);
    class_addmethod(c, (method)ll_preset_actname, "actname", A_SYM, 0);
    class_addmethod(c, (method)ll_preset_q_size, "q_size", A_LONG, 0);
    class_addmethod(c, (method)ll_preset_scrubrate, "scrubRate", A_FLOAT, 0);
    class_addmethod(c, (method)ll_preset_refresh, "refresh", 0);
    class_addmethod(c, (method)ll_preset_about, "about", 0);
    class_addmethod(c, (method)ll_preset_changed, "changed", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_clean, "clean", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_ignore, "ignore", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_clients, "clients", A_GIMME, 0);
    class_addmethod(c, (method)ll_preset_activestore, "activestore", A_LONG, 0);

    CLASS_ATTR_DEFAULT(c, "patching_rect", 0, "0. 0. 100. 40.");

    //########### ll_preset
    CLASS_STICKY_ATTR(c, "category", 0, "llpreset");

    CLASS_ATTR_LONG(c, "boxsize", 0, t_ll_preset, boxsize);
    CLASS_ATTR_ACCESSORS(c, "boxsize", NULL, ll_preset_boxsize_set);
    CLASS_ATTR_LABEL(c, "boxsize", 0, "Square Size");
    CLASS_ATTR_FILTER_MIN(c, "boxsize", 2);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "boxsize", 0, "12");

    CLASS_ATTR_CHAR(c, "wrap", 0, t_ll_preset, wrap);
    CLASS_ATTR_STYLE_LABEL(c, "wrap", 0, "onoff", "Wrap next / prev");
    CLASS_ATTR_DEFAULT_SAVE(c, "wrap", 0, "0");

    CLASS_STICKY_ATTR_CLEAR(c, "category");

    //########## colors
    CLASS_STICKY_ATTR(c, "category", 0, "Color");

    CLASS_ATTR_RGBA(c, "vbrgb", 0, t_ll_preset, vbrgb);
    CLASS_ATTR_STYLE_LABEL(c, "vbrgb", 0, "rgba", "Background");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "vbrgb", 0, "0.1 0.1 0.1 1.");

    CLASS_ATTR_RGBA(c, "vfrgb", 0, t_ll_preset, vfrgb);
    CLASS_ATTR_STYLE_LABEL(c, "vfrgb", 0, "rgba", "Square Fill");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "vfrgb", 0, "0.9 0.9 0.9 1.");

    CLASS_ATTR_RGBA(c, "vrgb2", 0, t_ll_preset, vrgb2);
    CLASS_ATTR_STYLE_LABEL(c, "vrgb2", 0, "rgba", "Current Slot");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "vrgb2", 0, "1. 0. 0. 1.");

    CLASS_ATTR_RGBA(c, "vrgb3", 0, t_ll_preset, vrgb3);
    CLASS_ATTR_STYLE_LABEL(c, "vrgb3", 0, "rgba", "Stored Slot");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "vrgb3", 0, "0. 1. 0. 1.");

    CLASS_STICKY_ATTR_CLEAR(c, "category");

    CLASS_ATTR_ORDER(c, "vbrgb", 0, "1");
    CLASS_ATTR_ORDER(c, "vfrgb", 0, "2");
    CLASS_ATTR_ORDER(c, "vrgb3", 0, "3");
    CLASS_ATTR_ORDER(c, "vrgb2", 0, "4");

    class_register(CLASS_BOX, c);
    class_alias(c, gensym("ll_preset"));
    s_ll_preset_class = c;

    // symbols are cached here so nothing calls gensym() in high priority
    llp_sym_pat = gensym("pat");
    llp_sym_preset_ramp = gensym("preset-ramp");
    llp_sym_prepare_dump = gensym("prepare_dump");
    llp_sym_plugin_state = gensym("plugin_state");
    llp_sym_param_select = gensym("param_select");
    llp_sym_pattrmarker = gensym("pattrmarker");
    llp_sym_act = gensym("act");
    llp_sym_actui = gensym("actui");
    llp_sym_recall = gensym("recall");
    llp_sym_store = gensym("store");
    llp_sym_delete = gensym("delete");
    llp_sym_copy = gensym("copy");
    llp_sym_getslotlist = gensym("getslotlist");
    llp_sym_active = gensym("active");
    llp_sym_active_set = gensym("active_set");
    llp_sym_name = gensym("name");
    llp_sym_text = gensym("text");
    llp_sym_fontsize = gensym("fontsize");
    llp_sym_subpatcher = gensym("subpatcher");
    llp_sym_bringtofront = gensym("bringtofront");
    llp_sym_v8 = gensym("v8");
    llp_sym_change_TEXT = gensym("change_TEXT");
    llp_sym_refresh = gensym("refresh");
    llp_sym_no = gensym("no");
    llp_sym_empty = gensym("");
    llp_sym_pres_menu = gensym("pres_menu");
    llp_sym_items = gensym("items");
    llp_sym_comma = gensym(",");
    llp_sym_int = gensym("int");
    llp_sym_boxsize = gensym("boxsize");
    llp_sym_file = gensym("file");
}

// ------------------------------------------------------------------------
// geometry
// ------------------------------------------------------------------------

static void ll_preset_recompute(t_ll_preset *x, double w, double h)
{
    double sq, step;
    long cols = 0, rows = 0;

    x->g_square = x->boxsize / 2.0;
    x->g_step = x->g_square * 2.0 + x->g_inner;
    x->g_startX = x->g_margin + x->g_square;
    x->g_startY = x->g_margin + x->g_square;

    sq = x->g_square;
    step = x->g_step;

    if (step > 0) {
        // half-visible rule (same as the js)
        double cx, cy;
        for (cx = x->g_startX;; cx += step) {
            double vis = MIN(cx + sq, w - x->g_margin) - MAX(cx - sq, x->g_margin);
            if (vis < sq / 2.0) break;
            cols++;
        }
        for (cy = x->g_startY;; cy += step) {
            double vis = MIN(cy + sq, h - x->g_margin) - MAX(cy - sq, x->g_margin);
            if (vis < sq / 2.0) break;
            rows++;
        }
    }

    x->num_cols = cols;
    x->num_rows = rows;
    x->last_w = w;
    x->last_h = h;
    x->geom_dirty = 0;
}

static long ll_preset_maxslot(t_ll_preset *x)
{
    long m = x->num_cols * x->num_rows;
    return m > LL_PRESET_MAX_SLOTS ? LL_PRESET_MAX_SLOTS : m;
}

static long ll_preset_coord_to_square(t_ll_preset *x, double px, double py)
{
    long row, col, index = 1;
    double sq = x->g_square, step = x->g_step;

    for (row = 0; row < x->num_rows; row++) {
        double cy = x->g_startY + row * step;
        for (col = 0; col < x->num_cols; col++) {
            double cx = x->g_startX + col * step;
            if (px >= cx - sq && px <= cx + sq && py >= cy - sq && py <= cy + sq)
                return index;
            index++;
        }
    }
    return 0;
}

// ------------------------------------------------------------------------
// stored-slot helpers (the multi-slot ones expect the caller to hold the lock)
// ------------------------------------------------------------------------

static char ll_preset_is_stored(t_ll_preset *x, long slot)
{
    return (slot >= 1 && slot <= LL_PRESET_MAX_SLOTS) ? x->slots[slot] : 0;
}

static long ll_preset_stored_count(t_ll_preset *x)
{
    long i, n = 0, max = ll_preset_maxslot(x);
    for (i = 1; i <= max; i++)
        if (x->slots[i]) n++;
    return n;
}

// k-th stored slot (0-based); 0 if none
static long ll_preset_stored_at(t_ll_preset *x, long k)
{
    long i, n = 0, max = ll_preset_maxslot(x);
    for (i = 1; i <= max; i++) {
        if (x->slots[i]) {
            if (n == k) return i;
            n++;
        }
    }
    return 0;
}

// index of slot among stored slots, -1 if not stored
static long ll_preset_stored_indexof(t_ll_preset *x, long slot)
{
    long i, n = 0, max = ll_preset_maxslot(x);
    for (i = 1; i <= max; i++) {
        if (x->slots[i]) {
            if (i == slot) return n;
            n++;
        }
    }
    return -1;
}

// nearest stored index to value; -1 if nothing stored
static long ll_preset_find_stored_index(t_ll_preset *x, double v)
{
    long i, n = 0, best = -1, max = ll_preset_maxslot(x);
    double bestd = 1e300;
    for (i = 1; i <= max; i++) {
        if (x->slots[i]) {
            double d = fabs(i - v);
            if (d < bestd) {
                bestd = d;
                best = n;
            }
            n++;
        }
    }
    return best;
}

// recallmulti encoding [a, b] → prev, next, amt
static char ll_preset_interp_state(char interp, double a, double b, long *prev, long *next, double *amt)
{
    double wa, wb, sum;
    long pa, pb;

    if (!interp) return 0;
    pa = (long)floor(a);
    pb = (long)floor(b);
    wa = a - pa;
    wb = b - pb;
    sum = wa + wb;
    if (sum <= 0) return 0;

    *prev = pa;
    *next = pb;
    *amt = wb / sum;
    return 1;
}

static long ll_preset_current_slot(t_ll_preset *x)
{
    long max = x->num_cols * x->num_rows;
    char interp;
    double f;

    LL_LOCK(x);
    interp = x->val_interp;
    f = x->val_slot;
    LL_UNLOCK(x);

    if (interp) return 0; // js: [a, b] falls through to 0

    if (max > 0) {
        if (f < 1) f = 1;
        if (f > max) f = max;
    }
    return (long)floor(f + 0.5);
}

static double ll_preset_semantic_position(t_ll_preset *x)
{
    double pos = 0;

    LL_LOCK(x);
    if (ll_preset_stored_count(x)) {
        if (!x->val_interp) {
            long i = ll_preset_find_stored_index(x, x->val_slot);
            pos = i < 0 ? 0 : i;
        } else {
            double a = x->val_a, b = x->val_b;
            long pa = (long)floor(a), pb = (long)floor(b);
            double wa = a - pa, wb = b - pb, sum = wa + wb, amt;
            long ip = ll_preset_stored_indexof(x, pa), inx = ll_preset_stored_indexof(x, pb);

            if (ip < 0 || inx < 0) {
                pos = ip >= 0 ? ip : 0;
            } else {
                amt = sum > 0 ? wb / sum : 0;
                pos = ip + amt * (inx - ip);
            }
        }
    }
    LL_UNLOCK(x);
    return pos;
}

// where a ramp starts: the current slot, or the nearer one while interpolating
static long ll_preset_ramp_origin(t_ll_preset *x, long fallback)
{
    long p, n, c;
    double amt, a, b;
    char interp;

    LL_LOCK(x);
    interp = x->val_interp;
    a = x->val_a;
    b = x->val_b;
    LL_UNLOCK(x);

    if (ll_preset_interp_state(interp, a, b, &p, &n, &amt)) return amt < 0.5 ? p : n;

    c = ll_preset_current_slot(x);
    return c ? c : fallback;
}

// ------------------------------------------------------------------------
// patcher lookups (main thread)
// ------------------------------------------------------------------------

static t_object *ll_preset_find_named(t_object *patcher, t_symbol *name)
{
    t_object *b;
    if (!patcher) return NULL;
    for (b = jpatcher_get_firstobject(patcher); b; b = jbox_get_nextobject(b)) {
        if (jbox_get_varname(b) == name) return jbox_get_object(b);
    }
    return NULL;
}

static void ll_preset_track(t_ll_preset *x, t_object **slot, t_object *found)
{
    if (*slot == found) return;
    if (*slot) object_detach_byptr(x, *slot);
    *slot = found;
    if (found) object_attach_byptr_register(x, found, CLASS_BOX);
}

void ll_preset_refresh(t_ll_preset *x)
{
    if (!x->patcher) object_obex_lookup(x, gensym("#P"), &x->patcher);
    if (!x->patcher) return;

    ll_preset_track(x, &x->pat, ll_preset_find_named(x->patcher, llp_sym_pat));
    ll_preset_track(x, &x->preset_ramp, ll_preset_find_named(x->patcher, llp_sym_preset_ramp));
    ll_preset_track(x, &x->prepare_dump, ll_preset_find_named(x->patcher, llp_sym_prepare_dump));
    x->has_plugin_state = ll_preset_find_named(x->patcher, llp_sym_plugin_state) != NULL;
    x->has_param_select = ll_preset_find_named(x->patcher, llp_sym_param_select) != NULL;
    x->discrete_active = -1;
}

// find <name> ("actui", "pres_menu") in the act bpatcher: walk up to a box named
// "act" whose subpatcher holds it
static t_object *ll_preset_find_in_act(t_ll_preset *x, t_symbol *name)
{
    t_object *p = x->patcher;
    while (p) {
        t_object *act = ll_preset_find_named(p, llp_sym_act);
        if (act) {
            t_object *sub = (t_object *)object_method(act, llp_sym_subpatcher, (void *)0, (void *)0);
            if (sub) {
                t_object *o = ll_preset_find_named(sub, name);
                if (o) return o;
            }
        }
        p = jpatcher_get_parentpatcher(p);
    }
    return NULL;
}

static t_object *ll_preset_find_actui(t_ll_preset *x)
{
    return ll_preset_find_in_act(x, llp_sym_actui);
}

// queue "active_set <what> <slot>" for actui (any thread); consecutive recalls merge
static void ll_preset_notice(t_ll_preset *x, t_symbol *what, long slot)
{
    char merged = 0;

    LL_LOCK(x);
    if (x->notice_count && what == llp_sym_recall) {
        long last = (x->notice_head + x->notice_count - 1) % LL_PRESET_NOTICE_QUEUE;
        if (x->notices[last].what == llp_sym_recall) {
            x->notices[last].slot = slot;
            merged = 1;
        }
    }
    if (!merged && x->notice_count < LL_PRESET_NOTICE_QUEUE) {
        long idx = (x->notice_head + x->notice_count) % LL_PRESET_NOTICE_QUEUE;
        x->notices[idx].what = what;
        x->notices[idx].slot = slot;
        x->notice_count++;
    }
    LL_UNLOCK(x);

    qelem_set(x->actui_qelem);
}

// main thread: tell actui now (active_store must apply before the recall)
static void ll_preset_actui_now(t_ll_preset *x, t_symbol *what, long slot)
{
    t_object *ui = ll_preset_find_actui(x);
    t_atom a[2];
    if (!ui) return;
    atom_setsym(a, what);
    atom_setlong(a + 1, slot);
    object_method_typed(ui, llp_sym_active_set, 2, a, NULL);
}

static void ll_preset_actui_qfn(t_ll_preset *x)
{
    t_object *ui = ll_preset_find_actui(x);

    for (;;) {
        t_ll_preset_notice n;
        t_atom a[2];

        LL_LOCK(x);
        if (!x->notice_count) {
            LL_UNLOCK(x);
            break;
        }
        n = x->notices[x->notice_head];
        x->notice_head = (x->notice_head + 1) % LL_PRESET_NOTICE_QUEUE;
        x->notice_count--;
        LL_UNLOCK(x);

        if (!ui) continue;
        atom_setsym(a, n.what);
        atom_setlong(a + 1, n.slot);
        object_method_typed(ui, llp_sym_active_set, 2, a, NULL);
    }
}

static void ll_preset_resize_qfn(t_ll_preset *x)
{
    t_symbol *n = x->act_name;
    t_atom a[2];
    if (!n || n == llp_sym_empty || !n->s_thing) return;
    atom_setsym(a, llp_sym_change_TEXT);
    atom_setsym(a + 1, llp_sym_refresh);
    typedmess(n->s_thing, llp_sym_v8, 2, a);
}

// ------------------------------------------------------------------------
// pattrstorage communication (any thread)
// ------------------------------------------------------------------------

static void ll_preset_pat_send(t_ll_preset *x, t_symbol *msg, long ac, t_atom *av)
{
    if (!x->pat && systhread_ismainthread()) ll_preset_refresh(x);
    if (x->pat) object_method_typed(x->pat, msg, ac, av, NULL);
}

static void ll_preset_pat_send_long(t_ll_preset *x, t_symbol *msg, long n)
{
    t_atom a;
    atom_setlong(&a, n);
    ll_preset_pat_send(x, msg, 1, &a);
}

// whole-state clients (plugin@) can't be interpolated: inactive during ramps
static void ll_preset_set_discrete_active(t_ll_preset *x, int on)
{
    t_atom a[2];
    if (!x->pat) return;
    if (!on && x->discrete_active == 0) return;
    x->discrete_active = on;

    atom_setlong(a + 1, on ? 1 : 0);
    if (x->has_plugin_state) {
        atom_setsym(a, llp_sym_plugin_state);
        object_method_typed(x->pat, llp_sym_active, 2, a, NULL);
    }
    if (x->has_param_select) {
        atom_setsym(a, llp_sym_param_select);
        object_method_typed(x->pat, llp_sym_active, 2, a, NULL);
    }
}

static void ll_preset_notifyclients(t_ll_preset *x)
{
    object_notify(x, _sym_modified, NULL);
}

static void ll_preset_redraw(t_ll_preset *x)
{
    if (x->ready) jbox_redraw(&x->j_box);
}

// ------------------------------------------------------------------------
// value handling (js: normalizeValue / setvalueof)
// ------------------------------------------------------------------------

// caller holds the lock
static char ll_preset_normalize(t_ll_preset *x, long n, double v0, double v1, double v2,
                                long *prev, long *next, double *amt)
{
    long count = ll_preset_stored_count(x);
    if (!count) return 0;

    if (n >= 3) {
        double a = v2;
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        *prev = (long)v0;
        *next = (long)v1;
        *amt = a;
        return 1;
    }

    if (n == 1) {
        double f = v0;
        if (f == floor(f)) {
            // integer → absolute slot recall, or nearest stored slot
            long slot = (long)f;
            if (!ll_preset_is_stored(x, slot))
                slot = ll_preset_stored_at(x, ll_preset_find_stored_index(x, slot));
            *prev = *next = slot;
            *amt = 0;
            return 1;
        } else {
            // float → semantic interpolation over stored slots (1-based)
            double pos = f - 1.0;
            long i = (long)floor(pos);
            double a = pos - i;
            if (i < 0) {
                *prev = *next = ll_preset_stored_at(x, 0);
                *amt = 0;
            } else if (i >= count - 1) {
                *prev = *next = ll_preset_stored_at(x, count - 1);
                *amt = 0;
            } else {
                *prev = ll_preset_stored_at(x, i);
                *next = ll_preset_stored_at(x, i + 1);
                *amt = a;
            }
            return 1;
        }
    }
    return 0;
}

static void ll_preset_setvalue_raw(t_ll_preset *x, long prev, long next, double amt)
{
    // recalls nested in our own recall, again and again, are a loop
    if (x->own_change >= LLP_MAX_RECALL_DEPTH) {
        object_error((t_object *)x, "recall loop stopped: %ld nested recalls (now %ld %ld %g). "
                     "Something recalled by this slot sets the preset grid again.",
                     x->own_change, prev, next, amt);
        return;
    }

    if (prev == next || amt == 0) {
        t_atom a;

        if (x->active_store) {
            // the act must apply the slot's parameter set first: main thread only
            if (!systhread_ismainthread()) {
                x->deferred_slot = prev;
                qelem_set(x->recall_qelem);
                return;
            }
            ll_preset_actui_now(x, llp_sym_recall, prev);
        } else {
            ll_preset_notice(x, llp_sym_recall, prev);
        }

        LL_LOCK(x);
        x->val_interp = 0;
        x->val_slot = prev;
        LL_UNLOCK(x);

        ll_preset_set_discrete_active(x, 1);

        atom_setlong(&a, prev);
        x->own_change++;
        ll_preset_pat_send(x, llp_sym_recall, 1, &a);
        x->own_change--;
        x->cur_dirty = 0;
    } else {
        t_atom a[3];

        LL_LOCK(x);
        x->val_a = prev + (1.0 - amt);
        x->val_b = next + amt;
        x->val_interp = 1;
        LL_UNLOCK(x);

        ll_preset_set_discrete_active(x, 0);

        atom_setlong(a, prev);
        atom_setlong(a + 1, next);
        atom_setfloat(a + 2, amt);
        x->own_change++;
        ll_preset_pat_send(x, llp_sym_recall, 3, a);
        x->own_change--;
        x->cur_dirty = 0;
    }
    ll_preset_redraw(x);
}

static void ll_preset_apply(t_ll_preset *x, long n, double v0, double v1, double v2)
{
    long prev, next;
    double amt;
    char ok;

    LL_LOCK(x);
    ok = ll_preset_normalize(x, n, v0, v1, v2, &prev, &next, &amt);
    LL_UNLOCK(x);

    if (ok) ll_preset_setvalue_raw(x, prev, next, amt);
}

t_max_err ll_preset_getvalueof(t_ll_preset *x, long *ac, t_atom **av)
{
    char alloc, interp;
    double a, b, slot;
    long n;

    LL_LOCK(x);
    interp = x->val_interp;
    a = x->val_a;
    b = x->val_b;
    slot = x->val_slot;
    LL_UNLOCK(x);

    n = interp ? 2 : 1;
    if (ac && av && atom_alloc_array(n, ac, av, &alloc) == MAX_ERR_NONE) {
        if (interp) {
            atom_setfloat(*av, a);
            atom_setfloat(*av + 1, b);
        } else {
            atom_setlong(*av, (long)slot);
        }
        *ac = n;
    }
    return MAX_ERR_NONE;
}

static char ll_preset_is_number(t_atom *a)
{
    return atom_gettype(a) == A_LONG || atom_gettype(a) == A_FLOAT;
}

t_max_err ll_preset_setvalueof(t_ll_preset *x, long ac, t_atom *av)
{
    if (ac >= 3 && ll_preset_is_number(av) && ll_preset_is_number(av + 1))
        ll_preset_apply(x, 3, atom_getfloat(av), atom_getfloat(av + 1), atom_getfloat(av + 2));
    else if (ac == 1 && ll_preset_is_number(av))
        ll_preset_apply(x, 1, atom_getfloat(av), 0, 0);
    return MAX_ERR_NONE;
}

// ------------------------------------------------------------------------
// ramps (scheduler clock)
// ------------------------------------------------------------------------

static double ll_preset_getramp(t_ll_preset *x)
{
    long ac = 0;
    t_atom *av = NULL;
    double r = 0;

    if (!x->preset_ramp) return 0;
    object_getvalueof(x->preset_ramp, &ac, &av);
    if (ac && av) {
        r = atom_getfloat(av);
        sysmem_freeptr(av);
    }
    return r;
}

static void ll_preset_stop_ramp(t_ll_preset *x)
{
    x->ramp_running = 0;
    clock_unset(x->ramp_clock);
}

static void ll_preset_start_ramp(t_ll_preset *x, long prev, long next, double ms)
{
    x->ramp_prev = prev;
    x->ramp_next = next;
    x->ramp_time = MAX(1.0, ms);
    clock_getftime(&x->ramp_start);
    x->ramp_running = 1;
    clock_fdelay(x->ramp_clock, LL_PRESET_RAMP_INTERVAL);
}

static void ll_preset_ramp_tick(t_ll_preset *x)
{
    double now, t;

    if (!x->ramp_running) return;

    clock_getftime(&now);
    t = x->ramp_time > 0 ? (now - x->ramp_start) / x->ramp_time : 1;
    if (t >= 1) t = 1;

    ll_preset_apply(x, 3, x->ramp_prev, x->ramp_next, t);
    ll_preset_notifyclients(x);

    if (t >= 1) {
        // end of ramp: collapse to the target slot
        x->ramp_running = 0;
        ll_preset_apply(x, 1, x->ramp_next, 0, 0);
        ll_preset_notifyclients(x);
    } else {
        clock_fdelay(x->ramp_clock, LL_PRESET_RAMP_INTERVAL);
    }
}

// recall a slot like a click or an int: ramp if preset-ramp > 0
static void ll_preset_recall_slot(t_ll_preset *x, long slot)
{
    long origin = ll_preset_ramp_origin(x, slot);
    double r = ll_preset_getramp(x);

    if (r > 0 && origin != slot) {
        ll_preset_start_ramp(x, origin, slot, r);
    } else {
        ll_preset_stop_ramp(x);
        ll_preset_apply(x, 1, slot, 0, 0);
        ll_preset_notifyclients(x);
    }
}

// ------------------------------------------------------------------------
// store / delete / copy
// ------------------------------------------------------------------------

static void ll_preset_store(t_ll_preset *x, long slot)
{
    t_atom a;
    if (slot < 1 || slot > LL_PRESET_MAX_SLOTS) return;

    LL_LOCK(x);
    x->slots[slot] = 1;
    LL_UNLOCK(x);

    if (!x->pat && systhread_ismainthread()) ll_preset_refresh(x);

    // prepare_dump makes plugin@ update [pattr plugin_state]: not a user change
    x->own_change++;
    ll_preset_set_discrete_active(x, 1);
    // let the act refresh stored values first (plugin@ → [pattr plugin_state])
    if (x->prepare_dump) object_method(x->prepare_dump, _sym_bang);

    ll_preset_pat_send_long(x, llp_sym_store, slot);
    ll_preset_pat_send(x, llp_sym_getslotlist, 0, NULL);
    x->own_change--;

    x->cur_dirty = 0;
    x->slot_changed[slot] = 1;
    x->file_dirty = 1;

    // active_store: the set must be saved before the slot is recalled
    if (x->active_store && systhread_ismainthread()) ll_preset_actui_now(x, llp_sym_store, slot);
    else ll_preset_notice(x, llp_sym_store, slot);

    atom_setlong(&a, slot);
    outlet_anything(x->outlet, llp_sym_store, 1, &a);
}

static void ll_preset_delete(t_ll_preset *x, long slot)
{
    t_atom a;
    if (slot < 1 || slot > LL_PRESET_MAX_SLOTS) return;

    LL_LOCK(x);
    x->slots[slot] = 0;
    LL_UNLOCK(x);
    x->slot_changed[slot] = 1;
    x->file_dirty = 1;

    ll_preset_pat_send_long(x, llp_sym_delete, slot);
    ll_preset_pat_send(x, llp_sym_getslotlist, 0, NULL);

    atom_setlong(&a, slot);
    outlet_anything(x->outlet, llp_sym_delete, 1, &a);
    ll_preset_redraw(x);
}

void ll_preset_copy(t_ll_preset *x, long src, long dst)
{
    t_atom a[2];

    if (src < 1 || dst < 1 || src > LL_PRESET_MAX_SLOTS || dst > LL_PRESET_MAX_SLOTS || src == dst) return;
    if (!ll_preset_is_stored(x, src)) return;

    LL_LOCK(x);
    x->slots[dst] = 1;
    LL_UNLOCK(x);
    x->slot_changed[dst] = 1;
    x->file_dirty = 1;

    atom_setlong(a, src);
    atom_setlong(a + 1, dst);
    ll_preset_pat_send(x, llp_sym_copy, 2, a);
    ll_preset_pat_send(x, llp_sym_getslotlist, 0, NULL);

    outlet_anything(x->outlet, llp_sym_copy, 2, a);
    ll_preset_redraw(x);
}

// ------------------------------------------------------------------------
// messages
// ------------------------------------------------------------------------

void ll_preset_int(t_ll_preset *x, long n)
{
    ll_preset_recall_slot(x, n);
}

void ll_preset_float(t_ll_preset *x, double f)
{
    ll_preset_stop_ramp(x);
    ll_preset_apply(x, 1, f, 0, 0);
    ll_preset_notifyclients(x);
}

void ll_preset_list(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    if (ac >= 3) {
        ll_preset_stop_ramp(x);
        ll_preset_apply(x, 3, atom_getfloat(av), atom_getfloat(av + 1), atom_getfloat(av + 2));
        ll_preset_notifyclients(x);
    } else if (ac == 1) {
        if (atom_gettype(av) == A_FLOAT) ll_preset_float(x, atom_getfloat(av));
        else ll_preset_int(x, atom_getlong(av));
    }
}

void ll_preset_anything(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    if (ac == 1) {
        ll_preset_int(x, atom_getlong(av));
        return;
    }
    object_post((t_object *)x, "doesn't understand \"%s\"", s->s_name);
}

void ll_preset_bang(t_ll_preset *x)
{
    ll_preset_redraw(x);
    outlet_int(x->outlet, ll_preset_current_slot(x));
}

// step to the next / previous stored slot from the current position
static void ll_preset_step(t_ll_preset *x, long dir)
{
    long n, idx, slot;
    double pos;
    char nothing_recalled;

    pos = ll_preset_semantic_position(x); // takes the lock itself: never nest it

    LL_LOCK(x);
    n = ll_preset_stored_count(x);
    if (!n) {
        LL_UNLOCK(x);
        return;
    }
    nothing_recalled = !x->val_interp && x->val_slot < 1;

    if (nothing_recalled) idx = dir > 0 ? 0 : n - 1;
    else idx = dir > 0 ? (long)floor(pos + 1e-9) + 1 : (long)ceil(pos - 1e-9) - 1;
    if (x->wrap) {
        if (idx > n - 1) idx = 0;
        if (idx < 0) idx = n - 1;
    } else {
        if (idx < 0) idx = 0;
        if (idx > n - 1) idx = n - 1;
    }

    slot = ll_preset_stored_at(x, idx);
    LL_UNLOCK(x);

    ll_preset_recall_slot(x, slot);
}

void ll_preset_next(t_ll_preset *x)
{
    ll_preset_step(x, 1);
}

void ll_preset_prev(t_ll_preset *x)
{
    ll_preset_step(x, -1);
}

void ll_preset_store_msg(t_ll_preset *x, long slot)
{
    ll_preset_store(x, slot);
}

void ll_preset_slotlist(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    long i;
    LL_LOCK(x);
    memset(x->slots, 0, sizeof(x->slots));
    for (i = 0; i < ac; i++) {
        long n = atom_getlong(av + i);
        if (n >= 1 && n <= LL_PRESET_MAX_SLOTS) x->slots[n] = 1;
    }
    LL_UNLOCK(x);
    ll_preset_redraw(x);
}

void ll_preset_clear(t_ll_preset *x)
{
    LL_LOCK(x);
    memset(x->slots, 0, sizeof(x->slots));
    LL_UNLOCK(x);
    ll_preset_redraw(x);
}

void ll_preset_read(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    outlet_anything(x->outlet, llp_sym_getslotlist, 0, NULL);
}

void ll_preset_noop(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
}

void ll_preset_assist(t_ll_preset *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET) sprintf(s, "int: Recall Slot, float: Interpolate, slotlist from pattrstorage");
    else                   sprintf(s, "store / delete / copy / getslotlist, Current Slot on bang");
}

void ll_preset_about(t_ll_preset *x)
{
    post("ll.preset - v" LL_PRESET_VERSION);
}

void ll_preset_fontsize(t_ll_preset *x, double v)
{
    x->fontsize = v;
    ll_preset_redraw(x);
}

static void ll_preset_set_text_atoms(t_ll_preset *x, long ac, t_atom *av)
{
    t_symbol **nt = NULL, **old;
    long i;
    char buf[64];

    if (ac > 0) {
        nt = (t_symbol **)sysmem_newptrclear(ac * sizeof(t_symbol *));
        for (i = 0; i < ac; i++) {
            switch (atom_gettype(av + i)) {
            case A_SYM: nt[i] = atom_getsym(av + i); break;
            case A_LONG:
                snprintf(buf, sizeof(buf), "%ld", (long)atom_getlong(av + i));
                nt[i] = gensym(buf);
                break;
            case A_FLOAT:
                snprintf(buf, sizeof(buf), "%g", atom_getfloat(av + i));
                nt[i] = gensym(buf);
                break;
            default: nt[i] = llp_sym_empty;
            }
        }
    }

    critical_enter(x->text_lock);
    old = x->text;
    x->text = nt;
    x->text_count = nt ? ac : 0;
    critical_exit(x->text_lock);

    if (old) sysmem_freeptr(old);
}

void ll_preset_text(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    ll_preset_set_text_atoms(x, ac, av);
    ll_preset_redraw(x);
}

void ll_preset_set_TEXT_data(t_ll_preset *x, t_symbol *dictname)
{
    t_dictionary *d = dictobj_findregistered_retain(dictname);
    long ac = 0;
    t_atom *av = NULL;
    t_atom fs;

    if (!d) return;

    if (dictionary_copyatoms(d, llp_sym_text, &ac, &av) == MAX_ERR_NONE) {
        ll_preset_set_text_atoms(x, ac, av);
        if (av) sysmem_freeptr(av);
    }
    if (dictionary_getatom(d, llp_sym_fontsize, &fs) == MAX_ERR_NONE) x->fontsize = atom_getfloat(&fs);

    dictobj_release(d);
    ll_preset_redraw(x);
}

void ll_preset_actname(t_ll_preset *x, t_symbol *s)
{
    x->act_name = s;
    ll_preset_refresh(x);
    if (x->pat) object_method_typed(x->pat, llp_sym_getslotlist, 0, NULL, NULL);
    ll_preset_redraw(x);
}

void ll_preset_q_size(t_ll_preset *x, long v)
{
    t_atom a;
    atom_setlong(&a, v);
    object_attr_setvalueof(x, llp_sym_boxsize, 1, &a);
}

void ll_preset_scrubrate(t_ll_preset *x, double v)
{
    x->scrub_rate = v;
}

t_max_err ll_preset_boxsize_set(t_ll_preset *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        long v = atom_getlong(av);
        if (v < 2) v = 2;
        x->boxsize = v;
        if (x->last_w > 0) ll_preset_recompute(x, x->last_w, x->last_h);
        x->geom_dirty = 1;
        ll_preset_redraw(x);
    }
    return MAX_ERR_NONE;
}

// ------------------------------------------------------------------------
// change tracking
// ------------------------------------------------------------------------

static char ll_preset_is_ignored(t_ll_preset *x, t_symbol *client)
{
    long i;
    const char *name = client->s_name;

    for (i = 0; i < x->ignore_count; i++) {
        const char *ig = x->ignore[i]->s_name;
        size_t n = strlen(ig);
        if (!strcmp(name, ig)) return 1;
        if (!strncmp(name, ig, n) && name[n] == ':' && name[n + 1] == ':') return 1; // "ig::..."
    }
    return 0;
}

// a parameter moved (any thread): "changed <client> <value...>"
void ll_preset_changed(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    t_symbol *client;

    if (!ac || atom_gettype(av) != A_SYM) return;
    client = atom_getsym(av);

    if (x->own_change > 0 || x->ramp_running) return; // our own recall / ramp / store (too many to log)
    if (!strncmp(client->s_name, "act::", 5)) return; // act menus, active_store ...
    if (ll_preset_is_ignored(x, client)) return;
    if (ll_preset_client_inactive(x, client)) return; // not part of the presets

    if (!x->cur_dirty) {
        x->cur_dirty = 1;
        ll_preset_redraw(x);
    }
}

// "clean": preset file loaded or cleared. "clean file": preset file written.
void ll_preset_clean(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    char file_only = ac && atom_gettype(av) == A_SYM && atom_getsym(av) == llp_sym_file;

    x->file_dirty = 0;
    memset(x->slot_changed, 0, sizeof(x->slot_changed));
    if (!file_only) x->cur_dirty = 0;
    ll_preset_redraw(x);
}

void ll_preset_ignore(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    long i;
    x->ignore_count = 0;
    for (i = 0; i < ac && x->ignore_count < LL_PRESET_MAX_IGNORE; i++)
        if (atom_gettype(av + i) == A_SYM) x->ignore[x->ignore_count++] = atom_getsym(av + i);
}

// recall handed over from the scheduler (active_store on)
static void ll_preset_recall_qfn(t_ll_preset *x)
{
    long slot = x->deferred_slot;
    if (slot < 1) return;
    ll_preset_actui_qfn(x); // pending store notices first (their parameter sets)
    ll_preset_setvalue_raw(x, slot, slot, 0);
    ll_preset_notifyclients(x);
}

// "clients <name> <0|1> <name> <0|1> ..." from ll.act_main.js
void ll_preset_clients(t_ll_preset *x, t_symbol *s, long ac, t_atom *av)
{
    long i, n = 0;
    t_symbol **names = NULL, **old_names;
    char *active = NULL, *old_active;

    if (ac >= 2) {
        names = (t_symbol **)sysmem_newptrclear((ac / 2) * sizeof(t_symbol *));
        active = (char *)sysmem_newptrclear(ac / 2);
        for (i = 0; i + 1 < ac; i += 2) {
            if (atom_gettype(av + i) != A_SYM) continue;
            names[n] = atom_getsym(av + i);
            active[n] = atom_getlong(av + i + 1) != 0;
            n++;
        }
    }

    LL_LOCK(x);
    old_names = x->client_names;
    old_active = x->client_active;
    x->client_names = names;
    x->client_active = active;
    x->client_count = n;
    x->have_clients = 1;
    LL_UNLOCK(x);

    if (old_names) sysmem_freeptr(old_names);
    if (old_active) sysmem_freeptr(old_active);
}

void ll_preset_activestore(t_ll_preset *x, long on)
{
    x->active_store = on != 0;
}

// is this client excluded from presets (active 0)? unknown clients count as included
static char ll_preset_client_inactive(t_ll_preset *x, t_symbol *client)
{
    long i;
    char inactive = 0;

    LL_LOCK(x);
    if (x->have_clients) {
        for (i = 0; i < x->client_count; i++) {
            if (x->client_names[i] == client) {
                inactive = !x->client_active[i];
                break;
            }
        }
    }
    LL_UNLOCK(x);
    return inactive;
}

// parameters submenu: include / exclude a client (pattrstorage active flag)
static void ll_preset_set_client_active(t_ll_preset *x, t_symbol *client, char on)
{
    t_atom a[2];
    const char *sep = strstr(client->s_name, "::");
    long i;

    // a parameter inside a subpatcher needs the subpatcher active too
    if (on && sep) {
        char parent[256];
        size_t n = (size_t)(sep - client->s_name);
        if (n >= sizeof(parent)) n = sizeof(parent) - 1;
        memcpy(parent, client->s_name, n);
        parent[n] = '\0';
        atom_setsym(a, gensym(parent));
        atom_setlong(a + 1, 1);
        ll_preset_pat_send(x, llp_sym_active, 2, a);
    }

    atom_setsym(a, client);
    atom_setlong(a + 1, on ? 1 : 0);
    ll_preset_pat_send(x, llp_sym_active, 2, a);

    LL_LOCK(x);
    for (i = 0; i < x->client_count; i++)
        if (x->client_names[i] == client) x->client_active[i] = on;
    LL_UNLOCK(x);

    // with active_store the slot's stored set now differs; the file stores flags too
    if (x->active_store) x->cur_dirty = 1;
    x->file_dirty = 1;
}

// ------------------------------------------------------------------------
// init
// ------------------------------------------------------------------------

static void ll_preset_init(t_ll_preset *x)
{
    t_object *marker;

    ll_preset_refresh(x);
    if (x->pat) object_method_typed(x->pat, llp_sym_getslotlist, 0, NULL, NULL);

    marker = ll_preset_find_named(x->patcher, llp_sym_pattrmarker);
    if (marker) {
        t_symbol *nm = object_attr_getsym(marker, llp_sym_name);
        if (nm && nm != llp_sym_no && nm != llp_sym_empty) ll_preset_actname(x, nm);
    }

    if (x->patcher && object_getmethod(x->patcher, llp_sym_bringtofront))
        object_method(x->patcher, llp_sym_bringtofront, x);

    ll_preset_redraw(x);
}

void ll_preset_loadbang(t_ll_preset *x)
{
    qelem_unset(x->init_qelem);
    ll_preset_init(x);
}

static void ll_preset_init_qfn(t_ll_preset *x)
{
    ll_preset_init(x);
}

void *ll_preset_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ll_preset *x;
    t_dictionary *d = object_dictionaryarg(argc, argv);
    long flags;
    t_rect r;

    if (!d) return NULL;
    if (!(x = (t_ll_preset *)object_alloc(s_ll_preset_class))) return NULL;

    flags = 0 | JBOX_DRAWFIRSTIN | JBOX_NODRAWBOX | JBOX_DRAWINLAST | JBOX_GROWBOTH;
    jbox_new(&x->j_box, flags, argc, argv);
    x->j_box.b_firstin = (t_object *)x;
    x->outlet = outlet_new(x, NULL);

    x->boxsize = 12;
    x->g_margin = 1;
    x->g_inner = 1;
    x->last_w = x->last_h = -1;
    x->geom_dirty = 1;
    x->hover_index = -1;
    x->scrub_rate = 2;
    x->fontsize = 12;
    x->discrete_active = -1;
    x->ignore[x->ignore_count++] = gensym("act");
    x->ignore[x->ignore_count++] = gensym("ll.blues");
    x->ignore[x->ignore_count++] = gensym("presets");
    x->ignore[x->ignore_count++] = gensym("preset-ramp");
    critical_new(&x->lock);
    critical_new(&x->text_lock);
    x->ramp_clock = clock_new(x, (method)ll_preset_ramp_tick);
    x->init_qelem = qelem_new(x, (method)ll_preset_init_qfn);
    x->actui_qelem = qelem_new(x, (method)ll_preset_actui_qfn);
    x->resize_qelem = qelem_new(x, (method)ll_preset_resize_qfn);
    x->recall_qelem = qelem_new(x, (method)ll_preset_recall_qfn);

    attr_dictionary_process(x, d);
    jbox_ready(&x->j_box);
    x->ready = 1;

    // geometry before the first paint, so int / float work immediately
    jbox_get_patching_rect((t_object *)x, &r);
    ll_preset_recompute(x, r.width, r.height);
    x->geom_dirty = 1;

    object_obex_lookup(x, gensym("#P"), &x->patcher);
    // objects created in an already-open patcher get no loadbang
    qelem_set(x->init_qelem);

    return x;
}

void ll_preset_free(t_ll_preset *x)
{
    x->ramp_running = 0;
    if (x->ramp_clock) {
        clock_unset(x->ramp_clock);
        object_free(x->ramp_clock);
    }
    // freeing a qelem also cancels it if it's still pending
    if (x->init_qelem) qelem_free(x->init_qelem);
    if (x->actui_qelem) qelem_free(x->actui_qelem);
    if (x->resize_qelem) qelem_free(x->resize_qelem);
    if (x->recall_qelem) qelem_free(x->recall_qelem);
    if (x->client_names) sysmem_freeptr(x->client_names);
    if (x->client_active) sysmem_freeptr(x->client_active);

    if (x->drag_delta) jbox_set_mousedragdelta((t_object *)x, 0);

    ll_preset_track(x, &x->pat, NULL);
    ll_preset_track(x, &x->preset_ramp, NULL);
    ll_preset_track(x, &x->prepare_dump, NULL);
    if (x->text) sysmem_freeptr(x->text);
    critical_free(x->text_lock);
    critical_free(x->lock);
    jbox_free(&x->j_box);
}

t_max_err ll_preset_notify(t_ll_preset *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (msg == _sym_free) {
        if (sender == x->pat) x->pat = NULL;
        if (sender == x->preset_ramp) x->preset_ramp = NULL;
        if (sender == x->prepare_dump) x->prepare_dump = NULL;
    }
    return jbox_notify(&x->j_box, s, msg, sender, data);
}

// ------------------------------------------------------------------------
// paint
// ------------------------------------------------------------------------

static double ll_preset_rand01(long seed)
{
    double v = sin((double)seed) * 10000.0;
    return v - floor(v);
}

void ll_preset_paint(t_ll_preset *x, t_object *view)
{
    t_rect rect;
    t_jgraphics *g = (t_jgraphics *)patcherview_get_jgraphics(view);
    double sq, inner, y, va, vb;
    long row, col, i, cur, max, iprev = 0, inext = 0;
    double iamt = 0;
    char interp_active, vinterp;
    char slots[LL_PRESET_MAX_SLOTS + 1];
    char cur_dirty = x->cur_dirty;

    jbox_get_rect_for_view((t_object *)x, view, &rect);

    if (x->geom_dirty || rect.width != x->last_w || rect.height != x->last_h) {
        char resized = !x->geom_dirty && x->last_w >= 0;
        ll_preset_recompute(x, rect.width, rect.height);
        if (resized) qelem_set(x->resize_qelem);
    }

    // snapshot the shared state, then draw without holding the lock
    max = ll_preset_maxslot(x);
    LL_LOCK(x);
    memcpy(slots, x->slots, (size_t)(max + 1));
    vinterp = x->val_interp;
    va = x->val_a;
    vb = x->val_b;
    LL_UNLOCK(x);

    sq = x->g_square;
    inner = x->g_inner;

    jgraphics_set_source_jrgba(g, &x->vbrgb);
    jgraphics_rectangle(g, 0, 0, rect.width, rect.height);
    jgraphics_fill(g);

    interp_active = ll_preset_interp_state(vinterp, va, vb, &iprev, &inext, &iamt);
    cur = ll_preset_current_slot(x);

    // squares
    i = 0;
    y = x->g_margin + sq;
    for (row = 0; row < x->num_rows; row++) {
        double xx = x->g_margin + sq;
        for (col = 0; col < x->num_cols; col++) {
            char stored, is_current;
            t_jrgba *color = &x->vfrgb;

            i++;
            stored = i <= max ? slots[i] : 0;
            is_current = (cur == i) && stored;
            if (is_current && !interp_active) color = &x->vrgb2;
            else if (stored) color = &x->vrgb3;

            jgraphics_set_source_jrgba(g, color);
            jgraphics_rectangle(g, xx - sq, y - sq, sq * 2, sq * 2);
            jgraphics_fill(g);

            // slot changed since the preset file was loaded / written: bottom-right corner
            if (i <= max && x->slot_changed[i]) {
                double k = MAX(3.0, sq * 0.7);
                jgraphics_set_source_jrgba(g, &x->vbrgb);
                jgraphics_move_to(g, xx + sq, y + sq);
                jgraphics_line_to(g, xx + sq - k, y + sq);
                jgraphics_line_to(g, xx + sq, y + sq - k);
                jgraphics_close_path(g);
                jgraphics_fill(g);
            }

            // current slot's parameters moved since recall / store: top-right dot
            if (cur_dirty && is_current && !interp_active) {
                double rad = MAX(1.5, sq * 0.22);
                jgraphics_set_source_jrgba(g, &x->vbrgb);
                jgraphics_ellipse(g, xx + sq - 2 * rad - 1, y - sq + 1, 2 * rad, 2 * rad);
                jgraphics_fill(g);
            }

            // interpolation: random sub-cells fill (next) / drain (prev)
            if (interp_active && (i == iprev || i == inext)) {
                double cw = (sq * 2) / LL_PRESET_RAND_CELLS;
                double threshold = (i == inext) ? iamt : 1 - iamt;
                long rx, ry;

                jgraphics_set_source_jrgba(g, &x->vrgb2);
                for (ry = 0; ry < LL_PRESET_RAND_CELLS; ry++) {
                    for (rx = 0; rx < LL_PRESET_RAND_CELLS; rx++) {
                        long seed = i * 1000 + ry * LL_PRESET_RAND_CELLS + rx;
                        if (ll_preset_rand01(seed) < threshold) {
                            jgraphics_rectangle(g, xx - sq + rx * cw, y - sq + ry * cw, cw, cw);
                        }
                    }
                }
                jgraphics_fill(g);
            }

            // hover overlay + icon
            if (!interp_active && !x->option_down && i == x->hover_index) {
                t_jrgba c = x->vbrgb;
                double size = sq * 0.9;

                c.alpha = 0.2;
                jgraphics_set_source_jrgba(g, &c);
                jgraphics_rectangle(g, xx - sq, y - sq, sq * 2, sq * 2);
                jgraphics_fill(g);

                c.alpha = 1.0;
                jgraphics_set_source_jrgba(g, &c);
                jgraphics_set_line_width(g, 1.5);

                if (x->hover_add || x->hover_replace) {
                    // +
                    jgraphics_move_to(g, xx - size / 2, y);
                    jgraphics_line_to(g, xx + size / 2, y);
                    jgraphics_move_to(g, xx, y - size / 2);
                    jgraphics_line_to(g, xx, y + size / 2);
                    jgraphics_stroke(g);
                } else if (x->hover_remove) {
                    // ×
                    jgraphics_move_to(g, xx - size / 2, y - size / 2);
                    jgraphics_line_to(g, xx + size / 2, y + size / 2);
                    jgraphics_move_to(g, xx - size / 2, y + size / 2);
                    jgraphics_line_to(g, xx + size / 2, y - size / 2);
                    jgraphics_stroke(g);
                }
            }

            xx += sq * 2 + inner;
        }
        y += sq * 2 + inner;
    }

    // text, left-aligned inside each square, overflowing right
    critical_enter(x->text_lock);
    if (x->text && x->text_count) {
        jgraphics_set_source_jrgba(g, &x->vbrgb);
        jgraphics_select_font_face(g, "Arial", JGRAPHICS_FONT_SLANT_NORMAL, JGRAPHICS_FONT_WEIGHT_NORMAL);
        jgraphics_set_font_size(g, x->fontsize);

        i = 0;
        y = x->g_margin + sq;
        for (row = 0; row < x->num_rows; row++) {
            double xx = x->g_margin + sq;
            for (col = 0; col < x->num_cols; col++) {
                i++;
                if (i - 1 < x->text_count && x->text[i - 1] && x->text[i - 1]->s_name[0]) {
                    const char *txt = x->text[i - 1]->s_name;
                    double tw, th;
                    jgraphics_text_measure(g, txt, &tw, &th);
                    jgraphics_move_to(g, xx - sq + 2, y + th * 0.35);
                    jgraphics_show_text(g, txt);
                }
                xx += sq * 2 + inner;
            }
            y += sq * 2 + inner;
        }
    }
    critical_exit(x->text_lock);
}

// ------------------------------------------------------------------------
// right-click menu (main thread)
// ------------------------------------------------------------------------

// ---- the act's pres_menu (ll.act_main.js) ----
// read when the popup opens; choosing an item sends its index to the umenu

#define LL_PRESET_MAX_PRES_ITEMS 512

typedef struct _ll_preset_pres {
    long count;
    long idx[LL_PRESET_MAX_PRES_ITEMS];       // index in the umenu
    char text[LL_PRESET_MAX_PRES_ITEMS][128]; // item text
    t_atom current;                           // umenu value (symbol or index)
    char has_current;
} t_ll_preset_pres;

// umenu item separator: a comma atom, or "," as in saved patchers
static char ll_preset_is_item_sep(t_atom *a)
{
    long t = atom_gettype(a);
    return t == A_COMMA || t == A_SEMI || (t == A_SYM && atom_getsym(a) == llp_sym_comma);
}

static void ll_preset_pres_push(t_ll_preset_pres *p, long idx, const char *item)
{
    if (p->count >= LL_PRESET_MAX_PRES_ITEMS) return;
    p->idx[p->count] = idx;
    snprintf(p->text[p->count], sizeof(p->text[0]), "%s", item);
    p->count++;
}

// read the umenu's items (an item can be several atoms, e.g. "ƒ default")
static void ll_preset_pres_read(t_object *pres, t_ll_preset_pres *p)
{
    long ac = 0, cac = 0, i, idx = 0;
    t_atom *av = NULL, *cav = NULL;
    char item[256], has_seps = 0;

    p->count = 0;
    p->has_current = 0;

    object_attr_getvalueof(pres, llp_sym_items, &ac, &av);
    if (ac && av) {
        for (i = 0; i < ac; i++)
            if (ll_preset_is_item_sep(av + i)) has_seps = 1;

        item[0] = '\0';
        for (i = 0; i <= ac; i++) {
            char part[128];

            if (i == ac || ll_preset_is_item_sep(av + i)) {
                ll_preset_pres_push(p, idx++, item);
                item[0] = '\0';
                continue;
            }
            if (!has_seps && i > 0) {
                ll_preset_pres_push(p, idx++, item);
                item[0] = '\0';
            }
            switch (atom_gettype(av + i)) {
            case A_SYM: snprintf(part, sizeof(part), "%s", atom_getsym(av + i)->s_name); break;
            case A_LONG: snprintf(part, sizeof(part), "%ld", (long)atom_getlong(av + i)); break;
            case A_FLOAT: snprintf(part, sizeof(part), "%g", atom_getfloat(av + i)); break;
            default: part[0] = '\0';
            }
            if (item[0] && strlen(item) < sizeof(item) - 1) strcat(item, " ");
            strncat(item, part, sizeof(item) - strlen(item) - 1);
        }
        sysmem_freeptr(av);
    }

    object_getvalueof(pres, &cac, &cav);
    if (cac && cav) {
        p->current = cav[0];
        p->has_current = 1;
    }
    if (cav) sysmem_freeptr(cav);
}

static long ll_preset_pres_find(t_ll_preset_pres *p, const char *name)
{
    long i;
    for (i = 0; i < p->count; i++)
        if (!strcmp(p->text[i], name)) return p->idx[i];
    return -1;
}

static char ll_preset_pres_is_action(const char *t)
{
    return !strcmp(t, "write") || !strcmp(t, "clear!") || !strcmp(t, "TEXT");
}

// the loaded preset file, from the umenu value; NULL if none
static const char *ll_preset_pres_current_name(t_ll_preset_pres *p)
{
    const char *name = NULL;
    long i;

    if (!p->has_current) return NULL;
    if (atom_gettype(&p->current) == A_SYM) {
        name = atom_getsym(&p->current)->s_name;
    } else if (atom_gettype(&p->current) == A_LONG) {
        for (i = 0; i < p->count; i++)
            if (p->idx[i] == atom_getlong(&p->current)) name = p->text[i];
    }
    if (!name || !name[0] || name[0] == '(' || !strcmp(name, "-") || !strcmp(name, "_")) return NULL;
    if (ll_preset_pres_is_action(name)) return NULL;
    return name;
}

// preset files submenu: all items but the title, "_" and write / clear! / TEXT
static long ll_preset_pres_fill_files(t_ll_preset_pres *p, t_jpopupmenu *sub, const char *current)
{
    long i, files = 0;
    char pending_sep = 0;

    for (i = 0; i < p->count; i++) {
        const char *t = p->text[i];
        size_t len = strlen(t);

        if (!len || !strcmp(t, "_") || ll_preset_pres_is_action(t)) continue;
        if (t[0] == '(' && t[len - 1] == ')') continue;
        if (!strcmp(t, "-")) {
            if (files) pending_sep = 1;
            continue;
        }
        if (pending_sep) {
            jpopupmenu_addseperator(sub);
            pending_sep = 0;
        }
        jpopupmenu_additem(sub, MENU_PRES + (int)p->idx[i], t, NULL, current && !strcmp(current, t), 0, NULL);
        files++;
    }
    return files;
}

static void ll_preset_choose_pres_item(t_object *pres, long idx)
{
    t_atom a;
    atom_setlong(&a, idx);
    object_method_typed(pres, llp_sym_int, 1, &a, NULL);
}

// parameters submenu: names sorted alphabetically, ignoring case
typedef struct _ll_preset_param_item {
    t_symbol *name;
    char active;
} t_ll_preset_param_item;

static int ll_preset_param_cmp(const void *a, const void *b)
{
    const unsigned char *p = (const unsigned char *)((const t_ll_preset_param_item *)a)->name->s_name;
    const unsigned char *q = (const unsigned char *)((const t_ll_preset_param_item *)b)->name->s_name;
    for (;; p++, q++) {
        int cp = tolower(*p), cq = tolower(*q);
        if (cp != cq || !cp) return cp != cq ? cp - cq : strcmp(((const t_ll_preset_param_item *)a)->name->s_name,
                                                                ((const t_ll_preset_param_item *)b)->name->s_name);
    }
}

// ---- the popup ----

static void ll_preset_menu(t_ll_preset *x, t_object *view, t_pt pt)
{
    t_jpopupmenu *m, *sub = NULL, *psub = NULL;
    t_symbol **pnames = NULL; // snapshot of the clients list, indexed by MENU_PARAM + i
    long pcount = 0;
    t_jfont *font;
    t_ll_preset_pres *pres_items = NULL;
    long c = ll_preset_coord_to_square(x, pt.x, pt.y);
    char stored = ll_preset_is_stored(x, c);
    char can_paste = x->copy_slot && x->copy_slot != c && ll_preset_is_stored(x, x->copy_slot);
    t_object *pres = ll_preset_find_in_act(x, llp_sym_pres_menu);
    double r = ll_preset_getramp(x);
    long count;
    int gx = 0, gy = 0, choice;
    t_pt screen;
    char buf[160];

    LL_LOCK(x);
    count = ll_preset_stored_count(x);
    LL_UNLOCK(x);

    // highlight the slot the menu is for, and keep it while the menu is open
    x->hover_index = c ? c : -1;
    x->hover_add = c && !stored;
    x->hover_remove = x->hover_replace = 0;
    x->menu_open = 1;
    ll_preset_redraw(x);

    m = jpopupmenu_create();
    font = jfont_create(LL_PRESET_MENU_FONT, JGRAPHICS_FONT_SLANT_NORMAL, JGRAPHICS_FONT_WEIGHT_NORMAL,
                        LL_PRESET_MENU_FONTSIZE);
    jpopupmenu_setfont(m, font);

    // slot section
    if (c) {
        if (stored) {
            snprintf(buf, sizeof(buf), "slot %ld", c);
            jpopupmenu_additem(m, MENU_NONE, buf, NULL, 0, 1, NULL);
            jpopupmenu_additem(m, MENU_RECALL, "recall", NULL, 0, 0, NULL);
            if (r > 0) jpopupmenu_additem(m, MENU_RECALL_NORAMP, "recall without ramp", NULL, 0, 0, NULL);
            jpopupmenu_additem(m, MENU_STORE, "store again", NULL, 0, 0, NULL);
            jpopupmenu_additem(m, MENU_COPY, "copy", NULL, 0, 0, NULL);
        } else {
            snprintf(buf, sizeof(buf), "slot %ld (empty)", c);
            jpopupmenu_additem(m, MENU_NONE, buf, NULL, 0, 1, NULL);
            jpopupmenu_additem(m, MENU_STORE, "store", NULL, 0, 0, NULL);
        }
        if (can_paste) {
            snprintf(buf, sizeof(buf), "paste from slot %ld", x->copy_slot);
            jpopupmenu_additem(m, MENU_PASTE, buf, NULL, 0, 0, NULL);
        }
        if (stored) {
            snprintf(buf, sizeof(buf), "delete slot %ld", c);
            jpopupmenu_additem(m, MENU_DELETE, buf, NULL, 0, 0, NULL);
        }
        jpopupmenu_addseperator(m);
    }

    // navigation
    jpopupmenu_additem(m, MENU_NEXT, "next slot", NULL, 0, count < 1, NULL);
    jpopupmenu_additem(m, MENU_PREV, "previous slot", NULL, 0, count < 1, NULL);

    // the act's parameters: which ones are included in the presets
    LL_LOCK(x);
    if (x->have_clients && x->client_count) {
        long i;
        pnames = (t_symbol **)sysmem_newptrclear(x->client_count * sizeof(t_symbol *));
        psub = jpopupmenu_create();
        jpopupmenu_setfont(psub, font);
        jpopupmenu_additem(psub, MENU_NONE,
                           x->active_store ? "included in this slot (saved on store)" : "included in all slots",
                           NULL, 0, 1, NULL);
        jpopupmenu_addseperator(psub);

        // collect, sort, then add in that order (pnames follows the menu order)
        {
            t_ll_preset_param_item *items =
                (t_ll_preset_param_item *)sysmem_newptrclear(x->client_count * sizeof(t_ll_preset_param_item));
            long n = 0;
            for (i = 0; i < x->client_count; i++) {
                t_symbol *name = x->client_names[i];
                if (!strcmp(name->s_name, "act") || !strncmp(name->s_name, "act::", 5)) continue;
                if (ll_preset_is_ignored(x, name)) continue;
                items[n].name = name;
                items[n].active = x->client_active[i];
                n++;
            }
            qsort(items, (size_t)n, sizeof(t_ll_preset_param_item), ll_preset_param_cmp);
            for (i = 0; i < n; i++) {
                pnames[pcount] = items[i].name;
                jpopupmenu_additem(psub, MENU_PARAM + (int)pcount, items[i].name->s_name, NULL, items[i].active, 0, NULL);
                pcount++;
            }
            sysmem_freeptr(items);
        }
    }
    LL_UNLOCK(x);
    if (psub) {
        jpopupmenu_addseperator(m);
        jpopupmenu_addsubmenu(m, "parameters", psub, pcount == 0);
    }

    // the act's preset files (pres_menu)
    if (pres) pres_items = (t_ll_preset_pres *)sysmem_newptrclear(sizeof(t_ll_preset_pres));
    if (pres_items) {
        const char *current;
        long files, i_write, i_text, i_clear;

        ll_preset_pres_read(pres, pres_items);
        current = ll_preset_pres_current_name(pres_items);
        i_write = ll_preset_pres_find(pres_items, "write");
        i_text = ll_preset_pres_find(pres_items, "TEXT");
        i_clear = ll_preset_pres_find(pres_items, "clear!");

        jpopupmenu_addseperator(m);
        snprintf(buf, sizeof(buf), "%s%s", current ? current : "no preset file", x->file_dirty ? " *" : "");
        jpopupmenu_additem(m, MENU_NONE, buf, NULL, 0, 1, NULL);

        sub = jpopupmenu_create();
        jpopupmenu_setfont(sub, font);
        files = ll_preset_pres_fill_files(pres_items, sub, current);
        jpopupmenu_addsubmenu(m, "load preset file", sub, files == 0);

        if (i_write >= 0) jpopupmenu_additem(m, MENU_PRES + (int)i_write, "write", NULL, 0, 0, NULL);
        if (i_text >= 0) jpopupmenu_additem(m, MENU_PRES + (int)i_text, "TEXT", NULL, 0, 0, NULL);
        if (i_clear >= 0) {
            jpopupmenu_addseperator(m);
            jpopupmenu_additem(m, MENU_PRES + (int)i_clear, "clear!", NULL, 0, 0, NULL);
        }
    }

    jmouse_getposition_global(&gx, &gy);
    screen.x = gx;
    screen.y = gy;
    choice = jpopupmenu_popup(m, screen, 0);

    jpopupmenu_destroy(m);
    if (sub) jpopupmenu_destroy(sub); // added with addsubmenu: the parent doesn't own it
    if (psub) jpopupmenu_destroy(psub);
    jfont_destroy(font);
    if (pres_items) sysmem_freeptr(pres_items);
    x->menu_open = 0;

    switch (choice) {
    case MENU_RECALL: ll_preset_recall_slot(x, c); break; // same as a click
    case MENU_RECALL_NORAMP:
        ll_preset_stop_ramp(x);
        ll_preset_apply(x, 1, c, 0, 0);
        ll_preset_notifyclients(x);
        break;
    case MENU_STORE:
        ll_preset_store(x, c);
        ll_preset_apply(x, 1, c, 0, 0);
        ll_preset_notifyclients(x);
        break;
    case MENU_COPY: x->copy_slot = c; break;
    case MENU_PASTE: ll_preset_copy(x, x->copy_slot, c); break;
    case MENU_DELETE: ll_preset_delete(x, c); break;
    case MENU_NEXT: ll_preset_next(x); break;
    case MENU_PREV: ll_preset_prev(x); break;
    default:
        if (choice >= MENU_PARAM && choice < MENU_PARAM + pcount) {
            t_symbol *name = pnames[choice - MENU_PARAM];
            ll_preset_set_client_active(x, name, ll_preset_client_inactive(x, name));
        } else if (choice >= MENU_PRES && choice < MENU_PARAM && pres) {
            ll_preset_choose_pres_item(pres, choice - MENU_PRES);
        }
        break;
    }
    if (pnames) sysmem_freeptr(pnames);
    ll_preset_redraw(x);
}

// ------------------------------------------------------------------------
// mouse
// ------------------------------------------------------------------------

// delta: endless drag (scrub started by the click), else absolute positions
static void ll_preset_begin_scrub(t_ll_preset *x, t_object *view, char delta, double startx)
{
    x->option_scrub_active = 1;
    x->option_down = 1;
    x->drag_start = ll_preset_semantic_position(x);
    x->scrub_accum = 0;
    x->last_drag_x = startx;
    x->have_last_drag_x = 1;
    ll_preset_stop_ramp(x);

    if (delta) {
        x->drag_delta = 1;
        jbox_set_mousedragdelta((t_object *)x, 1);
    } else {
        jmouse_setcursor(view, (t_object *)x, JMOUSE_CURSOR_RESIZE_LEFTRIGHT);
    }
    ll_preset_redraw(x);
}

static void ll_preset_end_scrub(t_ll_preset *x)
{
    char was = x->option_down || x->option_scrub_active;
    x->option_down = 0;
    x->option_scrub_active = 0;
    x->have_last_drag_x = 0;
    if (was) ll_preset_redraw(x);
}

// move the scrub position by dx pixels, in stored-index space
static void ll_preset_scrub_by(t_ll_preset *x, double dx)
{
    long n, idx, prev, next;
    double f, amt;

    x->scrub_accum += dx;

    LL_LOCK(x);
    n = ll_preset_stored_count(x);
    if (n < 2) {
        LL_UNLOCK(x);
        return;
    }

    f = x->drag_start + (x->scrub_accum * x->scrub_rate) / 100.0;
    if (f < 0 || f > n - 1) {
        f = f < 0 ? 0 : n - 1;
        // keep the accumulator at the end, so reversing responds right away
        if (x->scrub_rate != 0) x->scrub_accum = (f - x->drag_start) * 100.0 / x->scrub_rate;
    }

    idx = (long)floor(f);
    amt = f - idx;
    if (idx >= n - 1) {
        prev = next = ll_preset_stored_at(x, n - 1);
        amt = 0;
    } else {
        prev = ll_preset_stored_at(x, idx);
        next = ll_preset_stored_at(x, idx + 1);
    }
    LL_UNLOCK(x);

    if (prev == next) ll_preset_apply(x, 1, prev, 0, 0);
    else ll_preset_apply(x, 3, prev, next, amt);
    ll_preset_notifyclients(x);
}

void ll_preset_mousedown(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    char del = (mod & LL_PRESET_KEY_DELETE) != 0;
    char shift = (mod & eShiftKey) != 0;
    char option = (mod & eAltKey) != 0;
    long c;

    if (mod & LL_PRESET_MENU_MOD) {
        ll_preset_menu(x, view, pt);
        return;
    }

    c = ll_preset_coord_to_square(x, pt.x, pt.y);
    x->click = c;
    x->have_last_drag_x = 0;

    if (c) {
        char stored = ll_preset_is_stored(x, c);

        if (x->hover_index == c && x->hover_remove) {
            ll_preset_delete(x, c);
            return;
        }
        if (!stored || shift || x->hover_add) {
            // add / store
            ll_preset_store(x, c);
            ll_preset_apply(x, 1, c, 0, 0);
            ll_preset_notifyclients(x);
            return;
        }
        if (del) {
            ll_preset_delete(x, c);
            return;
        }
    }

    // option-click: don't change the preset, start an endless-drag scrub
    if (option) {
        ll_preset_begin_scrub(x, view, 1, pt.x);
        return;
    }

    if (c) ll_preset_recall_slot(x, c);
    x->drag_start = ll_preset_current_slot(x);
}

// absolute positions: normal drag, or a scrub latched by pressing option mid-drag
void ll_preset_mousedrag(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    char option = (mod & eAltKey) != 0;
    long d;

    if (x->drag_delta) return;

    if (option && !x->option_scrub_active) {
        ll_preset_begin_scrub(x, view, 0, pt.x);
        return;
    }

    if (x->option_scrub_active && option) {
        double dx;
        if (!x->have_last_drag_x) {
            x->last_drag_x = pt.x;
            x->have_last_drag_x = 1;
        }
        dx = pt.x - x->last_drag_x;
        x->last_drag_x = pt.x;
        ll_preset_scrub_by(x, dx);
        return;
    }

    if (x->option_scrub_active && !option) {
        ll_preset_end_scrub(x);
        return;
    }

    // normal drag: recall stored slots under the mouse
    d = ll_preset_coord_to_square(x, pt.x, pt.y);
    if (d && d != x->click && ll_preset_is_stored(x, d)) {
        x->click = d;
        ll_preset_stop_ramp(x);
        ll_preset_apply(x, 1, d, 0, 0);
        ll_preset_notifyclients(x);
    }
}

// endless drag: pt is the movement since the last call
void ll_preset_mousedragdelta(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    if (!x->drag_delta) return;

    // option released: pause the scrub until it's pressed again or the mouse goes up
    x->option_scrub_active = (mod & eAltKey) != 0;
    if (!x->option_scrub_active) return;

    ll_preset_scrub_by(x, pt.x);
}

void ll_preset_mouseup(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    if (x->drag_delta) {
        x->drag_delta = 0;
        jbox_set_mousedragdelta((t_object *)x, 0);
    }
    ll_preset_end_scrub(x);
    jmouse_setcursor(view, (t_object *)x, (mod & eAltKey) ? JMOUSE_CURSOR_RESIZE_LEFTRIGHT : JMOUSE_CURSOR_ARROW);
}

void ll_preset_mousemove(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    long idx;
    long oi = x->hover_index;
    char oa = x->hover_add, orm = x->hover_remove, orp = x->hover_replace;

    // left-right cursor while option is held: option-drag scrubs
    jmouse_setcursor(view, (t_object *)x, (mod & eAltKey) ? JMOUSE_CURSOR_RESIZE_LEFTRIGHT : JMOUSE_CURSOR_ARROW);

    if (x->menu_open) return;
    idx = ll_preset_coord_to_square(x, pt.x, pt.y);

    if (!idx) {
        x->hover_index = -1;
        x->hover_add = x->hover_remove = x->hover_replace = 0;
    } else {
        char stored = ll_preset_is_stored(x, idx);
        x->hover_index = idx;
        x->hover_remove = (mod & LL_PRESET_KEY_DELETE) != 0;
        x->hover_replace = (mod & eShiftKey) && stored;
        x->hover_add = !x->hover_remove && !x->hover_replace && !stored;
    }

    if (oi != x->hover_index || oa != x->hover_add || orm != x->hover_remove || orp != x->hover_replace)
        ll_preset_redraw(x);
}

void ll_preset_mouseleave(t_ll_preset *x, t_object *view, t_pt pt, long mod)
{
    // a running scrub is ended by mouseup (mouse is captured while dragging)
    if (x->menu_open) return;
    x->hover_index = -1;
    x->hover_add = x->hover_remove = x->hover_replace = 0;
    ll_preset_redraw(x);
}