#include "ext.h"
#include "ext_obex.h"
#include "ext_obex_util.h"   // <-- REQUIRED for atom_settext, atom_gettext
#include "jgraphics.h"
#include "jpatcher_api.h"

typedef struct _ll_menu {
    t_jbox  j_box;
    void    *outlet_index, *outlet_symbol, *outlet_status;

    t_atomarray *items;   // store the menu items dynamically
    t_symbol *prepend;   // optional symbol to prepend on output
    t_symbol *checked;   // symbol to mark as checked
    
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
void ll_menu_show(t_ll_menu *x);
void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

void ll_menu_paint(t_ll_menu *x, t_object *view);
void ll_menu_getdrawparams(t_ll_menu *x, t_object *patcherview, t_jboxdrawparams *params);
void ll_menu_mousedown(t_ll_menu *x, t_object *patcherview, t_pt pt, long modifiers);
t_max_err ll_menu_getvalue(t_ll_menu *x, long *ac, t_atom **av);
t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av);

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *argc, t_atom **argv);
t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long argc, t_atom *argv);

void ll_menu_rebuild_items_from_text(t_ll_menu *x);
void ll_menu_update_items_text(t_ll_menu *x);

void ll_menu_clear(t_ll_menu *x);
void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv);
void ll_menu_delete(t_ll_menu *x, long index);
void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);




// ll_menu only ?
void ll_menu_items(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv);
void ll_menu_checksymbol(t_ll_menu *x, t_symbol *s);

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
    
    // ll_menu
    class_addmethod(c, (method)ll_menu_show, "show", 0);
    
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

    CLASS_ATTR_CHAR_ARRAY(c, "items", 0, t_ll_menu, items_text, 2048);
    CLASS_ATTR_LABEL(c,  "items", 0, "Menu Items");
    CLASS_ATTR_STYLE(c,  "items", 0, "text_large");  // <-- Edit… button!
    CLASS_ATTR_ACCESSORS(c, "items", ll_menu_getitems, ll_menu_setitems);
    CLASS_ATTR_SAVE(c,   "items", 0);
    CLASS_ATTR_BASIC(c,  "items", 0);
    
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
    
    CLASS_STICKY_ATTR_CLEAR(c, "category");
    
    class_register(CLASS_BOX, c);
    ll_menu_class = c;
}

