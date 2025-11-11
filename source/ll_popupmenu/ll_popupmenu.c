#include "ext.h"
#include "ext_obex.h"
#include "jgraphics.h"
#include "jpatcher_api.h"

typedef struct _ll_popupmenu {
    t_jbox  j_box;
    void    *outlet;
    t_atomarray *items;   // store the menu items dynamically
    t_symbol *prepend;   // optional symbol to prepend on output
    t_symbol *checked;   // symbol to mark as checked
    
    char objectclick;
    char position_mode;
    
    char draw_arrow;
    
    t_jrgba     bgcolor;
    t_jrgba     textcolor;
    t_jrgba     bordercolor;
    double      fontsize;
    double      menufontsize;
    t_jfont    *font;
} t_ll_popupmenu;

t_class *ll_popupmenu_class;

void *ll_popupmenu_new(t_symbol *s, long argc, t_atom *argv);
void ll_popupmenu_free(t_ll_popupmenu *x);
void ll_popupmenu_bang(t_ll_popupmenu *x);
void ll_popupmenu_items(t_ll_popupmenu *x, t_symbol *s, long argc, t_atom *argv);
void ll_popupmenu_checksymbol(t_ll_popupmenu *x, t_symbol *s);
void ll_popupmenu_paint(t_ll_popupmenu *x, t_object *view);
void ll_popupmenu_getdrawparams(t_ll_popupmenu *x, t_object *patcherview, t_jboxdrawparams *params);
void ll_popupmenu_mousedown(t_ll_popupmenu *x, t_object *patcherview, t_pt pt, long modifiers);
t_max_err ll_popupmenu_getvalue(t_ll_popupmenu *x, long *ac, t_atom **av);
t_max_err ll_popupmenu_setvalue(t_ll_popupmenu *x, long ac, t_atom *av);

t_max_err ll_popupmenu_getitems(t_ll_popupmenu *x, void *attr, long *argc, t_atom **argv);
t_max_err ll_popupmenu_setitems(t_ll_popupmenu *x, void *attr, long argc, t_atom *argv);

