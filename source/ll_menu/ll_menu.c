#include "ext.h"
#include "ext_obex.h"
#include "ext_obex_util.h"
#include "ext_dictobj.h"

#include "jgraphics.h"
#include "jpatcher_api.h"

#define ITEMS_FLAGS (OBEX_UTIL_ATOM_GETTEXT_SYM_NO_QUOTE)

typedef enum {
    MANUAL,
    SELECTED,
    TOGGLES
} CheckModes;

typedef struct _ll_menu_item {
    t_atomarray *atoms; // one menu entry = list of atoms
} t_ll_menu_item;

typedef struct _ll_menu {
    t_jbox  j_box;
    void    *outlet_index, *outlet_symbol, *outlet_status;

    t_linklist *items;   // store the menu items dynamically
    t_symbol *prepend;   // optional symbol to prepend on output

    char checkmode;   // 0 = manual, 1 = last_selected, 2 = toggles
    char *checked_flags;   // array of char (0/1) per item, length = items_count
    
    long selected_item;   // last menu item the user selected in popup or via int/symbol
    
    char items_text[2048];
    long items_text_len;
    
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
void ll_menu_bang(t_ll_menu *x);
void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

void ll_menu_paint(t_ll_menu *x, t_object *view);
void ll_menu_getdrawparams(t_ll_menu *x, t_object *patcherview, t_jboxdrawparams *params);

void ll_menu_mousedown(t_ll_menu *x, t_object *patcherview, t_pt pt, long modifiers);

t_max_err ll_menu_getvalue(t_ll_menu *x, long *ac, t_atom **av);
t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av);

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *argc, t_atom **argv);
t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long argc, t_atom *argv);

// messages: generic
void ll_menu_int(t_ll_menu *x, long index);
void ll_menu_setint(t_ll_menu *x, long index);
void ll_menu_symbol(t_ll_menu *x, t_symbol *s);
void ll_menu_setsymbol(t_ll_menu *x, t_symbol *s);
void ll_menu_dict(t_ll_menu *x, t_symbol *s);
void ll_menu_dump(t_ll_menu *x);

// modify "items" attr
//  messages
void ll_menu_clear(t_ll_menu *x);
void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv);
void ll_menu_delete(t_ll_menu *x, long index);
//  helpers
void ll_menu_rebuild_items_from_text(t_ll_menu *x);
void ll_menu_update_items_text(t_ll_menu *x);

// check items
void ll_menu_resize_checked_flags(t_ll_menu *x, long new_count);
void ll_menu_checkitem(t_ll_menu *x, long index, long mode);
void ll_menu_clearchecks(t_ll_menu *x);

t_symbol *ll_menu_get_item_symbol(t_atomarray *it);
void ll_menu_validate_selected_item(t_ll_menu *x);
void ll_menu_set_selected_internal(t_ll_menu *x, long index);
void ll_menu_set_selected_and_output(t_ll_menu *x, long index);

char ll_menu_item_is_selectable(t_atomarray *it);
t_symbol *ll_menu_first_selectable_symbol(t_ll_menu *x);
long ll_menu_first_selectable_index(t_ll_menu *x);

void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);

