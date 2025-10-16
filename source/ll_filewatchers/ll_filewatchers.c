/**
	filewatchers - watch many paths
	joe steccato - joe@joesteccato.com
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

////////////////////////// object struct
typedef struct _filewatchers
{
	t_object	ob;
	t_atom		val;
    char        enabled;
    char        bang_only;
    void		*outlet1;     // Outlet for indices of changed files
    
    t_linklist *watchers;     // List of filewatcher instances
    t_linklist *paths;        // List of paths being watched
} t_filewatchers;

///////////////////////// function prototypes
//// standard set
void *filewatchers_new(t_symbol *s, long argc, t_atom *argv);
void filewatchers_free(t_filewatchers *x);
void filewatchers_assist(t_filewatchers *x, void *b, long m, long a, char *s);

void filewatchers_set_enabled(t_filewatchers *x, void *attr, long argc, t_atom *argv);

void filewatchers_int(t_filewatchers *x, long n);
void filewatchers_bang(t_filewatchers *x);

void filewatchers_list(t_filewatchers *x, t_symbol *s, long argc, t_atom *argv);
void filewatchers_watch(t_filewatchers *x, t_symbol *s, long argc, t_atom *argv);
void filewatchers_append(t_filewatchers *x, t_symbol *s);

void filewatchers_filechanged(t_filewatchers *x, char *filename, short path);
void filewatchers_clear(t_filewatchers *x);

//////////////////////// global class pointer variable
void *filewatchers_class;

void ext_main(void *r)
{
	t_class *c;

	c = class_new("ll_filewatchers", (method)filewatchers_new, (method)filewatchers_free, (long)sizeof(t_filewatchers),
				  0L /* leave NULL!! */, A_GIMME, 0);
    
    class_addmethod(c, (method)filewatchers_list,           "list",         A_GIMME,    0);
    class_addmethod(c, (method)filewatchers_watch,          "watch",        A_GIMME,    0);
    class_addmethod(c, (method)filewatchers_filechanged,    "filechanged",  A_CANT,     0);
    class_addmethod(c, (method)filewatchers_clear,          "clear",                    0);
    class_addmethod(c, (method)filewatchers_append,         "append",       A_SYM,      0);
    
	class_addmethod(c, (method)filewatchers_bang,			"bang",                     0);
	class_addmethod(c, (method)filewatchers_int,			"int",		    A_LONG,     0);

	class_addmethod(c, (method)filewatchers_assist,			"assist",		A_CANT,     0);

    CLASS_ATTR_CHAR(c,                  "bangonly", 0, t_filewatchers, bang_only);
    CLASS_ATTR_STYLE(c,                 "bangonly", 0, "onoff");
    CLASS_ATTR_STYLE_LABEL(c,           "bangonly", 0, "onoff", "Output Bang Only");
    CLASS_ATTR_DEFAULT_SAVE(c,          "bangonly", 0, "0");
    
    CLASS_ATTR_CHAR(c,                  "enabled", 0, t_filewatchers, enabled);
    CLASS_ATTR_ACCESSORS(c,             "enabled", NULL, (method)filewatchers_set_enabled);
    CLASS_ATTR_STYLE(c,                 "enabled", 0, "onoff");
    CLASS_ATTR_STYLE_LABEL(c,           "enabled", 0, "onoff", "Enable");
    CLASS_ATTR_DEFAULT_SAVE(c,          "enabled", 0, "0");

	class_register(CLASS_BOX, c);
	filewatchers_class = c;
}

void filewatchers_assist(t_filewatchers *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { //inlet
		sprintf(s, "List of paths to watch");
	}
	else {	// outlet
		sprintf(s, "Output index of path on change");
	}
}

void filewatchers_free(t_filewatchers *x)
{
    filewatchers_clear(x);
}

void *filewatcher_start_wrapper(void *w) {
    filewatcher_start(w);
    return NULL;
}

void *filewatcher_stop_wrapper(void *w) {
    filewatcher_stop(w);
    return NULL;
}

