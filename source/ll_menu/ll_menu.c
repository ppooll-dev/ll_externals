/*
 ll_menu
 by joe steccato 2025

 umenu with extras ("show" message, checksymbol
 */

#include "ext.h"
#include "ext_obex.h"
#include "ext_obex_util.h"
#include "ext_dictobj.h"

#include "jgraphics.h"
#include "jpatcher_api.h"

#define ITEMS_FLAGS (OBEX_UTIL_ATOM_GETTEXT_SYM_NO_QUOTE)

static t_symbol *sym_items = NULL;

typedef enum {
    MANUAL,
    SELECTED,
    TOGGLES
} CheckModes;

typedef struct _ll_menu_item {
    long        ac;         // atom count
    t_atom     *av;         // owned array of atoms (raw)
    char       *label;      // canonical C-string label from atom_gettext()
    char       *full_label; // exact output version "(a)"

    char        is_separator;
    char        is_disabled;
    char        is_selectable;
} t_ll_menu_item;

typedef struct _ll_menu {
    t_jbox  j_box;
    void    *outlet_index, *outlet_symbol, *outlet_status;

    t_linklist *items;      // store the menu items dynamically
    
    void *items_storage;
    
    t_symbol *prepend;      // optional symbol to prepend on output

    char checkmode;         // 0 = manual, 1 = last_selected, 2 = toggles
    char *checked_flags;    // array of char (0/1) per item, length = items_count
    
    long selected_item;     // last menu item the user selected in popup or via int/symbol
    
    long items_count;
    
    char objectclick;
    char position_mode;
    char draw_arrow;
    
    char show_cancel;
    char pattr_stores_sym;
    
    t_jrgba     bgcolor;
    t_jrgba     textcolor;
    t_jrgba     bordercolor;
    double      fontsize;
    double      menufontsize;
    t_jfont    *font;
} t_ll_menu;

t_class *ll_menu_class;

void *ll_menu_new(t_symbol *s, long argc, t_atom *argv);
void ll_menu_free(t_ll_menu *x);
void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void ll_menu_paint(t_ll_menu *x, t_object *view);
void ll_menu_getdrawparams(t_ll_menu *x, t_object *patcherview, t_jboxdrawparams *params);
void ll_menu_mousedown(t_ll_menu *x, t_object *patcherview, t_pt pt, long modifiers);

// messages: common
void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_bang(t_ll_menu *x);
void ll_menu_int(t_ll_menu *x, long index);
void ll_menu_setint(t_ll_menu *x, long index);
void ll_menu_symbol(t_ll_menu *x, t_symbol *s, long ac, t_atom *av);
void ll_menu_setsymbol(t_ll_menu *x, t_symbol *s, long ac, t_atom *av);
void ll_menu_dict(t_ll_menu *x, t_symbol *s);
void ll_menu_dump(t_ll_menu *x);
void ll_menu_show(t_ll_menu *x); // show menu

// messages: modify "items" attr
void ll_menu_clear(t_ll_menu *x);
void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv);
void ll_menu_delete(t_ll_menu *x, long index);

// messages: check items
void ll_menu_checkitem(t_ll_menu *x, long index, long mode);
void ll_menu_checksymbol(t_ll_menu *x, t_symbol *msg, long ac, t_atom *av);
void ll_menu_clearchecks(t_ll_menu *x);

// attr get/set
t_max_err ll_menu_getvalue(t_ll_menu *x, long *ac, t_atom **av);
t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av);

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *argc, t_atom **argv);
t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long argc, t_atom *argv);

t_max_err ll_menu_get_pattrmode(t_ll_menu *x, void *attr, long *ac, t_atom **av);
t_max_err ll_menu_set_pattrmode(t_ll_menu *x, void *attr, long ac, t_atom *av);

// helpers - items
void ll_menu_validate_selected_item(t_ll_menu *x);
long ll_menu_first_selectable_index(t_ll_menu *x);

static t_ll_menu_item *ll_menu_item_new(long ac, t_atom *av);
static void ll_menu_item_free(t_ll_menu_item *item);

static inline long ll_menu_count(t_ll_menu *x) {
    return x->items ? linklist_getsize(x->items) : 0;
}

long ll_menu_find_atoms_index(t_ll_menu *x, long ac, t_atom *av)
{
    if (!x->items || ac < 0 || !av)
        return -1;

    long size = 0;
    char *needle = NULL;

    atom_gettext(ac, av, &size, &needle, ITEMS_FLAGS);

    if (!needle)
        return -1;

    // Trim whitespace
    char *p = needle;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while (len && isspace((unsigned char)p[len - 1])) len--;

    p[len] = 0;  // null-terminate trimmed

    long count = ll_menu_count(x);

    // 1) umenu rule: empty string → first separator
    if (len == 0) {
        for (long i = 0; i < count; i++) {
            t_ll_menu_item *item = linklist_getindex(x->items, i);
            if (item && item->is_separator) {
                sysmem_freeptr(needle);
                return i;
            }
        }
        // no separator → fallback to first selectable
        long idx = ll_menu_first_selectable_index(x);
        sysmem_freeptr(needle);
        return idx;
    }

    // 2) Normal canonical string lookup
    for (long i = 0; i < count; i++) {
        t_ll_menu_item *item = linklist_getindex(x->items, i);
        if (item && item->full_label && strcmp(item->full_label, p) == 0) {
            sysmem_freeptr(needle);
            return i;
        }
    }

    sysmem_freeptr(needle);
    return -1;
}