// ll_menu only ?
void ll_menu_items(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_show(t_ll_menu *x);
void ll_menu_checksymbol(t_ll_menu *x, t_symbol *s, long mode);

void ext_main(void *r)
{
    t_class *c;
    common_symbols_init();
    c = class_new("ll_menu",
                  (method)ll_menu_new,
                  (method)ll_menu_free,
                  sizeof(t_ll_menu),
                  (method)NULL,
                  A_GIMME,
                  0L);
    
    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;

    jbox_initclass(c, JBOX_DRAWFIRSTIN | JBOX_FIXWIDTH | JBOX_FONTATTR | JBOX_COLOR);
    
    class_addmethod(c, (method)ll_menu_paint, "paint", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_notify, "notify", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_anything, "anything", A_GIMME, 0);
    
    class_addmethod(c, (method)ll_menu_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_bang, "bang", 0);
    class_addmethod(c, (method)ll_menu_checksymbol, "checksymbol", A_SYM, 0);
    class_addmethod(c, (method)ll_menu_getdrawparams, "getdrawparams", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_getvalue, "getvalueof", A_CANT, 0);
    class_addmethod(c, (method)ll_menu_setvalue, "setvalueof", A_CANT, 0);
    
    class_addmethod(c, (method)ll_menu_clear, "clear", 0);
    class_addmethod(c, (method)ll_menu_append, "append", A_GIMME, 0);
    class_addmethod(c, (method)ll_menu_insert, "insert", A_LONG, A_GIMME, 0);
    class_addmethod(c, (method)ll_menu_delete, "delete", A_LONG, 0);
    
    class_addmethod(c, (method)ll_menu_int,        "int",        A_LONG, 0);
    class_addmethod(c, (method)ll_menu_setint,     "set",        A_LONG, 0);
    class_addmethod(c, (method)ll_menu_symbol,     "symbol",     A_SYM,  0);
    class_addmethod(c, (method)ll_menu_setsymbol,  "setsymbol",  A_SYM,  0);
    class_addmethod(c, (method)ll_menu_dict, "dictionary", A_SYM, 0);
    
    class_addmethod(c, (method)ll_menu_checkitem,   "checkitem",   A_LONG, A_LONG, 0);
    class_addmethod(c, (method)ll_menu_clearchecks, "clearchecks", 0);
    class_addmethod(c, (method)ll_menu_checksymbol, "checksymbol", A_SYM, A_LONG, 0);
    
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

    CLASS_ATTR_ATOM(c, "items", 0, t_ll_menu, items_text);
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
    CLASS_ATTR_STYLE_LABEL(c, "pattrmode", 0, "onoff", "Pattr Stores Symbol");
    CLASS_ATTR_DEFAULT_SAVE(c, "pattrmode", 0, "0");
    
    CLASS_ATTR_CHAR(c, "checkmode", 0, t_ll_menu, checkmode);
    CLASS_ATTR_STYLE_LABEL(c, "checkmode", 0, "enum", "Check Mode");
    CLASS_ATTR_ENUMINDEX(c, "checkmode", 0, "Manual Selected Toggles");
    CLASS_ATTR_DEFAULT_SAVE(c, "checkmode", 0, "1"); // default last_selected?
    
    CLASS_STICKY_ATTR_CLEAR(c, "category");
    
    class_register(CLASS_BOX, c);
    ll_menu_class = c;
}

// Store incoming list of options
void ll_menu_items(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    // free previous list
    if (x->items)
        object_free(x->items);

    x->items = atomarray_new(argc, argv);

    // update count
    long new_count = argc;
    ll_menu_resize_checked_flags(x, new_count);

    // validate selected_item
    ll_menu_validate_selected_item(x);

    // update text representation
    ll_menu_update_items_text(x);

    // attribute modified → redraw but DO NOT output or change pattr
    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object*)x, gensym("items")));
    jbox_redraw((t_jbox*)x);
}


void ll_menu_getdrawparams(t_ll_menu *x, t_object *patcherview, t_jboxdrawparams *params)
{
    params->d_borderthickness = 0;
    params->d_bordercolor     = x->bordercolor;
    params->d_boxfillcolor    = x->bgcolor;
}

// Show popup menu on bang
void ll_menu_bang(t_ll_menu *x)
{
    // TODO: output value
    ll_menu_show(x);
}

// Dump menu items
void ll_menu_dump(t_ll_menu *x)
{
    if (!x->items)
        return;

    long count = linklist_getsize(x->items);

    for (long i = 0; i < count; i++) {
        t_atomarray *it = (t_atomarray *)linklist_getindex(x->items, i);
        if (!it)
            continue;

        long ac = 0;
        t_atom *av = NULL;
        atomarray_getatoms(it, &ac, &av);

        // outv = [ index, <item atoms...> ]
        long outc = ac + 1;
        t_atom *outv = (t_atom *)sysmem_newptr(outc * sizeof(t_atom));
        if (!outv)
            return; // allocation failed; bail safely

        // first atom = index
        atom_setlong(outv, i);

        // copy item atoms after index
        if (ac > 0)
            sysmem_copyptr(av, outv + 1, ac * sizeof(t_atom));

        outlet_anything(x->outlet_status, gensym("dump"), outc, outv);

        sysmem_freeptr(outv);
    }
}

void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    post("ANYTHING: %s", s->s_name);
}

