// ll_zip.c  — threaded, async, progress-safe version (fixed)

#include "ext.h"
#include "ext_obex.h"
#include "ext_strings.h"
#include "ext_systhread.h"

#include "miniz.h"

#include <dirent.h>
#include <sys/stat.h>

// ===============================
// OBJECT STRUCT
// ===============================
typedef struct _ll_zip {
    t_object ob;
    void *outlet;

    char src[MAX_PATH_CHARS];
    char dst[MAX_PATH_CHARS];

    t_systhread       thread;
    t_systhread_mutex lock;

    long total_files;
    long current_file;
    long last_percent;

    char running;
} t_ll_zip;


// ===============================
// METHOD DECLARATIONS
// ===============================
void *ll_zip_class;

void *ll_zip_new(t_symbol *s, long argc, t_atom *argv);
void ll_zip_free(t_ll_zip *x);

void ll_zip_do(t_ll_zip *x, t_symbol *sym, long argc, t_atom *argv);
void ll_zip_threadproc(t_ll_zip *x);

void ll_zip_bounce(t_ll_zip *x, t_symbol *msg, long argc, t_atom *argv);

t_max_err resolve_path(t_symbol *pathsym, char *outpath);
long count_files_in_folder(const char *folder);

t_max_err zip_add_folder(
    t_ll_zip *x,
    mz_zip_archive *zip,
    const char *root,
    const char *folder,
    size_t rootlen
);


// ===============================
// CLASS SETUP
// ===============================
void ext_main(void *r)
{
    t_class *c = class_new(
        "ll_zip",
        (method)ll_zip_new,
        (method)ll_zip_free,
        sizeof(t_ll_zip),
        0L,
        A_GIMME,
        0
    );

    class_addmethod(c, (method)ll_zip_do, "zip", A_GIMME, 0);

    class_register(CLASS_BOX, c);
    ll_zip_class = c;
}


// ===============================
// NEW / FREE
// ===============================
void *ll_zip_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ll_zip *x = (t_ll_zip *)object_alloc(ll_zip_class);

    if (x) {
        x->outlet = outlet_new((t_object *)x, NULL);
        systhread_mutex_new(&(x->lock), SYSTHREAD_MUTEX_NORMAL);
        x->thread  = NULL;
        x->running = 0;
        x->total_files   = 0;
        x->current_file  = 0;
        x->src[0] = x->dst[0] = 0;
    }
    return x;
}

void ll_zip_free(t_ll_zip *x)
{
    // If a thread is still running, wait for it.
    if (x->thread) {
        unsigned int ret;
        systhread_join(x->thread, &ret);
        x->thread = NULL;
    }
    systhread_mutex_free(x->lock);
}


// ===============================
// PUBLIC: [zip <folder> <output.zip>]
// ===============================
void ll_zip_do(t_ll_zip *x, t_symbol *sym, long argc, t_atom *argv)
{
    x->last_percent = -1;
    if (argc < 2) {
        object_error((t_object *)x, "usage: zip <folder> <output.zip>");
        return;
    }

    if (x->running) {
        object_error((t_object *)x, "already running");
        return;
    }

    // Resolve paths
    resolve_path(atom_getsym(argv),     x->src);
    resolve_path(atom_getsym(argv + 1), x->dst);

    // If dest has no path, put it next to source
    if (!strchr(x->dst, '/') && !strchr(x->dst, ':')) {
        char parent[MAX_PATH_CHARS];
        strncpy_zero(parent, x->src, MAX_PATH_CHARS);

        char *slash = strrchr(parent, '/');
        if (slash)
            *slash = 0;

        snprintf(x->dst, MAX_PATH_CHARS, "%s/%s", parent, atom_getsym(argv + 1)->s_name);
    }

    // Count files for progress
    x->total_files  = count_files_in_folder(x->src);
    x->current_file = 0;
    x->running      = 1;

    // Start worker thread (low priority: -32)
    systhread_create((method)ll_zip_threadproc, x, 0, -32, 0, &x->thread);
}