static void ll_menu_output(t_ll_menu *x, long index, t_ll_menu_item *item)
{
    long ac = item->ac;
    t_atom *av = item->av;

    if (x->prepend && x->prepend != gensym("")) {
        outlet_anything(x->outlet_symbol, x->prepend, ac, av);
    } else {
        outlet_anything(x->outlet_symbol, _sym_list, ac, av);
    }

    outlet_int(x->outlet_index, index);
}


static t_ll_menu_item *ll_menu_get_valid_item(t_ll_menu *x, long index)
{
    long count = ll_menu_count(x);

    if (count <= 0)       return NULL;
    if (index < 0 || index >= count) return NULL;

    t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, index);

    return item ? item : NULL;
}

void ll_menu_realloc_flags(t_ll_menu *x)
{
    long new_count = ll_menu_count(x);

    char *new_flags = NULL;
    if (new_count > 0)
        new_flags = (char*)sysmem_newptrclear(new_count);

    // copy old bits if useful
    if (x->checked_flags && new_flags) {
        long min = MIN(x->items_count, new_count);
        memcpy(new_flags, x->checked_flags, min);
    }

    sysmem_freeptr(x->checked_flags);
    x->checked_flags = new_flags;
    x->items_count = new_count;
}

static void ll_menu_post_items_changed(t_ll_menu *x, void *attr)
{
    ll_menu_realloc_flags(x);
    ll_menu_validate_selected_item(x);
    jbox_redraw((t_jbox *)x);
    object_notify(x, gensym("attr_modified"), attr);
}

void ll_menu_select(t_ll_menu *x, long index, char do_output, char do_notify)
{
    t_ll_menu_item *item = ll_menu_get_valid_item(x, index);
    if (!item)
        return;

    // --- internal selection logic ---
    long count = ll_menu_count(x);
    if (!x->checked_flags || x->items_count != count)
        ll_menu_realloc_flags(x);

    x->selected_item = index;

    switch (x->checkmode) {
        case MANUAL:
            break;

        case SELECTED:
            memset(x->checked_flags, 0, count);
            x->checked_flags[index] = 1;
            break;

        case TOGGLES:
            x->checked_flags[index] ^= 1;
            break;
    }

    // redraw UI
    jbox_redraw((t_jbox *)x);

    // output
    if (do_output)
        ll_menu_output(x, index, item);

    // pattr notify
    if (do_notify)
        object_notify(x, _sym_modified, NULL);
}