// Show popup menu on bang
void ll_menu_show(t_ll_menu *x)
{
    if (!x->items || linklist_getsize(x->items) == 0) {
//        object_error((t_object *)x, "no items set — send a list first");
        return;
    }

    t_jpopupmenu *menu = jpopupmenu_create();

    // get colors from the object attributes
    t_jrgba text, bg;
    object_attr_getjrgba((t_object *)x, _sym_color, &text);
    object_attr_getjrgba((t_object *)x, _sym_bgcolor, &bg);

    // simple highlight color (slightly brighter background)
    t_jrgba hilitebg = bg;
    hilitebg.red   = MIN(1.0, bg.red + 0.15);
    hilitebg.green = MIN(1.0, bg.green + 0.15);
    hilitebg.blue  = MIN(1.0, bg.blue + 0.15);

    t_jrgba hilitetext = text;

    // apply colors and font
    jpopupmenu_setcolors(menu, text, bg, hilitetext, hilitebg);
    
    // build a menu font (can differ from UI font)
    t_symbol *fname = jbox_get_fontname((t_object *)x);
    double msize = (x->menufontsize > 0) ? x->menufontsize : x->fontsize;
    if (msize <= 0) msize = 12.0;

    t_jfont *menufont = jfont_create(
        fname && fname != gensym("") ? fname->s_name : "Arial",
        jbox_get_font_slant((t_object *)x),
        jbox_get_font_weight((t_object *)x),
        msize
    );
    jpopupmenu_setfont(menu, menufont);
    jfont_destroy(menufont);

    // ----- build menu items from linklist of t_atomarray -----
    long count = x->items ? linklist_getsize(x->items) : 0;

    for (long i = 0; i < count; i++) {
        t_atomarray *it = (t_atomarray *)linklist_getindex(x->items, i);
        if (!it)
            continue;

        long ac2 = 0;
        t_atom *av2 = NULL;
        atomarray_getatoms(it, &ac2, &av2);

        if (ac2 <= 0 || !av2)
            continue;

        long txtsize = 0;
        char *txt = NULL;
        if (atom_gettext(ac2, av2, &txtsize, &txt, OBEX_UTIL_ATOM_GETTEXT_SYM_NO_QUOTE) != MAX_ERR_NONE || !txt)
            continue;

        const char *label = txt;

        // trim leading/trailing spaces
        const char *p = label;
        while (*p && isspace((unsigned char)*p)) p++;
        size_t len = strlen(p);
        while (len && isspace((unsigned char)p[len-1])) len--;

        // buffer for display label (possibly unwrapped)
        char labelbuf[256];
        if (len >= sizeof(labelbuf))
            len = sizeof(labelbuf) - 1;
        memcpy(labelbuf, p, len);
        labelbuf[len] = '\0';

        // ----- separators -----
        if (!strcmp(labelbuf, "-") || !strcmp(labelbuf, "<separator>")) {
            jpopupmenu_addseparator(menu);
            sysmem_freeptr(txt);
            continue;
        }

        // ----- detect "(...)" = disabled, but keep inner text for display -----
        int disabled = 0;
        if (len >= 2 && labelbuf[0] == '(' && labelbuf[len-1] == ')') {
            disabled = 1;

            // unwrap parens
            size_t inner_len = len - 2;
            if (inner_len >= sizeof(labelbuf))
                inner_len = sizeof(labelbuf) - 1;

            memmove(labelbuf, labelbuf + 1, inner_len);
            labelbuf[inner_len] = '\0';

            // trim inner
            char *ib = labelbuf;
            while (*ib && isspace((unsigned char)*ib)) ib++;
            char *end = ib + strlen(ib);
            while (end > ib && isspace((unsigned char)end[-1])) *--end = '\0';

            if (ib != labelbuf)
                memmove(labelbuf, ib, strlen(ib) + 1);
        }

        // -------- CHECK STATE --------
        //
        // manual : use checked_flags
        // selected : highlight selected_item
        // toggles : use checked_flags
        //
        int is_checked = 0;

        if (x->checkmode == SELECTED) {
            // last_selected
            if (i == x->selected_item)
                is_checked = 1;
        }
        else {
            // manual (0) AND toggles (2)
            if (x->checked_flags && x->checked_flags[i])
                is_checked = 1;
        }

        // ----- per-item color (dim if disabled) -----
        t_jrgba itemtext = text;
        if (disabled) {
            itemtext.red   = itemtext.red   * 0.7;
            itemtext.green = itemtext.green * 0.7;
            itemtext.blue  = itemtext.blue  * 0.7;
            itemtext.alpha = itemtext.alpha * 0.6;
        }

        // IMPORTANT: use id = i+1 so we can map back to linklist index
        jpopupmenu_additem(menu,
                           (int)(i + 1),
                           labelbuf,      // visible label
                           &itemtext,
                           is_checked,
                           disabled,
                           NULL);

        sysmem_freeptr(txt);
    }

    // ----- where to show the menu -----
    bool showAtMousePosition = x->objectclick == 0 && x->position_mode == 1;
    t_pt pt;
    if (showAtMousePosition) {
        int xpix, ypix;
        jmouse_getposition_global(&xpix, &ypix);
        pt.x = (double)xpix;
        pt.y = (double)ypix;
    } else {
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

        pt.x = (double)rect.x;
        pt.y = (double)rect.y + rect.height - 1;

        long sx, sy;
        patcherview_canvas_to_screen(view, pt.x, pt.y, &sx, &sy);
        pt.x = (double)sx;
        pt.y = (double)sy;
    }
    
    // ----- popup -----
    int choice = jpopupmenu_popup(menu, pt, 0);
    long count2 = linklist_getsize(x->items);
    
    if (choice > 0 && choice <= count2) {
        long index = choice - 1;
        ll_menu_set_selected_and_output(x, index);
    }
    else if (x->show_cancel) {
        outlet_anything(x->outlet_symbol, gensym("<cancel>"), 0, NULL);
        outlet_int(x->outlet_index, -1);
    }

    if (x->objectclick)
        x->objectclick = 0;

    jpopupmenu_destroy(menu);
}


void ll_menu_free(t_ll_menu *x)
{
    if (x->font)
        jfont_destroy(x->font);
    
    if (x->items)
        object_free(x->items);
    jbox_free((t_jbox *)x);
}