void ext_main(void *r)
{
    t_class *c;

    common_symbols_init();

    c = class_new("ll_popupmenu",
                  (method)ll_popupmenu_new,
                  (method)ll_popupmenu_free,
                  sizeof(t_ll_popupmenu),
                  (method)NULL,
                  A_GIMME,
                  0L);
    
    jbox_initclass(c, JBOX_DRAWFIRSTIN | JBOX_FIXWIDTH | JBOX_FONTATTR | JBOX_COLOR);

    c->c_flags |= CLASS_FLAG_NEWDICTIONARY;
    
    class_addmethod(c, (method)ll_popupmenu_paint, "paint", A_CANT, 0);
    
    class_addmethod(c, (method)ll_popupmenu_mousedown, "mousedown", A_CANT, 0);
    class_addmethod(c, (method)ll_popupmenu_bang, "bang", 0);
    class_addmethod(c, (method)ll_popupmenu_items, "items", A_GIMME, 0);
    class_addmethod(c, (method)ll_popupmenu_checksymbol, "checksymbol", A_SYM, 0);
    class_addmethod(c, (method)ll_popupmenu_getdrawparams, "getdrawparams", A_CANT, 0);
    class_addmethod(c, (method)ll_popupmenu_getvalue, "getvalueof", A_CANT, 0);
    class_addmethod(c, (method)ll_popupmenu_setvalue, "setvalueof", A_CANT, 0);

    CLASS_ATTR_DEFAULT(c, "patching_rect", 0, "0. 0. 100. 24.");
    
    CLASS_ATTR_CHAR(c, "drawarrow", 0, t_ll_popupmenu, draw_arrow);
    CLASS_ATTR_STYLE_LABEL(c, "drawarrow", 0, "onoff", "Draw Arrow");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "drawarrow", 0, "1");
    
    // COLOR ATTRIBUTES
    CLASS_STICKY_ATTR(c, "category", 0, "Color");

    CLASS_ATTR_RGBA_LEGACY(c, "bgcolor", "brgb", 0, t_ll_popupmenu, bgcolor);
    CLASS_ATTR_ALIAS(c, "bgcolor", "brgba");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "bgcolor", 0, ".23 .23 .23 1.");
    CLASS_ATTR_STYLE_LABEL(c, "bgcolor", 0, "rgba", "Background Color");

    CLASS_ATTR_RGBA(c, "color", 0, t_ll_popupmenu, textcolor);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "color", 0, "1. 1. 1. 1.");
    CLASS_ATTR_STYLE_LABEL(c, "color", 0, "rgba", "Color");
    CLASS_ATTR_PAINT(c, "color", 0);
    
    CLASS_ATTR_RGBA_LEGACY(c, "bordercolor", "border", 0, t_ll_popupmenu, bordercolor);
    CLASS_ATTR_ALIAS(c, "bordercolor", "borderrgba");
    CLASS_ATTR_DEFAULTNAME_SAVE_PAINT(c, "bordercolor", 0, "0. 0. 0. 0.");
    CLASS_ATTR_STYLE_LABEL(c, "bordercolor", 0, "rgba", "Border Color");
    
    CLASS_STICKY_ATTR_CLEAR(c, "category");

    // font attributes
    CLASS_STICKY_ATTR(c, "category", 0, "Font");

    CLASS_ATTR_DOUBLE(c, "fontsize", 0, t_ll_popupmenu, fontsize);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "fontsize", 0, "11.");
    CLASS_ATTR_STYLE_LABEL(c, "fontsize", 0, "number", "Font Size");
    
    CLASS_ATTR_DOUBLE(c, "menufontsize", 0, t_ll_popupmenu, menufontsize);
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c, "menufontsize", 0, "11.");
    CLASS_ATTR_STYLE_LABEL(c, "menufontsize", 0, "number", "Menu Font Size");

    CLASS_STICKY_ATTR_CLEAR(c, "category");

    // behavior attributes
    CLASS_STICKY_ATTR(c, "category", 0, "Behavior");
    
    CLASS_ATTR_ATOM(c, "items", 0, t_ll_popupmenu, items);
    CLASS_ATTR_ACCESSORS(c, "items", (method)ll_popupmenu_getitems, (method)ll_popupmenu_setitems);
    CLASS_ATTR_LABEL(c, "items", 0, "Popup Menu Items");
    CLASS_ATTR_STYLE_LABEL(c, "items", 0, "text", "Popup Menu Items");
    CLASS_ATTR_SAVE(c, "items", 0);

    // This line marks it as an atomarray-type attribute (like umenu)
    CLASS_ATTR_ATOMARRAY(c, "items", 0);
    
    CLASS_ATTR_SYM(c, "prepend", 0, t_ll_popupmenu, prepend);
    CLASS_ATTR_LABEL(c, "prepend", 0, "Prepend symbol for output");
    CLASS_ATTR_SAVE(c, "prepend", 0);
    
    CLASS_ATTR_CHAR(c,                "positionmode", 0, t_ll_popupmenu, position_mode);
    CLASS_ATTR_STYLE_LABEL(c,        "positionmode", 0, "enum", "positionmode");
    CLASS_ATTR_ENUMINDEX(c,            "positionmode", 0, "Object Mouse");
    CLASS_ATTR_DEFAULT_SAVE_PAINT(c,"positionmode", 0, "0");
    CLASS_STICKY_ATTR_CLEAR(c, "category");
    
    
    class_register(CLASS_BOX, c);
    ll_popupmenu_class = c;
}

// Store incoming list of options
void ll_popupmenu_items(t_ll_popupmenu *x, t_symbol *s, long argc, t_atom *argv)
{
    if (x->items)
        object_free(x->items);
    x->items = atomarray_new(argc, argv);
}

void ll_popupmenu_getdrawparams(t_ll_popupmenu *x, t_object *patcherview, t_jboxdrawparams *params)
{
    params->d_borderthickness = 0;
    params->d_bordercolor     = x->bordercolor;
    params->d_boxfillcolor    = x->bgcolor;
}