void ext_main(void *r)
{
    t_class *c;
    common_symbols_init();
    
    sym_items = gensym("items");
    
    c = class_new("ll_menu",
                  (method)ll_menu_new,
                  (method)ll_menu_free,
                  sizeof(t_ll_menu),
                  (method)NULL,
                  A_GIMME,
                  0L);
    
    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;

    jbox_initclass(c, JBOX_DRAWFIRSTIN | JBOX_FIXWIDTH | JBOX_FONTATTR | JBOX_COLOR);
    
    class_addmethod(c, (method)ll_menu_paint,       "paint", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_notify,      "notify", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_anything,    "anything", A_GIMME, 0);
        
    class_addmethod(c, (method)ll_menu_mousedown,       "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_bang,            "bang", 0);
    class_addmethod(c, (method)ll_menu_getdrawparams,   "getdrawparams", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_getvalue,        "getvalueof", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_setvalue,        "setvalueof", A_CANT, 0);
    
    class_addmethod(c, (method)ll_menu_clear,  "clear", 0);
    class_addmethod(c, (method)ll_menu_append, "append", A_GIMME, 0);
    class_addmethod(c, (method)ll_menu_insert, "insert", A_LONG, A_GIMME, 0);
    class_addmethod(c, (method)ll_menu_delete, "delete", A_LONG, 0);
    
    class_addmethod(c, (method)ll_menu_int,        "int",        A_LONG, 0);
    class_addmethod(c, (method)ll_menu_setint,     "set",        A_LONG, 0);
    class_addmethod(c, (method)ll_menu_symbol,     "symbol",     A_GIMME,  0);
    class_addmethod(c, (method)ll_menu_setsymbol,  "setsymbol",  A_GIMME,  0);
    class_addmethod(c, (method)ll_menu_dict,       "dictionary", A_SYM, 0);
    
    class_addmethod(c, (method)ll_menu_checkitem,   "checkitem",   A_LONG, A_LONG, 0);
    class_addmethod(c, (method)ll_menu_clearchecks, "clearchecks", 0);
    class_addmethod(c, (method)ll_menu_checksymbol, "checksymbol", A_GIMME, 0);
    
    // ll_menu
    class_addmethod(c, (method)ll_menu_show, "show", 0);
    class_addmethod(c, (method)ll_menu_dump, "dump", 0);
    
    // APPEARANCE
    CLASS_STICKY_ATTR(c, "category", 0, "Appearance");
    
    CLASS_ATTR_CHAR(c, "arrow", 0, t_ll_menu, draw_arrow);
    CLASS_ATTR_STYLE_LABEL(c, "arrow", 0, "onoff", "Draw Arrow");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "arrow", 0, "1");
    
    CLASS_ATTR_DEFAULT(c, "patching_rect", 0, "0. 0. 100. 24.");

    CLASS_STICKY_ATTR_CLEAR(c, "category");
    
    // COLOR ATTRIBUTES
    CLASS_STICKY_ATTR(c, "category", 0, "Color");

    CLASS_ATTR_RGBA_LEGACY(c, "bgcolor", "brgb", 0, t_ll_menu, bgcolor);
    CLASS_ATTR_ALIAS(c, "bgcolor", "brgba");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "bgcolor", 0, ".23 .23 .23 1.");
    CLASS_ATTR_STYLE_LABEL(c, "bgcolor", 0, "rgba", "Background Color");

    CLASS_ATTR_RGBA(c, "color", 0, t_ll_menu, textcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "color", 0, "1. 1. 1. 1.");
    CLASS_ATTR_STYLE_LABEL(c, "color", 0, "rgba", "Color");
    CLASS_ATTR_PAINT(c, "color", 0);
    
    CLASS_ATTR_RGBA_LEGACY(c, "bordercolor", "border", 0, t_ll_menu, bordercolor);
    CLASS_ATTR_ALIAS(c, "bordercolor", "borderrgba");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "bordercolor", 0, "0. 0. 0. 0.");
    CLASS_ATTR_STYLE_LABEL(c, "bordercolor", 0, "rgba", "Border Color");
    
    CLASS_STICKY_ATTR_CLEAR(c, "category");

    // font attributes
    CLASS_STICKY_ATTR(c, "category", 0, "Font");

    CLASS_ATTR_DOUBLE(c, "fontsize", 0, t_ll_menu, fontsize);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "fontsize", 0, "11.");
    CLASS_ATTR_STYLE_LABEL(c, "fontsize", 0, "number", "Font Size");
    
    CLASS_ATTR_DOUBLE(c, "menufontsize", 0, t_ll_menu, menufontsize);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "menufontsize", 0, "11.");
    CLASS_ATTR_STYLE_LABEL(c, "menufontsize", 0, "number", "Menu Font Size");

    CLASS_STICKY_ATTR_CLEAR(c, "category");

    // behavior attributes
    CLASS_STICKY_ATTR(c, "category", 0, "Behavior");

    CLASS_ATTR_ATOM(c, "items", ATTR_GET_OPAQUE | ATTR_SET_OPAQUE, t_ll_menu, items_storage);
    CLASS_ATTR_STYLE(c, "items", 0, "text_large");
    CLASS_ATTR_ACCESSORS(c, "items", ll_menu_getitems, ll_menu_setitems);
    CLASS_ATTR_LABEL(c, "items", 0, "Menu Items");
    CLASS_ATTR_SAVE(c, "items", 0);
    CLASS_ATTR_BASIC(c, "items", 0);
    
    CLASS_ATTR_SYM(c, "prefix", 0, t_ll_menu, prepend);
    CLASS_ATTR_LABEL(c, "prefix", 0, "Prefix");
    CLASS_ATTR_SAVE(c, "prefix", 0);
    
    CLASS_ATTR_CHAR(c,                "positionmode", 0, t_ll_menu, position_mode);
    CLASS_ATTR_STYLE_LABEL(c,        "positionmode", 0, "enum", "Menu Position");
    CLASS_ATTR_ENUMINDEX(c,            "positionmode", 0, "Object Mouse");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c,"positionmode", 0, "0");
    
    // Output -1, <cancel> messages when user cancels menu selection.
    CLASS_ATTR_CHAR(c, "outputcancel", 0, t_ll_menu, show_cancel);
    CLASS_ATTR_STYLE_LABEL(c, "outputcancel", 0, "onoff", "Output message on cancel");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "outputcancel", 0, "0");
    
    CLASS_ATTR_CHAR(c, "pattrmode", 0, t_ll_menu, pattr_stores_sym);
    CLASS_ATTR_ACCESSORS(c, "pattrmode",
                         ll_menu_get_pattrmode,
                         ll_menu_set_pattrmode);
    CLASS_ATTR_STYLE_LABEL(c, "pattrmode", 0, "onoff", "Pattr Stores Symbol");
    CLASS_ATTR_DEFAULT_SAVE(c, "pattrmode", 0, "0");
    
    CLASS_ATTR_CHAR(c, "checkmode", 0, t_ll_menu, checkmode);
    CLASS_ATTR_STYLE_LABEL(c, "checkmode", 0, "enum", "Check Mode");
    CLASS_ATTR_ENUMINDEX(c, "checkmode", 0, "Manual Selected Toggles");
    CLASS_ATTR_DEFAULT_SAVE(c, "checkmode", 0, "0"); // default last_selected?
    
    CLASS_STICKY_ATTR_CLEAR(c, "category");
    
    class_register(CLASS_BOX, c);
    ll_menu_class = c;
}