void *ll_menu_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ll_menu *x;
    long flags;
    t_dictionary *d = NULL;

    if (!(d = object_dictionaryarg(argc, argv)))
        return NULL;

    x = (t_ll_menu *)object_alloc(ll_menu_class);
    if (!x)
        return NULL;

    flags = 0
        | JBOX_DRAWFIRSTIN
        | JBOX_DRAWBACKGROUND
        | JBOX_NODRAWBOX
        | JBOX_DRAWINLAST
        | JBOX_TRANSPARENT
        | JBOX_GROWBOTH
        | JBOX_FIXWIDTH;

    jbox_new(&x->j_box, flags, argc, argv);
    x->j_box.b_firstin = (t_object *)x;

    x->prepend = gensym("");
    x->items   = NULL;
    x->objectclick = 0;
    
    x->selected_item = 0;
    x->checked_flags = NULL;   // allocated later when items exist
    x->checkmode = 0;          // DEFAULT: manual (matches umenu behavior)
    
    x->items_text[0] = 0;  // ensure empty string
    
    x->outlet_status = outlet_new((t_object *)x, NULL);     // right: dump
    x->outlet_symbol = outlet_new((t_object *)x, NULL);     // middle: symbol
    x->outlet_index  = outlet_new((t_object *)x, NULL);     // left: int index

    attr_dictionary_process(x, d);

    jbox_ready(&x->j_box);
    return x;
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

    // FONT (rebuilt each paint — normal for Max objects)
    if (x->font)
        jfont_destroy(x->font);

    t_symbol *fname = jbox_get_fontname((t_object *)x);
    double fsize = (x->fontsize > 0 ? x->fontsize : jbox_get_fontsize((t_object *)x));
    if (fsize <= 0) fsize = 12.0;

    x->font = jfont_create(
        fname && fname != gensym("") ? fname->s_name : "Arial",
        jbox_get_font_slant((t_object *)x),
        jbox_get_font_weight((t_object *)x),
        fsize
    );

    // COMPUTE DISPLAYED TEXT
    t_symbol *display = gensym("");

    long count = x->items ? linklist_getsize(x->items) : 0;

    // primary: selected_item
    if (x->selected_item >= 0 && x->selected_item < count) {
        t_atomarray *it = linklist_getindex(x->items, x->selected_item);
        display = ll_menu_get_item_symbol(it);
    }

    // fallback: first selectable
    if (!display || display == gensym("") || display == gensym("<none>")) {
        display = ll_menu_first_selectable_symbol(x);
    }

    const char *label = (display && display != gensym("")) ? display->s_name : "";

    // TEXT LAYOUT
    double margin    = 4.0;
    double arrow_pad = 10.0;
    double text_w    = rect.width - (margin * 2 + arrow_pad);
    if (text_w < 1) text_w = 1;

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

    // explicitly set text color (fixes Max 8+)
    jtextlayout_settextcolor(layout, &txt);

    // draw label
    jtextlayout_draw(layout, g);
    jtextlayout_destroy(layout);

    // ARROW (dropdown marker)
    if (x->draw_arrow) {
        jgraphics_set_source_jrgba(g, &txt);
        jgraphics_move_to(g, rect.width - 10,           rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 5,            rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 7.5,          rect.height / 2 + 2);
        jgraphics_close_path(g);
        jgraphics_fill(g);
    }

    // BORDER
    jgraphics_set_source_jrgba(g, &border);
    jgraphics_rectangle(g, 0.5, 0.5, rect.width - 1, rect.height - 1);
    jgraphics_stroke(g);
}

void ll_menu_mousedown(t_ll_menu *x, t_object *patcherview, t_pt pt, long modifiers)
{
    x->objectclick = 1;
    ll_menu_bang(x);
}

t_max_err ll_menu_getvalue(t_ll_menu *x, long *ac, t_atom **av)
{
    if (!ac || !av)
        return MAX_ERR_NONE;

    char alloc = 0;
    if (atom_alloc_array(1, ac, av, &alloc))
        return MAX_ERR_OUT_OF_MEM;

    long count = x->items ? linklist_getsize(x->items) : 0;

    // pattr stores index
    if (x->pattr_stores_sym == 0) {
        long out_index = (count == 0 ? 0 : x->selected_item);
        atom_setlong(*av, out_index);
        return MAX_ERR_NONE;
    }

    // pattr stores symbol
    if (x->selected_item >= 0 && x->selected_item < count) {
        t_atomarray *it = linklist_getindex(x->items, x->selected_item);
        t_symbol *sym = ll_menu_get_item_symbol(it);
        atom_setsym(*av, sym ? sym : gensym(""));
    } else {
        atom_setsym(*av, gensym(""));
    }

    return MAX_ERR_NONE;
}