void filewatchers_set_enabled(t_filewatchers *x, void *attr, long argc, t_atom *argv)
{
    if(argc && argv) { // Check if we have arguments
        long n = atom_getlong(argv); // Get the first argument as long
        x->enabled = (n != 0); // Set enabled status based on argument
        
        if(x->enabled) {
            linklist_funall(x->watchers, filewatcher_start_wrapper, NULL);
        } else {
            linklist_funall(x->watchers, filewatcher_stop_wrapper, NULL);
        }
    }
}

void filewatchers_int(t_filewatchers *x, long n)
{
    t_atom argv; // Create a single t_atom
    atom_setlong(&argv, n); // Set its value to the incoming long
    filewatchers_set_enabled(x, NULL, 1, &argv); // Call the custom setter with this atom
}

void filewatchers_bang(t_filewatchers *x)
{
    t_atom argv; // Create a single t_atom
    long n = !x->enabled; // Toggle the current state
    atom_setlong(&argv, n); // Set the atom to the new value
    filewatchers_set_enabled(x, NULL, 1, &argv); // Use the custom setter
}

void filewatchers_watch(t_filewatchers *x, t_symbol *s, long argc, t_atom *argv){
    filewatchers_list(x, s, argc, argv);
}

void filewatchers_list(t_filewatchers *x, t_symbol *s, long argc, t_atom *argv)
{
    // Clear current watchers and paths
    filewatchers_clear(x);
    
    // Set up new watchers
    for (int i = 0; i < argc; i++) {
        t_symbol *path = atom_getsym(argv+i);
        filewatchers_append(x, path);
    }
}

void filewatchers_append(t_filewatchers *x, t_symbol *s)
{
    t_fourcc outtype;
    char filename[MAX_PATH_CHARS];
    short path;

    strcpy(filename, s->s_name);
    if (locatefile_extended(filename, &path, &outtype, NULL, 0)) {
        object_error((t_object *)x, "Path not found: %s", s->s_name);
        return;
    }

    void* watcher = filewatcher_new((t_object *)x, path, "");
    if (x->enabled)
        filewatcher_start(watcher);

    // Store the watcher
    linklist_append(x->watchers, watcher);

    // Store a dynamically allocated copy of the short
    short *stored_path = (short *)sysmem_newptr(sizeof(short));
    *stored_path = path;
    linklist_append(x->paths, stored_path);
}

void filewatchers_filechanged(t_filewatchers *x, char *filename, short path) 
{
    if(x->bang_only){
        outlet_bang(x->outlet1);
        return;
    }
    void *obj;
    long index = -1;
    long size = linklist_getsize(x->paths);
    for (long i = 0; i < size; i++) {
        short *p = linklist_getindex(x->paths, i);
        if (p && *p == path) {
            index = i;
            break;
        }
    }
    if(index == -1){
        return;
    }
    char fullpath[MAX_PATH_CHARS];
    short path_err = path_topathname(path, filename, fullpath);
    if (path_err) {
        object_error((t_object *)x, "Error resolving path: \n%s  || Error: %ld", filename, path_err);
        return;
    }
    t_atom path_info[2];
    atom_setlong(path_info, index);
    atom_setsym(path_info+1, gensym(fullpath));
    outlet_list(x->outlet1, NULL, 2, path_info);
}

void filewatchers_clear(t_filewatchers *x)
{
    if (x->watchers) {
        void *watcher;
        while ((watcher = linklist_getindex(x->watchers, 0))) {
            filewatcher_stop(watcher);
            object_free(watcher);
            linklist_chuckindex(x->watchers, 0);
        }
    }

    if (x->paths) {
        short *stored_path;
        while (linklist_getsize(x->paths) > 0) {
            stored_path = linklist_getindex(x->paths, 0);
            if (stored_path)
                sysmem_freeptr(stored_path);
            linklist_chuckindex(x->paths, 0);
        }
    }
}

void *filewatchers_new(t_symbol *s, long argc, t_atom *argv)
{
	t_filewatchers *x = NULL;

	if ((x = (t_filewatchers *)object_alloc(filewatchers_class))) {
        attr_args_process(x, argc, argv);
        
        x->outlet1 = outlet_new(x, NULL);
        x->watchers = linklist_new();
        x->paths = linklist_new();
	}
	return (x);
}