void *ll_menu_new(t_symbol *s, long argc, t_atom *argv)
{
    t_dictionary *d = object_dictionaryarg(argc, argv);
    if (!d)
        return NULL;

    t_ll_menu *x = (t_ll_menu *)object_alloc(ll_menu_class);
    if (!x)
        return NULL;

    long flags =
          JBOX_DRAWFIRSTIN
        | JBOX_DRAWBACKGROUND
        | JBOX_NODRAWBOX
        | JBOX_DRAWINLAST
        | JBOX_TRANSPARENT
        | JBOX_GROWBOTH
        | JBOX_FIXWIDTH;

    jbox_new(&x->j_box, flags, argc, argv);
    x->j_box.b_firstin = (t_object *)x;

    // --- internal state ---
    x->items        = NULL;     // linklist of t_ll_menu_item*, created lazily
    x->items_count  = 0;

    x->prepend      = gensym("");
    x->objectclick  = 0;
    x->position_mode = 0;
    x->draw_arrow   = 1;        // will be overridden by "arrow" attr if set

    x->show_cancel      = 0;
    x->pattr_stores_sym = 0;

    x->selected_item = 0;
    x->checkmode     = MANUAL;  // enum { MANUAL, SELECTED, TOGGLES }
    x->checked_flags = NULL;    // allocated when items exist

    x->font = NULL;

    // --- outlets (right-to-left in Max UI, but created here in logical order) ---
    x->outlet_status = outlet_new((t_object *)x, NULL); // right: dump / status
    x->outlet_symbol = outlet_new((t_object *)x, NULL); // middle: symbol/list
    x->outlet_index  = outlet_new((t_object *)x, NULL); // left: index

    // apply attributes from the dictionary (items, colors, etc.)
    attr_dictionary_process(x, d);

    jbox_ready(&x->j_box);
    return x;
}

void ll_menu_free(t_ll_menu *x)
{
    if (x->font)
        jfont_destroy(x->font);

    // free items list AND each t_ll_menu_item
    if (x->items) {
        linklist_funall(x->items, (method)ll_menu_item_free, NULL);
        object_free(x->items);
        x->items = NULL;
    }

    if (x->checked_flags) {
        sysmem_freeptr(x->checked_flags);
        x->checked_flags = NULL;
    }

    jbox_free((t_jbox *)x);
}

void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (msg == gensym("attr_modified") && sender == (void *)x)
        jbox_redraw((t_jbox *)x);
}

void ll_menu_paint(t_ll_menu *x, t_object *view)
{
    t_jgraphics *g = (t_jgraphics *)patcherview_get_jgraphics(view);
    t_rect rect;
    jbox_get_rect_for_view((t_object *)x, view, &rect);

    // COLORS
    t_jrgba bg, txt, border;
    object_attr_getjrgba((t_object *)x, _sym_bgcolor, &bg);
    object_attr_getjrgba((t_object *)x, _sym_color, &txt);
    object_attr_getjrgba((t_object *)x, gensym("bordercolor"), &border);

    // BACKGROUND
    jgraphics_set_source_jrgba(g, &bg);
    jgraphics_rectangle_fill_fast(g, 0, 0, rect.width, rect.height);

    // FONT
    if (x->font)
        jfont_destroy(x->font);

    t_symbol *fname = jbox_get_fontname((t_object *)x);
    double fsize = (x->fontsize > 0 ? x->fontsize : jbox_get_fontsize((t_object *)x));
    if (fsize <= 0)
        fsize = 12.0;

    x->font = jfont_create(
        (fname && fname != gensym("")) ? fname->s_name : "Arial",
        (t_jgraphics_font_slant) jbox_get_font_slant((t_object *)x),
        (t_jgraphics_font_weight) jbox_get_font_weight((t_object *)x),
        fsize
    );

    // DETERMINE DISPLAY LABEL
    const char *label = "";
    char isSeparator = 0;
    long count = ll_menu_count(x);

    // 1. Try selected item
    if (x->selected_item >= 0 && x->selected_item < count) {
        t_ll_menu_item *sel = (t_ll_menu_item *)linklist_getindex(x->items, x->selected_item);
        if (sel) {
            isSeparator = sel->is_separator;
            if (sel->label && !isSeparator)
                label = sel->label;
        }
    }

    // 2. Fallback: first selectable item
    if (label[0] == '\0' && !isSeparator) {
        for (long i = 0; i < count; i++) {
            t_ll_menu_item *it = (t_ll_menu_item *)linklist_getindex(x->items, i);
            if (it && it->label) {
                label = it->label;
                break;
            }
        }
    }

    // DRAW TEXT
    double margin    = 4.0;
    double arrow_pad = 10.0;
    double text_w    = rect.width - (margin * 2 + arrow_pad);
    if (text_w < 1)
        text_w = 1;

    t_jtextlayout *layout = jtextlayout_create();
    jtextlayout_set(
        layout,
        label,
        x->font,
        margin,
        0,
        text_w,
        rect.height,
        JGRAPHICS_TEXT_JUSTIFICATION_LEFT |
        JGRAPHICS_TEXT_JUSTIFICATION_VCENTERED,
        JGRAPHICS_TEXTLAYOUT_NOWRAP
    );

    jtextlayout_settextcolor(layout, &txt);
    jtextlayout_draw(layout, g);
    jtextlayout_destroy(layout);

    // ARROW
    if (x->draw_arrow) {
        jgraphics_set_source_jrgba(g, &txt);
        jgraphics_move_to(g, rect.width - 10,  rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 5,   rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 7.5, rect.height / 2 + 2);
        jgraphics_close_path(g);
        jgraphics_fill(g);
    }

    // BORDER
    jgraphics_set_source_jrgba(g, &border);
    jgraphics_rectangle(g, 0.5, 0.5, rect.width - 1, rect.height - 1);
    jgraphics_stroke(g);
}