// Show popup menu on bang
void ll_popupmenu_bang(t_ll_popupmenu *x)
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
        pt.y = (double)rect.y + rect.height;

        // convert patcher coords → screen coords
        long sx, sy;
        patcherview_canvas_to_screen(view, pt.x, pt.y, &sx, &sy);
        pt.x = (double)sx;
        pt.y = (double)sy;

    }
    
    // show popup menu
    int choice = jpopupmenu_popup(menu, pt, 0);

    if (choice > 0 && choice <= argc) {
        // selection
        t_atom out[2];
        long out_count = 0;

        // index first
        atom_setlong(&out[out_count++], choice);

        // derive symbol string for the chosen item
        t_atom  *a   = argv + (choice - 1);
        t_symbol *sym = NULL;
        char buf[64];

        switch (atom_gettype(a)) {
            case A_SYM:  sym = atom_getsym(a); break;
            case A_LONG: snprintf(buf, sizeof(buf), "%ld", atom_getlong(a)); sym = gensym(buf); break;
            case A_FLOAT:snprintf(buf, sizeof(buf), "%.3f", atom_getfloat(a)); sym = gensym(buf); break;
            default:      sym = gensym(""); break; // treat unknown as empty
        }

        // include symbol ONLY if non-empty and not "<none>"
        if (sym && sym != gensym("") && sym != gensym("<none>")) {
            atom_setsym(&out[out_count++], sym);
        }

        if (x->prepend && x->prepend != gensym("")) {
            outlet_anything(x->outlet, x->prepend, out_count, out);
        } else {
            outlet_list(x->outlet, NULL, out_count, out);
        }

        // store checked as empty or real (never "<none>")
        x->checked = (sym && sym != gensym("<none>")) ? sym : gensym("");
        object_notify(x, _sym_modified, NULL);
        jbox_redraw((t_jbox *)x);

    } else {
        // cancel -> ALWAYS output -1 <none>
        t_atom out[2];
        atom_setlong(&out[0], -1);
        atom_setsym (&out[1], gensym("<none>"));

        if (x->prepend && x->prepend != gensym("")) {
            outlet_anything(x->outlet, x->prepend, 2, out);
        } else {
            outlet_list(x->outlet, NULL, 2, out);
        }

        // do NOT change x->checked on cancel
    }


    if(x->objectclick)
        x->objectclick = 0;

    jpopupmenu_destroy(menu);
}

void ll_popupmenu_free(t_ll_popupmenu *x)
{
    if (x->font)
        jfont_destroy(x->font);
    
    if (x->items)
        object_free(x->items);
    jbox_free((t_jbox *)x);
}

void *ll_popupmenu_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ll_popupmenu *x;
    long flags;
    t_dictionary *d = NULL;

    if (!(d = object_dictionaryarg(argc, argv)))
        return NULL;

    x = (t_ll_popupmenu *)object_alloc(ll_popupmenu_class);
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

    x->prepend = NULL;
    x->checked = NULL;
    x->items   = NULL;
    x->objectclick = 0;

    x->outlet  = outlet_new((t_object *)x, NULL);

    attr_dictionary_process(x, d);

    jbox_ready(&x->j_box);
    return x;
}


void ll_popupmenu_checksymbol(t_ll_popupmenu *x, t_symbol *s)
{
    x->checked = s;
}

void ll_popupmenu_paint(t_ll_popupmenu *x, t_object *view)
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

void ll_popupmenu_mousedown(t_ll_popupmenu *x, t_object *patcherview, t_pt pt, long modifiers)
{
    x->objectclick = 1;
    ll_popupmenu_bang(x);
}

t_max_err ll_popupmenu_getvalue(t_ll_popupmenu *x, long *ac, t_atom **av)
{
    if (!ac || !av)
        return MAX_ERR_NONE;
    char alloc;
    if (atom_alloc_array(1, ac, av, &alloc))
        return MAX_ERR_OUT_OF_MEM;

    // save empty symbol if no selection
    t_symbol *sym = (x->checked && x->checked != gensym("<none>")) ? x->checked : gensym("");
    atom_setsym(*av, sym);
    return MAX_ERR_NONE;
}

t_max_err ll_popupmenu_setvalue(t_ll_popupmenu *x, long ac, t_atom *av)
{
    if (ac && av) {
        t_symbol *sym = atom_getsym(av);
        if (!sym || sym == gensym("<none>"))
            sym = gensym("");   // interpret <none> or NULL as empty
        x->checked = sym;
        jbox_redraw((t_jbox *)x);
    }
    return MAX_ERR_NONE;
}

t_max_err ll_popupmenu_getitems(t_ll_popupmenu *x, void *attr, long *argc, t_atom **argv)
{
    if (!x->items)
        return MAX_ERR_NONE;
    return atomarray_copyatoms(x->items, argc, argv);
}

t_max_err ll_popupmenu_setitems(t_ll_popupmenu *x, void *attr, long argc, t_atom *argv)
{
    if (x->items)
        object_free(x->items);
    x->items = atomarray_new(argc, argv);
    jbox_redraw((t_jbox *)x);
    object_notify(x, gensym("attr_modified"), attr);
    return MAX_ERR_NONE;
}