// ===============================
// THREADPROC — runs ZIP work
// ===============================
void ll_zip_threadproc(t_ll_zip *x)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_file(&zip, x->dst, 0)) {
        // Bounce "error" back to main thread
        defer_low(x, (method)ll_zip_bounce, gensym("error"), 0, NULL);
        x->running = 0;
        x->thread  = NULL;
        return;
    }

    size_t rootlen = strlen(x->src) + 1;

    if (x->total_files <= 0) {
        // Nothing to do, but still create empty archive
        zip_add_folder(x, &zip, x->src, x->src, rootlen);
    } else {
        zip_add_folder(x, &zip, x->src, x->src, rootlen);
    }

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    // Final 100% progress + "done"
    {
        t_atom a;
        atom_setfloat(&a, 100.0);
        defer_low(x, (method)ll_zip_bounce, gensym("progress"), 1, &a);
    }

    defer_low(x, (method)ll_zip_bounce, gensym("done"), 0, NULL);

    x->running = 0;
    x->thread  = NULL;
    return;
}


// ===============================
// RECURSIVE ZIP HELPER
// ===============================
t_max_err zip_add_folder(
    t_ll_zip *x,
    mz_zip_archive *zip,
    const char *root,
    const char *folder,
    size_t rootlen
){
    DIR *dir = opendir(folder);
    if (!dir)
        return MAX_ERR_INVALID_PTR;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir))) {

        if (entry->d_name[0] == '.') {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
        }

        snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            zip_add_folder(x, zip, root, path, rootlen);
        }
        else if (S_ISREG(st.st_mode)) {

            const char *relative = path + rootlen;

            // Add file to archive
            mz_zip_writer_add_file(zip, relative, path, NULL, 0, MZ_BEST_COMPRESSION);

            // Progress increment
            long cur, total;

            systhread_mutex_lock(x->lock);
            x->current_file++;
            cur   = x->current_file;
            total = x->total_files;
            systhread_mutex_unlock(x->lock);

            if (total > 0) {
                double pct = ((double)cur / (double)total) * 100.0;
                long pct_int = (long)pct;   // 0–100 integer percent

                if (pct_int != x->last_percent) {
                    x->last_percent = pct_int;

                    t_atom a;
                    atom_setfloat(&a, pct);

                    defer_low(x, (method)ll_zip_bounce, gensym("progress"), 1, &a);
                }
            }
        }
    }

    closedir(dir);
    return MAX_ERR_NONE;
}


// ===============================
// BOUNCE (main-thread safe outlet)
// ===============================
void ll_zip_bounce(t_ll_zip *x, t_symbol *msg, long argc, t_atom *argv)
{
    // All messages (progress/error/done) funneled here
    outlet_anything(x->outlet, msg, argc, argv);
}


// ===============================
// PATH RESOLVER
// ===============================
t_max_err resolve_path(t_symbol *pathsym, char *outpath)
{
    char temp[MAX_PATH_CHARS];
    short path_id;
    char filename[MAX_FILENAME_CHARS];
    t_fourcc outtype;
    t_max_err err;

    strncpy_zero(temp, pathsym->s_name, MAX_PATH_CHARS);

    // 1. Max-style fully qualified file path
    err = path_frompathname(temp, &path_id, filename);
    if (err == MAX_ERR_NONE)
        return path_toabsolutesystempath(path_id, filename, outpath);

    // 2. Search through Max search paths
    strncpy_zero(temp, pathsym->s_name, MAX_PATH_CHARS);
    err = locatefile_extended(temp, &path_id, &outtype, NULL, 0);

    if (err == MAX_ERR_NONE)
        return path_toabsolutesystempath(path_id, temp, outpath);

    // 3. Fallback: raw OS path
    strncpy_zero(outpath, pathsym->s_name, MAX_PATH_CHARS);
    return MAX_ERR_NONE;
}


// ===============================
// COUNT FILES
// ===============================
long count_files_in_folder(const char *folder)
{
    long count = 0;
    DIR *dir = opendir(folder);
    if (!dir)
        return 0;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir))) {

        if (entry->d_name[0] == '.') {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
        }

        snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
            count += count_files_in_folder(path);
        else if (S_ISREG(st.st_mode))
            count++;
    }

    closedir(dir);
    return count;
}