void ll_menu_getdrawparams(t_ll_menu *x, t_object *patcherview, t_jboxdrawparams *params)
{
    params->d_borderthickness = 0;
    params->d_bordercolor     = x->bordercolor;
    params->d_boxfillcolor    = x->bgcolor;
}

void ll_menu_mousedown(t_ll_menu *x, t_object *patcherview, t_pt pt, long modifiers)
{
    x->objectclick = 1;
    ll_menu_show(x);
}

//
// messages: generic
//

void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
//    post("ANYTHING: %s", s->s_name);
}

void ll_menu_bang(t_ll_menu *x)
{
    t_ll_menu_item *item = ll_menu_get_valid_item(x, x->selected_item);
    if (!item)
        return;

    ll_menu_select(x, x->selected_item, 1, 0);
}

//
// Set selected item from messages
//
void ll_menu_int(t_ll_menu *x, long index)
{
    ll_menu_select(x, index, 1, 1);
}

void ll_menu_setint(t_ll_menu *x, long index)
{
    ll_menu_select(x, index, 0, 1);
}

void ll_menu_symbol(t_ll_menu *x, t_symbol *msg, long ac, t_atom *av)
{
    long index = ll_menu_find_atoms_index(x, ac, av);
    if (index >= 0)
        ll_menu_select(x, index, 1, 1);
}

void ll_menu_setsymbol(t_ll_menu *x, t_symbol *msg, long ac, t_atom *av)
{
    long index = ll_menu_find_atoms_index(x, ac, av);
    if (index >= 0)
        ll_menu_select(x, index, 0, 1);
}

void ll_menu_dict(t_ll_menu *x, t_symbol *s)
{
    t_dictionary *d = dictobj_findregistered_retain(s);
    if (!d)
        return;

    t_object *obj = NULL;
    if (dictionary_getatomarray(d, sym_items, &obj)) {
        dictobj_release(d);
        return;
    }

    long ac = 0;
    t_atom *av = NULL;
    atomarray_getatoms((t_atomarray*)obj, &ac, &av);

    // CLEAR
    if (!x->items) {
        x->items = linklist_new();
        linklist_flags(x->items, OBJ_FLAG_DATA);
    } else {
        linklist_clear(x->items);
    }

    // BUILD ITEMS (each is a single symbol -> 1 atom)
    for (long i = 0; i < ac; i++) {
        t_ll_menu_item *item = ll_menu_item_new(1, av + i);
        linklist_append(x->items, item);
    }

    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
    dictobj_release(d);
}

// Dump menu items
void ll_menu_dump(t_ll_menu *x)
{
    if (!x->items)
        return;

    long count = ll_menu_count(x);

    for (long i = 0; i < count; i++) {

        t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, i);
        if (!item)
            continue;

        long ac = item->ac;
        t_atom *av = item->av;

        // Allocate output: [index, <item atoms...>]
        long outc = ac + 1;
        t_atom *outv = (t_atom *)sysmem_newptr(outc * sizeof(t_atom));
        if (!outv)
            return;

        // First atom = index
        atom_setlong(outv, i);

        // Copy item atom list
        if (ac > 0 && av)
            sysmem_copyptr(av, outv + 1, ac * sizeof(t_atom));

        // Output:
        //   dump index <atoms...>
        outlet_anything(x->outlet_status, gensym("dump"), outc, outv);

        sysmem_freeptr(outv);
    }
}