t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av)
{
    if (ac < 1 || !av)
        return MAX_ERR_NONE;

    long count = x->items ? linklist_getsize(x->items) : 0;
    if (count == 0)
        return MAX_ERR_NONE;

    // restore by index
    if (x->pattr_stores_sym == 0 && atom_gettype(av) == A_LONG) {
        long index = atom_getlong(av);
        ll_menu_set_selected_internal(x, index);
        return MAX_ERR_NONE;
    }

    // restore by symbol
    if (atom_gettype(av) == A_SYM) {
        t_symbol *s = atom_getsym(av);

        for (long i = 0; i < count; i++) {
            t_atomarray *it = linklist_getindex(x->items, i);
            if (!it || !ll_menu_item_is_selectable(it))
                continue;

            long ac2 = 0;
            t_atom *av2 = NULL;
            atomarray_getatoms(it, &ac2, &av2);

            if (ac2 > 0 &&
                atom_gettype(av2) == A_SYM &&
                atom_getsym(av2) == s)
            {
                ll_menu_set_selected_internal(x, i);
                return MAX_ERR_NONE;
            }
        }
    }

    // fallback to FIRST selectable
    long first = ll_menu_first_selectable_index(x);
    if (first >= 0)
        ll_menu_set_selected_internal(x, first);

    return MAX_ERR_NONE;
}


void ll_menu_clear(t_ll_menu *x)
{
    if (x->items)
        linklist_clear(x->items);

    // resize flags to zero
    ll_menu_resize_checked_flags(x, 0);

    // reset items_text
    strncpy(x->items_text, "<empty>", sizeof(x->items_text)-1);
    x->items_text[sizeof(x->items_text)-1] = 0;

    // selected_item will become 0 inside validate
    ll_menu_validate_selected_item(x);

    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object*)x, gensym("items")));
    jbox_redraw((t_jbox*)x);
}


void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    if (!x->items) {
        x->items = linklist_new();
        linklist_flags(x->items, OBJ_FLAG_DATA);
    }

    t_atomarray *item = atomarray_new(argc, argv);
    linklist_append(x->items, item);

    long new_count = linklist_getsize(x->items);
    ll_menu_resize_checked_flags(x, new_count);
    ll_menu_validate_selected_item(x);

    ll_menu_update_items_text(x);

    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object*)x, gensym("items")));
    jbox_redraw((t_jbox*)x);
}

void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv)
{
    if (!x->items)
        return;

    long size = linklist_getsize(x->items);
    if (index < 0) index = 0;
    if (index > size) index = size;

    t_atomarray *item = atomarray_new(argc, argv);
    linklist_insertindex(x->items, item, index);

    long new_count = linklist_getsize(x->items);
    ll_menu_resize_checked_flags(x, new_count);
    ll_menu_validate_selected_item(x);

    ll_menu_update_items_text(x);
    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object*)x, gensym("items")));
    jbox_redraw((t_jbox*)x);
}

void ll_menu_delete(t_ll_menu *x, long index)
{
    if (!x->items)
        return;

    t_atomarray *entry = linklist_getindex(x->items, index);
    if (entry)
        object_free(entry);

    linklist_deleteindex(x->items, index);

    long new_count = linklist_getsize(x->items);
    ll_menu_resize_checked_flags(x, new_count);
    ll_menu_validate_selected_item(x);

    ll_menu_update_items_text(x);
    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object*)x, gensym("items")));
    jbox_redraw((t_jbox*)x);
}

//
// Set selected item from messages
//
void ll_menu_int(t_ll_menu *x, long index)
{
    ll_menu_set_selected_and_output(x, index);
}

void ll_menu_setint(t_ll_menu *x, long index)
{
    ll_menu_set_selected_internal(x, index);
}

void ll_menu_symbol(t_ll_menu *x, t_symbol *s)
{
    if (!x->items) return;

    long count = linklist_getsize(x->items);

    for (long i = 0; i < count; i++) {
        t_atomarray *it = linklist_getindex(x->items, i);
        if (!it || !ll_menu_item_is_selectable(it))
            continue;

        long ac = 0;
        t_atom *av = NULL;
        atomarray_getatoms(it, &ac, &av);

        if (ac > 0 && atom_gettype(av) == A_SYM && atom_getsym(av) == s) {
            ll_menu_set_selected_and_output(x, i);
            return;
        }
    }
}

void ll_menu_setsymbol(t_ll_menu *x, t_symbol *s)
{
    if (!x->items) return;

    long count = linklist_getsize(x->items);

    for (long i = 0; i < count; i++) {
        t_atomarray *it = linklist_getindex(x->items, i);
        if (!it || !ll_menu_item_is_selectable(it))
            continue;

        long ac = 0;
        t_atom *av = NULL;
        atomarray_getatoms(it, &ac, &av);

        if (ac > 0 && atom_gettype(av) == A_SYM && atom_getsym(av) == s) {
            ll_menu_set_selected_internal(x, i);
            return;
        }
    }
}

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *ac, t_atom **av)
{
    ll_menu_update_items_text(x);

    if (ac && av) {
        char alloc;
        if (!atom_alloc_array(1, ac, av, &alloc))
            atom_setsym(*av, gensym(x->items_text));
    }

    return MAX_ERR_NONE;
}