// Store incoming list of options
void ll_menu_items(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    if (x->items)
        object_free(x->items);
    x->items = atomarray_new(argc, argv);
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

void ll_menu_anything(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    post("ANYTHING: %s", s->s_name);
}

// Show popup menu on bang
void ll_menu_show(t_ll_menu *x)
{
    if (!x->items) {
        object_error((t_object *)x, "no items set — send a list first");
        return;
    }

    t_jpopupmenu *menu = jpopupmenu_create();

    // get colors from the object attributes
    t_jrgba text, bg;
    object_attr_getjrgba((t_object *)x, _sym_color, &text);
    object_attr_getjrgba((t_object *)x, _sym_bgcolor, &bg);

    // optional: create a simple highlight color (slightly brighter background)
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

    // Add items from list
    long argc = 0;
    t_atom *argv = NULL;
    atomarray_getatoms(x->items, &argc, &argv);

    for (long i = 0; i < argc; i++) {
        const char *label = NULL;
        char buf[256];

        // ---- read label from atom ----
        switch (atom_gettype(argv + i)) {
            case A_SYM:
                label = atom_getsym(argv + i)->s_name;
                break;
            case A_LONG:
                snprintf(buf, sizeof(buf), "%ld", atom_getlong(argv + i));
                label = buf;
                break;
            case A_FLOAT:
                snprintf(buf, sizeof(buf), "%.2f", atom_getfloat(argv + i));
                label = buf;
                break;
            default:
                continue;
        }

        // ---- separators ----
        if (!strcmp(label, "-") || !strcmp(label, "<separator>")) {
            jpopupmenu_addseparator(menu);
            continue;
        }

        // ---- detect "(...)" = disabled, but keep inner text ----
        // trim leading/trailing spaces first
        const char *p = label;
        while (*p && isspace((unsigned char)*p)) p++;
        size_t len = strlen(p);
        while (len && isspace((unsigned char)p[len-1])) len--;

        int disabled = 0;
        char labelbuf[256];
        if (len >= 2 && p[0] == '(' && p[len-1] == ')') {
            disabled = 1;
            size_t inner_len = len - 2;
            if (inner_len >= sizeof(labelbuf)) inner_len = sizeof(labelbuf) - 1;
            memcpy(labelbuf, p + 1, inner_len);
            labelbuf[inner_len] = '\0';
            // trim inner spaces too
            char *ib = labelbuf;
            while (*ib && isspace((unsigned char)*ib)) ib++;
            char *end = ib + strlen(ib);
            while (end > ib && isspace((unsigned char)end[-1])) *--end = '\0';
            // if all spaces, fall back to original (still disabled)
            if (!*ib) strncpy(labelbuf, p, (len < sizeof(labelbuf) ? len : sizeof(labelbuf)-1)), labelbuf[len] = '\0';
        } else {
            // not wrapped in parens: use cleaned original span
            size_t copy_len = (len < sizeof(labelbuf) ? len : sizeof(labelbuf)-1);
            memcpy(labelbuf, p, copy_len);
            labelbuf[copy_len] = '\0';
        }

        // ---- checkmark: compare after unwrapping ----
        int is_checked = 0;
        if (x->checked && x->checked != gensym("") && !strcmp(labelbuf, x->checked->s_name)) {
            is_checked = 1;
        }

        // ---- per-item color (dim if disabled) ----
        t_jrgba itemtext = text;
        if (disabled) {
            itemtext.red   = itemtext.red   * 0.7;
            itemtext.green = itemtext.green * 0.7;
            itemtext.blue  = itemtext.blue  * 0.7;
            itemtext.alpha = itemtext.alpha * 0.6;  // slightly opaque
        }

        // pass per-item text color
        jpopupmenu_additem(menu,
                           (int)(i + 1),
                           labelbuf,
                           &itemtext,   // <- per-item color
                           is_checked,
                           disabled,
                           NULL);
    }

    bool showAtMousePosition = x->objectclick == 0 && x->position_mode == 1;
    t_pt pt;
    if(showAtMousePosition)
    {
        // Show menu near mouse
        int xpix, ypix;
        jmouse_getposition_global(&xpix, &ypix);
        pt.x = (double)xpix;
        pt.y = (double)ypix;
    }
    else
    {
        // Show menu below the box (bottom-left corner)
        t_object *patcher = NULL;
        object_obex_lookup((t_object *)x, gensym("#P"), &patcher);
        if (!patcher)
            return;

        t_object *view = jpatcher_get_firstview(patcher);
        if (!view)
            return;

        // get box rect (in patcher canvas coords)
        t_rect rect;
        jbox_get_rect_for_view((t_object *)x, view, &rect);

        // bottom-left corner of the box in patcher coords
        pt.x = (double)rect.x;
        pt.y = (double)rect.y + rect.height - 1;

        // convert patcher coords → screen coords
        long sx, sy;
        patcherview_canvas_to_screen(view, pt.x, pt.y, &sx, &sy);
        pt.x = (double)sx;
        pt.y = (double)sy;

    }
    
    // show popup menu
    int choice = jpopupmenu_popup(menu, pt, 0);
    if (choice > 0 && choice <= argc) {
        t_atom *a = argv + (choice - 1);
        t_symbol *sym = atom_getsym(a);
        
        if (sym)
            outlet_anything(x->outlet_symbol, sym, 0, NULL);

        // umenu uses 0-based
        outlet_int(x->outlet_index, choice - 1);

        x->checked = sym && sym != gensym("<none>") ? sym : gensym("");
        object_notify(x, gensym("modified"), NULL);
        jbox_redraw((t_jbox *)x);
    } else if(x->show_cancel) {
        // cancel selection
        outlet_anything(x->outlet_symbol, gensym("<cancel>"), 0, NULL);
        outlet_int(x->outlet_index, -1);
    }

    if(x->objectclick)
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
    x->checked = NULL;
    x->items   = NULL;
    x->objectclick = 0;
    
    x->items_text[2047] = 0;
    ll_menu_rebuild_items_from_text(x);
    
    x->outlet_status = outlet_new((t_object *)x, NULL);     // right: status/debug
    x->outlet_symbol = outlet_new((t_object *)x, NULL);     // middle: symbol
    x->outlet_index  = outlet_new((t_object *)x, NULL);     // left: int index

    attr_dictionary_process(x, d);

    jbox_ready(&x->j_box);
    return x;
}


void ll_menu_checksymbol(t_ll_menu *x, t_symbol *s)
{
    x->checked = s;
}

void ll_menu_paint(t_ll_menu *x, t_object *view)
{
    t_jgraphics *g = (t_jgraphics *)patcherview_get_jgraphics(view);
    t_rect rect;
    jbox_get_rect_for_view((t_object *)x, view, &rect);

    t_jrgba bg, txt, border;
    object_attr_getjrgba((t_object *)x, _sym_bgcolor, &bg);
    object_attr_getjrgba((t_object *)x, _sym_color, &txt);
    object_attr_getjrgba((t_object *)x, gensym("bordercolor"), &border);

    // background
    jgraphics_set_source_jrgba(g, &bg);
    jgraphics_rectangle_fill_fast(g, 0, 0, rect.width, rect.height);

    // rebuild font every paint ?
    if (x->font)
        jfont_destroy(x->font);

    t_symbol *fname = jbox_get_fontname((t_object *)x);
    double fsize = (x->fontsize > 0) ? x->fontsize : jbox_get_fontsize((t_object *)x);
    if (fsize <= 0) fsize = 12.0; // fallback

    x->font = jfont_create(
        fname && fname != gensym("") ? fname->s_name : "Arial",
        jbox_get_font_slant((t_object *)x),
        jbox_get_font_weight((t_object *)x),
        fsize
    );

    // text
    const char *label = (x->checked && x->checked != gensym("")) ? x->checked->s_name : "";
    double margin = 4.0, arrow_pad = 10.0;
    double text_w = rect.width - (margin * 2 + arrow_pad);
    if (text_w < 1) text_w = 1;

    t_jtextlayout *layout = jtextlayout_create();
    jtextlayout_set(
        layout, label, x->font,
        margin, 0,
        text_w, rect.height,
        JGRAPHICS_TEXT_JUSTIFICATION_LEFT | JGRAPHICS_TEXT_JUSTIFICATION_VCENTERED,
        JGRAPHICS_TEXTLAYOUT_NOWRAP
    );

    // explicitly set color
    jgraphics_set_source_jrgba(g, &txt);
    jtextlayout_settextcolor(layout, &txt);

    // draw
    jtextlayout_draw(layout, g);
    jtextlayout_destroy(layout);

    // dropdown triangle
    if (x->draw_arrow) {
        jgraphics_set_source_jrgba(g, &txt);
        jgraphics_move_to(g, rect.width - 10, rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 5,  rect.height / 2 - 3);
        jgraphics_line_to(g, rect.width - 7.5, rect.height / 2 + 2);
        jgraphics_close_path(g);
        jgraphics_fill(g);
    }

    // border
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

    char alloc;
    if (atom_alloc_array(1, ac, av, &alloc))
        return MAX_ERR_OUT_OF_MEM;

    if (x->pattr_stores_sym == 0) {
        //
        // Mode 0: return INDEX
        //
        long index = -1;

        if (x->checked && x->checked != gensym("")) {
            long argc = 0;
            t_atom *argv = NULL;
            atomarray_getatoms(x->items, &argc, &argv);

            for (long i = 0; i < argc; i++) {
                if (atom_getsym(argv + i) == x->checked) {
                    index = i; // 0-based
                    break;
                }
            }
        }

        atom_setlong(*av, index);
    } else {
        //
        // Mode 1: return SYMBOL
        //
        t_symbol *sym = (x->checked && x->checked != gensym("<none>"))
            ? x->checked
            : gensym("");
        atom_setsym(*av, sym);
    }

    return MAX_ERR_NONE;
}

t_max_err ll_menu_setvalue(t_ll_menu *x, long ac, t_atom *av)
{
    if (!ac || !av)
        return MAX_ERR_NONE;

    if (x->pattr_stores_sym == 0 && atom_gettype(av) == A_LONG) {
        //
        // Mode 0: set by INDEX
        //
        long index = atom_getlong(av);

        long argc = 0;
        t_atom *argv = NULL;
        atomarray_getatoms(x->items, &argc, &argv);

        if (index >= 0 && index < argc) {
            x->checked = atom_getsym(argv + index);
        } else {
            x->checked = gensym("");
        }
    } else {
        //
        // Mode 1 OR symbol provided
        //
        t_symbol *sym = atom_getsym(av);
        long argc = 0;
        t_atom *argv = NULL;
        atomarray_getatoms(x->items, &argc, &argv);

        // Validate symbol exists in item list
        int found = 0;
        for (long i = 0; i < argc; i++) {
            if (atom_getsym(argv + i) == sym) {
                found = 1;
                break;
            }
        }

        x->checked = found ? sym : gensym("");
    }

    // notify pattr & redraw
    object_notify(x, gensym("modified"), NULL);
    jbox_redraw((t_jbox *)x);
    return MAX_ERR_NONE;
}

void ll_menu_clear(t_ll_menu *x)
{
    if (x->items)
        object_free(x->items);
    x->items = atomarray_new(0, NULL);
    
    ll_menu_update_items_text(x);
    t_object *attr = object_attr_get((t_object *)x, gensym("items"));
    if (attr)
        object_notify(x, gensym("attr_modified"), attr);
    
    jbox_redraw((t_jbox *)x);
}

void ll_menu_append(t_ll_menu *x, t_symbol *s, long argc, t_atom *argv)
{
    if (!x->items)
        x->items = atomarray_new(0, NULL);

    atomarray_appendatoms(x->items, argc, argv);
    
    ll_menu_update_items_text(x);
    t_object *attr = object_attr_get((t_object *)x, gensym("items"));
    if (attr)
        object_notify(x, gensym("attr_modified"), attr);
    
    jbox_redraw((t_jbox *)x);
}

void ll_menu_insert(t_ll_menu *x, long index, long argc, t_atom *argv)
{
    if (!x->items)
        return;

    long oldcount = atomarray_getsize(x->items);
    if (index < 0) index = 0;
    if (index > oldcount) index = oldcount;

    long newcount = oldcount + argc;

    // get old atoms
    long oc = 0;
    t_atom *ov = NULL;
    atomarray_getatoms(x->items, &oc, &ov);

    // allocate new list
    long ac = 0;
    t_atom *av = NULL;
    char alloc = 0;
    if (atom_alloc_array(newcount, &ac, &av, &alloc))
        return;

    // copy before index
    for (long i = 0; i < index; i++)
        av[i] = ov[i];

    // insert new atoms
    for (long i = 0; i < argc; i++)
        av[index + i] = argv[i];

    // copy after index
    for (long i = index; i < oldcount; i++)
        av[argc + i] = ov[i];

    // replace atomarray contents
    atomarray_setatoms(x->items, ac, av);

    if (alloc)
        sysmem_freeptr(av);
    
    ll_menu_update_items_text(x);
    t_object *attr = object_attr_get((t_object *)x, gensym("items"));
    if (attr)
        object_notify(x, gensym("attr_modified"), attr);

    jbox_redraw((t_jbox *)x);
}

void ll_menu_delete(t_ll_menu *x, long index)
{
    if (!x->items)
        return;

    long oldcount = atomarray_getsize(x->items);
    if (index < 0 || index >= oldcount)
        return;

    long newcount = oldcount - 1;

    // get old atoms
    long oc = 0;
    t_atom *ov = NULL;
    atomarray_getatoms(x->items, &oc, &ov);

    long ac = 0;
    t_atom *av = NULL;
    char alloc = 0;

    if (atom_alloc_array(newcount, &ac, &av, &alloc))
        return;

    // before deleted item
    for (long i = 0; i < index; i++)
        av[i] = ov[i];

    // after deleted item
    for (long i = index + 1; i < oldcount; i++)
        av[i - 1] = ov[i];

    atomarray_setatoms(x->items, ac, av);

    if (alloc)
        sysmem_freeptr(av);
    
    
    ll_menu_update_items_text(x);
    t_object *attr = object_attr_get((t_object *)x, gensym("items"));
    if (attr)
        object_notify(x, gensym("attr_modified"), attr);

    jbox_redraw((t_jbox *)x);
}

t_max_err ll_menu_getitems(t_ll_menu *x, void *attr, long *ac, t_atom **av)
{
    if (!x->items) {
        *ac = 0;
        *av = NULL;
        return MAX_ERR_NONE;
    }

    return atomarray_copyatoms(x->items, ac, av);
}

t_max_err ll_menu_setitems(t_ll_menu *x, void *attr, long ac, t_atom *av)
{
    if (x->items)
        object_free(x->items);

    x->items = atomarray_new(ac, av);

    ll_menu_update_items_text(x);
    jbox_redraw((t_jbox *)x);

    return MAX_ERR_NONE;
}

void ll_menu_rebuild_items_from_text(t_ll_menu *x)
{
    char buf[2048];
    strncpy(buf, x->items_text, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;

    t_atom list[512];
    long count = 0;

    char *tok = strtok(buf, ",");

    while (tok && count < 512) {
        while (isspace((unsigned char)*tok)) tok++; // trim leading
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = 0; // trim trailing

        atom_setsym(list + count, gensym(tok));
        count++;
        tok = strtok(NULL, ",");
    }

    if (x->items)
        object_free(x->items);

    x->items = atomarray_new(count, list);
}

void ll_menu_update_items_text(t_ll_menu *x)
{
    long argc = 0;
    t_atom *argv = NULL;
    atomarray_getatoms(x->items, &argc, &argv);

    char text[2048] = "";
    for (long i = 0; i < argc; i++) {
        const char *s = atom_getsym(argv + i)->s_name;
        strcat(text, s);
        if (i < argc - 1)
            strcat(text, ", ");
    }

    strncpy(x->items_text, text, 2048);
    x->items_text[2047] = 0;
}

void ll_menu_notify(t_ll_menu *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    // Attribute changed?
    if (msg == gensym("attr_modified") && sender == (void *)x) {
        t_symbol *attrname = (t_symbol *)data;

        if (attrname == gensym("items")) {
            ll_menu_rebuild_items_from_text(x);
            object_notify(x, gensym("modified"), NULL); // PARAM + PATTR updated
            jbox_redraw((t_jbox *)x);
        }
        else if (attrname == gensym("pattrmode")) {
            object_notify(x, gensym("value"), NULL);
            object_notify(x, gensym("modified"), NULL);
            jbox_redraw((t_jbox *)x);
        }
    }
}