// Show popup menu on bang
void ll_menu_show(t_ll_menu *x)
{
    long count = ll_menu_count(x);

    if (count <= 0)
        return;

    // Build popup menu
    t_jpopupmenu *menu = jpopupmenu_create();

    // colors
    t_jrgba text, bg;
    object_attr_getjrgba((t_object *)x, _sym_color,   &text);
    object_attr_getjrgba((t_object *)x, _sym_bgcolor, &bg);

    t_jrgba hilitebg = bg;
    hilitebg.red   = MIN(1.0, bg.red   + 0.15);
    hilitebg.green = MIN(1.0, bg.green + 0.15);
    hilitebg.blue  = MIN(1.0, bg.blue  + 0.15);

    jpopupmenu_setcolors(menu, text, bg, text, hilitebg);

    // font
    double msize = (x->menufontsize > 0 ? x->menufontsize : x->fontsize);
    if (msize <= 0) msize = 12;

    t_symbol *fname = jbox_get_fontname((t_object *)x);
    t_jfont *menufont = jfont_create(
        (fname && fname != gensym("") ? fname->s_name : "Arial"),
        (t_jgraphics_font_slant) jbox_get_font_slant((t_object *)x),
        (t_jgraphics_font_weight) jbox_get_font_weight((t_object *)x),
        msize
    );
    jpopupmenu_setfont(menu, menufont);
    jfont_destroy(menufont);

    // Add menu items
    for (long i = 0; i < count; i++) {

        t_ll_menu_item *item =
            (t_ll_menu_item *)linklist_getindex(x->items, i);
        if (!item)
            continue;

        // Separator
        if (item->is_separator) {
            jpopupmenu_addseparator(menu);
            continue;
        }

        // Determine checkmark
        int checked = 0;
        if (x->checkmode == SELECTED) {
            checked = (i == x->selected_item);
        } else {
            if (x->checked_flags && x->checked_flags[i])
                checked = 1;
        }

        // Dim disabled items
        t_jrgba itemtext = text;
        if (item->is_disabled)
            itemtext.alpha *= 0.6;

        jpopupmenu_additem(
            menu,
            (int)(i + 1),           // menu API uses 1-based index
            item->label,    // display label
            &itemtext,
            checked,
            item->is_disabled,
            NULL
        );
    }

    // Compute popup position
    t_pt pt;
    if (x->objectclick == 0 && x->position_mode == 1) {

        // Mouse position
        int mx, my;
        jmouse_getposition_global(&mx, &my);
        pt.x = (double)mx;
        pt.y = (double)my;

    } else {

        // Under the object box
        t_object *patcher = NULL;
        object_obex_lookup((t_object *)x, gensym("#P"), &patcher);
        if (!patcher) {
            jpopupmenu_destroy(menu);
            return;
        }

        t_object *view = jpatcher_get_firstview(patcher);
        if (!view) {
            jpopupmenu_destroy(menu);
            return;
        }

        t_rect rect;
        jbox_get_rect_for_view((t_object *)x, view, &rect);

        long sx, sy;
        patcherview_canvas_to_screen(
            view,
            rect.x,
            rect.y + rect.height - 1,
            &sx, &sy
        );

        pt.x = (double)sx;
        pt.y = (double)sy;
    }

    // SHOW MENU
    int choice = jpopupmenu_popup(menu, pt, 0);

    if (choice > 0 && choice <= count) {
        ll_menu_select(x, choice - 1, 1, 1);
    }
    else if (x->show_cancel) {
        t_atom a;
        atom_setsym(&a, gensym("<cancel>"));

        if (x->prepend && x->prepend != gensym("")) {
            // prepend <prepend> <cancel>
            outlet_anything(x->outlet_symbol, x->prepend, 1, &a);
        } else {
            // output: list <cancel>
            outlet_anything(x->outlet_symbol, _sym_list, 1, &a);
        }

        outlet_int(x->outlet_index, -1);
    }

    x->objectclick = 0;
    jpopupmenu_destroy(menu);
}

//
// item messages
//
void ll_menu_clear(t_ll_menu *x)
{
    if (x->items)
        linklist_clear(x->items);

    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
}

void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    if (!x->items) {
        x->items = linklist_new();
        linklist_flags(x->items, OBJ_FLAG_DATA);
    }

    // Build new item
    t_ll_menu_item *item = ll_menu_item_new(argc, argv);
    if (!item)
        return;

    linklist_append(x->items, item);

    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
}

void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv)
{
    if (!x->items)
        return;

    long size = linklist_getsize(x->items);
    if (index < 0) index = 0;
    if (index > size) index = size;

    t_ll_menu_item *item = ll_menu_item_new(argc, argv);
    if (!item)
        return;

    linklist_insertindex(x->items, item, index);

    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
}

void ll_menu_delete(t_ll_menu *x, long index)
{
    if (!x->items)
        return;

    t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, index);
    if (item) {
        ll_menu_item_free(item);
    }

    linklist_deleteindex(x->items, index);

    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
}

//
// check messages
//
static void ll_menu_apply_check(
    t_ll_menu *x,
    long index,
    long mode,             // 0 = off, 1 = on, -1 = toggle, -999 = force clear
    char allow_selected    // 1 = obey SELECTED mode, 0 = just update array
) {
    long count = x->items_count;

    if (!x->checked_flags || count <= 0)
        return;

    // Special case: full clear
    if (mode == -999) {
        memset(x->checked_flags, 0, count);

        if (allow_selected && x->checkmode == SELECTED) {
            long idx = ll_menu_first_selectable_index(x);
            x->selected_item = (idx >= 0 ? idx : 0);
        }

        return;
    }

    // Regular check of a single index
    if (index < 0 || index >= count)
        return;

    char current = x->checked_flags[index];
    char newstate = current;

    if      (mode == 0)  newstate = 0;
    else if (mode == 1)  newstate = 1;
    else if (mode == -1) newstate = !current;

    // SELECTED = exclusive
    if (allow_selected && x->checkmode == SELECTED) {
        memset(x->checked_flags, 0, count);
        x->checked_flags[index] = newstate;
        x->selected_item = index;
    }
    else {
        x->checked_flags[index] = newstate;
    }
}

void ll_menu_clearchecks(t_ll_menu *x)
{
    ll_menu_apply_check(x, -1, -999, 1);
}

void ll_menu_checkitem(t_ll_menu *x, long index, long mode)
{
    ll_menu_apply_check(x, index, mode, 1);
}

void ll_menu_checksymbol(t_ll_menu *x, t_symbol *msg, long ac, t_atom *av)
{
    if (ac < 2)
        return;

    long mode = atom_getlong(av + (ac - 1));
    long key_ac = ac - 1;

    long idx = ll_menu_find_atoms_index(x, key_ac, av);
    if (idx < 0)
        return;

    ll_menu_apply_check(x, idx, mode, 1);
}