t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long ac, t_atom *av)
{
    // Empty case
    if (ac == 0 ||
       (ac == 1 && atom_gettype(av) == A_SYM &&
        atom_getsym(av) == gensym(""))) {

        ll_menu_clear(x);
        return MAX_ERR_NONE;
    }

    // Get full text
    long txtsize = 0;
    char *txt = NULL;

    if (atom_gettext(ac, av, &txtsize, &txt, ITEMS_FLAGS) == MAX_ERR_NONE && txt) {

        strncpy(x->items_text, txt, sizeof(x->items_text)-1);
        x->items_text[sizeof(x->items_text)-1] = 0;
        sysmem_freeptr(txt);

        // ---- rebuild list from text ----
        ll_menu_rebuild_items_from_text(x);

        // ---- update flags + selection ----
        long new_count = linklist_getsize(x->items);
        ll_menu_resize_checked_flags(x, new_count);
        ll_menu_validate_selected_item(x);

        // notify
        object_notify(x, gensym("attr_modified"), attr);
        jbox_redraw((t_jbox*)x);
    }

    return MAX_ERR_NONE;
}

void ll_menu_rebuild_items_from_text(t_ll_menu *x)
{
    if (!strcmp(x->items_text, "<empty>")) {
        if (x->items)
            linklist_clear(x->items);
        return;
    }

    if (!x->items) {
        x->items = linklist_new();
        linklist_flags(x->items, OBJ_FLAG_DATA);
    } else {
        linklist_clear(x->items);
    }

    char buffer[2048];
    strncpy(buffer, x->items_text, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = 0;

    char *start = buffer;
    char *comma;

    while ((comma = strstr(start, ", "))) {
        *comma = 0;

        long ac = 0;
        t_atom *av = NULL;

        if (atom_setparse(&ac, &av, start) == MAX_ERR_NONE) {
            t_atomarray *item = atomarray_new(ac, av);
            linklist_append(x->items, item);
        }

        if (av) sysmem_freeptr(av);

        start = comma + 2;
    }

    if (*start) {
        long ac = 0;
        t_atom *av = NULL;

        if (atom_setparse(&ac, &av, start) == MAX_ERR_NONE) {
            t_atomarray *item = atomarray_new(ac, av);
            linklist_append(x->items, item);
        }

        if (av) sysmem_freeptr(av);
    }
}

void ll_menu_update_items_text(t_ll_menu *x)
{
    if (!x->items) {
        x->items_text[0] = 0;
        return;
    }

    char result[2048] = "";
    long count = x->items ? linklist_getsize(x->items) : 0;
    if (count == 0) {
        strcpy(x->items_text, "<empty>");
        return;
    }

    for (long i = 0; i < count; i++) {
        t_atomarray *it = (t_atomarray*)linklist_getindex(x->items, i);
        if (!it)
            continue;

        long ac = 0;
        t_atom *av = NULL;
        atomarray_getatoms(it, &ac, &av);

        long txtsize = 0;
        char *txt = NULL;

        if (atom_gettext(ac, av, &txtsize, &txt, ITEMS_FLAGS) == MAX_ERR_NONE && txt) {
            size_t remain = sizeof(result) - strlen(result) - 1;
            strncat(result, txt, remain);

            if (i < count - 1) {
                // comma-space between entries
                remain = sizeof(result) - strlen(result) - 1;
                strncat(result, ", ", remain);
            }
            sysmem_freeptr(txt);
        }
    }

    strncpy(x->items_text, result, sizeof(x->items_text)-1);
    x->items_text[sizeof(x->items_text)-1] = 0;
}


void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    // Attribute changed?
    if (msg == gensym("attr_modified") && sender == (void *)x) {
        // The following causes <unknown> to show in pattrstorage
//        object_notify(x, gensym("value"), NULL);
//        object_notify(x, gensym("modified"), NULL); // PARAM + PATTR updated
        jbox_redraw((t_jbox *)x);
    }
}

// Return 1 if the item is a normal selectable menu entry
// Return 0 if separator ("-" or "<separator>") or disabled "(foo)"
char ll_menu_item_is_selectable(t_atomarray *it)
{
    if (!it) return 0;

    long ac = 0;
    t_atom *av = NULL;
    atomarray_getatoms(it, &ac, &av);

    if (ac <= 0 || !av)
        return 0;

    // Convert atoms → text for detection, with NO quotes
    long txtsize = 0;
    char *txt = NULL;
    if (atom_gettext(ac, av, &txtsize, &txt,
                     ITEMS_FLAGS) != MAX_ERR_NONE || !txt)
        return 0;

    // trim whitespace
    char *p = txt;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while (len && isspace((unsigned char)p[len - 1]))
        len--;

    int selectable = 1;

    // special case: empty after trim
    if (len == 0)
        selectable = 0;

    // separator
    if ((len == 1 && p[0] == '-') ||
        (len == 11 && !strncmp(p, "<separator>", 11)))
    {
        selectable = 0;
    }

    // disabled "(foo)" items
    if (len >= 2 && p[0] == '(' && p[len - 1] == ')')
        selectable = 0;

    sysmem_freeptr(txt);
    return selectable;
}