t_max_err ll_menu_getvalue(t_ll_menu *x, long *ac, t_atom **av)
{
    if (!ac || !av)
        return MAX_ERR_NONE;

    char alloc = 0;
    if (atom_alloc_array(1, ac, av, &alloc))
        return MAX_ERR_OUT_OF_MEM;

    long count = ll_menu_count(x);

    if (x->pattr_stores_sym == 0) {
        long idx = (count == 0 ? 0 : x->selected_item);
        atom_setlong(*av, idx);
        return MAX_ERR_NONE;
    }

    if (x->selected_item >= 0 && x->selected_item < count) {

        t_ll_menu_item *item =
            (t_ll_menu_item *)linklist_getindex(x->items, x->selected_item);

        if (!item) {
            atom_setsym(*av, gensym(""));
            return MAX_ERR_NONE;
        }

        if (item->is_separator)
            atom_setsym(*av, gensym(""));
        else
            atom_setsym(*av, gensym(item->full_label));
    }
    else {
        atom_setsym(*av, gensym(""));
    }
    return MAX_ERR_NONE;
}

t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av)
{
    long count = ll_menu_count(x);
    if (count == 0)
        return MAX_ERR_NONE;

    // (1) restore by index
    if (!x->pattr_stores_sym && ac == 1 && atom_gettype(av) == A_LONG) {
        ll_menu_select(x, atom_getlong(av), 1, 0);
        return MAX_ERR_NONE;
    }

    // (2) canonical string match
    long idx = ll_menu_find_atoms_index(x, ac, av);
    if (idx >= 0) {
        ll_menu_select(x, idx, 1, 0);
        return MAX_ERR_NONE;
    }

    // (3) fallback
    long first = ll_menu_first_selectable_index(x);
    if (first >= 0){
        ll_menu_select(x, first, 1, 0);
    }

    return MAX_ERR_NONE;
}

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *dest_ac, t_atom **dest_av)
{
    long count = ll_menu_count(x);

    // NO ITEMS → "<empty>"
    if (count == 0) {
        *dest_ac = 1;
        *dest_av = (t_atom *)sysmem_newptr(sizeof(t_atom));
        atom_setsym(*dest_av, gensym("<empty>"));
        return MAX_ERR_NONE;
    }

    // 1) First pass: determine total atoms required
    long total = 0;

    for (long i = 0; i < count; i++) {
        t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, i);
        if (!item)
            continue;

        total += item->ac;

        if (i < count - 1)
            total += 1; // comma between entries
    }

    // 2) Allocate final storage
    *dest_ac = total;
    *dest_av = (t_atom *)sysmem_newptr(total * sizeof(t_atom));

    long out_i = 0;

    // 3) Build final list: [item0 atoms] , [item1 atoms] , ...
    for (long i = 0; i < count; i++) {
        t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, i);
        if (!item)
            continue;

        // copy item atoms
        if (item->ac > 0 && item->av) {
            sysmem_copyptr(
                item->av,
                *dest_av + out_i,
                item->ac * sizeof(t_atom)
            );
            out_i += item->ac;
        }

        // add comma unless last
        if (i < count - 1) {
            A_SETCOMMA(&(*dest_av)[out_i]);
            out_i++;
        }
    }

    return MAX_ERR_NONE;
}

t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long ac, t_atom *av)
{
    if (!x)
        return MAX_ERR_GENERIC;

    // 1) "<empty>" → clear all items
    if (ac == 1 && atom_gettype(av) == A_SYM &&
        atom_getsym(av) == gensym("<empty>"))
    {
        if (x->items) {
            // free all items
            linklist_funall(x->items, (method)ll_menu_item_free, NULL);
            object_free(x->items);
            x->items = NULL;
        }

        ll_menu_realloc_flags(x);
        ll_menu_validate_selected_item(x);
        jbox_redraw((t_jbox *)x);

        object_notify(x, gensym("attr_modified"), attr);
        return MAX_ERR_NONE;
    }

    // 2) Reset list, free old items
    if (x->items) {
        linklist_funall(x->items, (method)ll_menu_item_free, NULL);
        object_free(x->items);
        x->items = NULL;
    }

    x->items = linklist_new();
    linklist_flags(x->items, OBJ_FLAG_DATA); // raw C data, we free manually

    // 3) Parse entries separated by A_COMMA
    long start = 0;

    while (start < ac) {
        long end = start;
        // find next comma or end
        while (end < ac && atom_gettype(&av[end]) != A_COMMA)
            end++;

        long entry_count = end - start;

        if (entry_count > 0) {
            t_ll_menu_item *item = ll_menu_item_new(entry_count, &av[start]);
            if (item)
                linklist_append(x->items, item);
        }

        // skip comma
        start = end + 1;
    }

    // 4) Rebuild flags + selection, redraw, notify
    ll_menu_post_items_changed(x, object_attr_get((t_object *)x, sym_items));
    return MAX_ERR_NONE;
}

t_max_err ll_menu_get_pattrmode(t_ll_menu *x, void *attr, long *ac, t_atom **av)
{
    char alloc;
    if (atom_alloc_array(1, ac, av, &alloc))
        return MAX_ERR_OUT_OF_MEM;

    atom_setlong(*av, x->pattr_stores_sym);
    return MAX_ERR_NONE;
}