t_symbol *ll_menu_first_selectable_symbol(t_ll_menu *x)
{
    if (!x->items)
        return gensym("");

    long count = linklist_getsize(x->items);

    for (long i = 0; i < count; i++) {
        t_atomarray *it = (t_atomarray *)linklist_getindex(x->items, i);
        if (!it)
            continue;

        if (!ll_menu_item_is_selectable(it))
            continue;

        // FULL TEXT for display, not only first atom
        long ac = 0;
        t_atom *av = NULL;
        atomarray_getatoms(it, &ac, &av);

        long txtsize = 0;
        char *txt = NULL;

        if (atom_gettext(ac, av, &txtsize, &txt, ITEMS_FLAGS) != MAX_ERR_NONE || !txt)
            return gensym("");

        t_symbol *sym = gensym(txt);
        sysmem_freeptr(txt);
        return sym;
    }

    return gensym("");
}

void ll_menu_dict(t_ll_menu *x, t_symbol *s)
{
    t_dictionary *d = dictobj_findregistered_retain(s);
    if (!d)
        return;

    // key "items" MUST be an atomarray
    t_object *obj = NULL;
    if (dictionary_getatomarray(d, gensym("items"), &obj) != MAX_ERR_NONE || !obj) {
        dictobj_release(d);
        return;
    }

    t_atomarray *aa = (t_atomarray *)obj;
    long ac = 0;
    t_atom *av = NULL;
    atomarray_getatoms(aa, &ac, &av);

    // no items → clear menu
    if (ac == 0) {
        ll_menu_clear(x);
        dictobj_release(d);
        return;
    }

    // ensure list exists
    if (!x->items) {
        x->items = linklist_new();
        linklist_flags(x->items, OBJ_FLAG_DATA);
    } else {
        linklist_clear(x->items);
    }

    // build from each symbol in the array
    for (long i = 0; i < ac; i++) {

        if (atom_gettype(av + i) != A_SYM)
            continue;

        t_symbol *sym = atom_getsym(av + i);

        // Create a single-atom entry
        t_atom item_atom;
        atom_setsym(&item_atom, sym);

        t_atomarray *it = atomarray_new(1, &item_atom);
        linklist_append(x->items, it);
    }

    // rebuild canonical items_text
    ll_menu_update_items_text(x);

    // notify
    object_notify(x, gensym("attr_modified"),
                  object_attr_get((t_object *)x, gensym("items")));

    jbox_redraw((t_jbox *)x);

    dictobj_release(d);
}

void ll_menu_resize_checked_flags(t_ll_menu *x, long new_count)
{
    long old_count = x->items_count;
    char *old_flags = x->checked_flags;

    // allocate new array
    if (new_count > 0) {
        x->checked_flags = (char *)sysmem_newptrclear(new_count * sizeof(char));

        // copy previous states (manual/toggles preserve)
        if (old_flags) {
            long copy_count = (old_count < new_count ? old_count : new_count);
            for (long i = 0; i < copy_count; i++)
                x->checked_flags[i] = old_flags[i];
        }
    }
    else {
        // no items → no flags
        x->checked_flags = NULL;
    }

    // free old
    if (old_flags)
        sysmem_freeptr(old_flags);

    // update x->items_count AFTER resizing
    x->items_count = new_count;

    // fix selected_item if out of range
    if (x->selected_item >= new_count)
        x->selected_item = 0;
}

t_symbol *ll_menu_get_item_symbol(t_atomarray *it)
{
    if (!it)
        return gensym("");

    long ac = 0;
    t_atom *av = NULL;
    atomarray_getatoms(it, &ac, &av);

    if (ac <= 0 || !av)
        return gensym("");

    // Turn entire atom list into a single string
    long txtsize = 0;
    char *txt = NULL;

    // Use atom_gettext with NO_QUOTE flag so "six seven" stays: six seven
    if (atom_gettext(ac, av, &txtsize, &txt, ITEMS_FLAGS) != MAX_ERR_NONE || !txt)
        return gensym("");

    // Convert to symbol
    t_symbol *result = gensym(txt);

    sysmem_freeptr(txt);
    return result;
}

long ll_menu_first_selectable_index(t_ll_menu *x)
{
    if (!x->items)
        return -1;

    long count = linklist_getsize(x->items);
    for (long i = 0; i < count; i++) {
        t_atomarray *it = linklist_getindex(x->items, i);
        if (!it)
            continue;

        if (ll_menu_item_is_selectable(it))
            return i;
    }
    return -1;
}

void ll_menu_validate_selected_item(t_ll_menu *x)
{
    long count = x->items ? linklist_getsize(x->items) : 0;

    // No items → nothing to validate
    if (count == 0) {
        x->selected_item = 0;
        return;
    }

    // If selected_item is out of range, find first selectable
    if (x->selected_item < 0 || x->selected_item >= count) {
        long first = ll_menu_first_selectable_index(x);
        x->selected_item = (first >= 0 ? first : 0);
        return;
    }

    // Selected item exists but is not selectable
    t_atomarray *it = linklist_getindex(x->items, x->selected_item);
    if (!ll_menu_item_is_selectable(it)) {
        long first = ll_menu_first_selectable_index(x);
        x->selected_item = (first >= 0 ? first : 0);
    }
}

void ll_menu_set_selected_internal(t_ll_menu *x, long index)
{
    long count = x->items ? linklist_getsize(x->items) : 0;
    if (count == 0)
        return;

    if (index < 0 || index >= count)
        return;

    t_atomarray *it = linklist_getindex(x->items, index);
    if (!ll_menu_item_is_selectable(it))
        return;

    // -------- update selected_item --------
    x->selected_item = index;

    // -------- apply checkmode --------
    switch (x->checkmode) {
        case MANUAL:
            break;

        case SELECTED:
            memset(x->checked_flags, 0, x->items_count * sizeof(char));
            x->checked_flags[index] = 1;
            break;

        case TOGGLES:
            x->checked_flags[index] ^= 1;
            break;
    }

    jbox_redraw((t_jbox *)x);
    object_notify(x, gensym("modified"), NULL); // pattr notify
}

void ll_menu_set_selected_and_output(t_ll_menu *x, long index)
{
    long count = x->items ? linklist_getsize(x->items) : 0;
    if (count == 0)
        return;

    if (index < 0 || index >= count)
        return;

    t_atomarray *it = linklist_getindex(x->items, index);
    if (!ll_menu_item_is_selectable(it))
        return;

    // -------- update selection + checkflags --------
    ll_menu_set_selected_internal(x, index);

    // -------- output list --------
    long ac = 0;
    t_atom *av = NULL;
    atomarray_getatoms(it, &ac, &av);

    // PREPEND mode
    if (x->prepend && x->prepend != gensym("")) {
        t_atom *outv = (t_atom *)sysmem_newptr((ac + 1) * sizeof(t_atom));

        atom_setsym(outv, x->prepend);
        if (ac > 0)
            sysmem_copyptr(av, outv + 1, ac * sizeof(t_atom));

        outlet_list(x->outlet_symbol, NULL, ac + 1, outv);
        sysmem_freeptr(outv);
    } else {
        outlet_list(x->outlet_symbol, NULL, ac, av);
    }

    // -------- output index --------
    outlet_int(x->outlet_index, index);

    // notify pattr/parameter
    object_notify(x, gensym("modified"), NULL);
}

void ll_menu_checkitem(t_ll_menu *x, long index, long mode)
{
    long count = x->items_count;
    if (index < 0 || index >= count || !x->checked_flags)
        return;

    if      (mode == 0)     x->checked_flags[index] = 0;
    else if (mode == 1)     x->checked_flags[index] = 1;
    else if (mode == -1)    x->checked_flags[index] ^= 1;

    // In last_selected mode, checkitem also updates selected_item
    if (x->checkmode == SELECTED) {
        x->selected_item = index;
    }

    jbox_redraw((t_jbox *)x);
    object_notify(x, gensym("modified"), NULL);
}

void ll_menu_clearchecks(t_ll_menu *x)
{
    if (!x->checked_flags || x->items_count == 0)
        return;

    memset(x->checked_flags, 0, x->items_count * sizeof(char));

    // In last_selected mode, clearchecks resets selection to the first selectable
    if (x->checkmode == SELECTED) {
        x->selected_item = ll_menu_first_selectable_index(x);
        if (x->selected_item < 0)
            x->selected_item = 0;
    }

    jbox_redraw((t_jbox *)x);
    object_notify(x, gensym("modified"), NULL);
}

void ll_menu_checksymbol(t_ll_menu *x, t_symbol *s, long mode)
{
    if (!x->items || !s || s == gensym(""))
        return;

    long count = x->items_count;
    if (count <= 0)
        return;

    for (long i = 0; i < count; i++) {
        t_atomarray *it = linklist_getindex(x->items, i);
        if (!it || !ll_menu_item_is_selectable(it))
            continue;

        t_symbol *sym = ll_menu_get_item_symbol(it);
        if (!sym || sym != s)
            continue;
        
        char current = x->checked_flags ? x->checked_flags[i] : 0;
        char newstate = current;

        if (mode == 0)
            newstate = 0;             // OFF
        else if (mode == 1)
            newstate = 1;             // ON
        else if (mode == -1)
            newstate = !current;      // toggle

        if (x->checkmode == SELECTED) {
            // last_selected → exclusive selection
            memset(x->checked_flags, 0, count * sizeof(char));
            x->checked_flags[i] = newstate;

            // Also update selected_item
            x->selected_item = i;
        }
        else {
            // manual or toggles → apply directly
            x->checked_flags[i] = newstate;
        }

        jbox_redraw((t_jbox *)x);
        object_notify(x, gensym("modified"), NULL);
        return;
    }
}