t_max_err ll_menu_set_pattrmode(t_ll_menu *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        x->pattr_stores_sym = atom_getlong(av);

        // Notify pattrstorage that value changed
        object_notify(x, gensym("modified"), NULL);

        jbox_redraw((t_jbox *)x);
    }
    return MAX_ERR_NONE;
}

long ll_menu_first_selectable_index(t_ll_menu *x)
{
    if (!x->items)
        return -1;

    long count = ll_menu_count(x);

    for (long i = 0; i < count; i++) {
        t_ll_menu_item *item = (t_ll_menu_item *)linklist_getindex(x->items, i);
        if (item)
            return i;
    }

    return -1;
}

void ll_menu_validate_selected_item(t_ll_menu *x)
{
    long count = ll_menu_count(x);

    // keep items_count in sync
    x->items_count = count;

    // no items → nothing to validate
    if (count == 0) {
        x->selected_item = 0;
        return;
    }

    // ff selected_item is out of range, find first selectable
    if (x->selected_item < 0 || x->selected_item >= count) {
        long first = ll_menu_first_selectable_index(x);
        x->selected_item = (first >= 0 ? first : 0);
        return;
    }

    // if current selection is not selectable, fall back to first selectable
    t_ll_menu_item *item =
        (t_ll_menu_item *)linklist_getindex(x->items, x->selected_item);

    if (!item) {
        long first = ll_menu_first_selectable_index(x);
        x->selected_item = (first >= 0 ? first : 0);
    }
}

t_ll_menu_item *ll_menu_item_new(long ac, t_atom *av)
{
    t_ll_menu_item *item =
        (t_ll_menu_item *)sysmem_newptrclear(sizeof(t_ll_menu_item));

    if (!item)
        return NULL;

    item->ac = ac;
    item->av = NULL;
    item->label = NULL;

    // copy atoms
    if (ac > 0) {
        item->av = (t_atom *)sysmem_newptr(ac * sizeof(t_atom));
        if (!item->av) {
            sysmem_freeptr(item);
            return NULL;
        }
        for (long i = 0; i < ac; i++) {
            t_atom *src = &av[i];
            t_atom *dst = &item->av[i];

            switch (atom_gettype(src)) {
                case A_LONG:
                    atom_setlong(dst, atom_getlong(src));
                    break;

                case A_FLOAT:
                    atom_setfloat(dst, atom_getfloat(src));
                    break;

                case A_SYM:
                    atom_setsym(dst, atom_getsym(src));
                    break;

                case A_OBJ: {
                    // dictionary JSON strings come in as A_OBJ
                    char *text = NULL;
                    long textsize = 0;

                    // Convert the dict object to a raw string
                    if (atom_gettext(1, src, &textsize, &text,
                                     OBEX_UTIL_ATOM_GETTEXT_SYM_NO_QUOTE) == MAX_ERR_NONE) {
                        atom_setsym(dst, gensym(text));
                    } else {
                        atom_setsym(dst, gensym(""));
                    }

                    if (text)
                        sysmem_freeptr(text);
                } break;
            }
        }
    }

    // convert atoms → canonical text
    long txtsize = 0;
    char *txt = NULL;

    atom_gettext(ac, item->av, &txtsize, &txt,
                 ITEMS_FLAGS);

    if (!txt) {
        item->label = sysmem_newptrclear(1); // empty string
        item->label[0] = 0;

        item->is_separator  = 0;
        item->is_disabled   = 0;
        item->is_selectable = 0;
        return item;
    }

    // trim whitespace
    char *p = txt;
    while (*p && isspace((unsigned char)*p)) p++;

    long len = strlen(p);
    while (len && isspace((unsigned char)p[len - 1])) len--;
    p[len] = 0;

    // detect special patterns
    item->is_separator = (!strcmp(p, "-") || !strcmp(p, "<separator>"));
    item->is_disabled = (len >= 2 && p[0] == '(' && p[len - 1] == ')');
    item->is_selectable = !item->is_disabled;

    item->full_label = NULL;

    if (item->is_disabled) {
        // Remove outer parentheses FIRST
        p[len - 1] = 0;          // drop trailing ')'
        char *inner = p + 1;     // skip '('
        while (*inner == ' ')    // skip whitespace
            inner++;

        // Full output version "(a)"
        item->full_label = (char *)sysmem_newptr(strlen(inner) + 3);
        sprintf(item->full_label, "(%s)", inner);

        // Canonical label = "a"
        item->label = (char *)sysmem_newptr(strlen(inner) + 1);
        strcpy(item->label, inner);

        sysmem_freeptr(txt);
        return item;
    }
    
    // not disabled → full_label = same as canonical text
    item->full_label = (char *)sysmem_newptr(strlen(p) + 1);
    strcpy(item->full_label, p);

    // Allocate + copy canonical label (this was missing!)
    item->label = (char *)sysmem_newptr(strlen(p) + 1);
    strcpy(item->label, p);

    sysmem_freeptr(txt);
    return item;
}

void ll_menu_item_free(t_ll_menu_item *item)
{
    if (!item) return;
    sysmem_freeptr(item->av);
    sysmem_freeptr(item->label);
    sysmem_freeptr(item->full_label);
    sysmem_freeptr(item);
}
