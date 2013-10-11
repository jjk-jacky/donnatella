
#include <locale.h>
#include <gtk/gtk.h>
#include <stdlib.h>     /* free() */
#include <ctype.h>      /* isblank() */
#include <string.h>
#include "donna.h"
#include "debug.h"
#include "app.h"
#include "provider.h"
#include "provider-fs.h"
#include "provider-command.h"
#include "provider-config.h"
#include "provider-task.h"
#include "provider-exec.h"
#include "provider-register.h"
#include "provider-internal.h"
#include "provider-mark.h"
#include "columntype.h"
#include "columntype-name.h"
#include "columntype-size.h"
#include "columntype-time.h"
#include "columntype-perms.h"
#include "columntype-text.h"
#include "columntype-label.h"
#include "columntype-progress.h"
#include "columntype-value.h"
#include "node.h"
#include "filter.h"
#include "sort.h"
#include "command.h"
#include "statusbar.h"
#include "imagemenuitem.h"
#include "misc.h"
#include "util.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_ACTIVE_LIST,
    PROP_JUST_FOCUSED,

    NB_PROPS
};

enum
{
    COL_TYPE_NAME = 0,
    COL_TYPE_SIZE,
    COL_TYPE_TIME,
    COL_TYPE_PERMS,
    COL_TYPE_TEXT,
    COL_TYPE_LABEL,
    COL_TYPE_PROGRESS,
    COL_TYPE_VALUE,
    NB_COL_TYPES
};

enum
{
    TITLE_DOMAIN_LOCATION,
    TITLE_DOMAIN_FULL_LOCATION,
    TITLE_DOMAIN_CUSTOM
};

struct filter
{
    DonnaFilter *filter;
    guint        toggle_count;
    guint        timeout;
};

struct intref
{
    DonnaArgType type;
    gpointer     ptr;
    gint64       last;
};

enum
{
    ST_SCE_DONNA,
    ST_SCE_ACTIVE,
    ST_SCE_FOCUSED,
    ST_SCE_TASK,
};

struct provider
{
    DonnaStatusProvider *sp;
    guint                id;
};

struct status
{
    gchar   *name;
    guint    source;
    GArray  *providers;
};

struct _DonnaDonnaPrivate
{
    GtkWindow       *window;
    GtkWidget       *floating_window;
    gboolean         just_focused;
    gboolean         exiting;
    DonnaConfig     *config;
    DonnaTaskManager*task_manager;
    DonnaStatusBar  *sb;
    GSList          *treeviews;
    GSList          *arrangements;
    GThreadPool     *pool;
    DonnaTreeView   *active_list;
    DonnaTreeView   *focused_tree;
    gulong           sid_active_location;
    GSList          *statuses;
    gchar           *cur_dirname;
    /* visuals are under a RW lock so everyone can read them at the same time
     * (e.g. creating nodes, get_children() & the likes). The write operation
     * should be quite rare. */
    GRWLock          lock;
    GHashTable      *visuals;
    /* ct, providers, filters, intrefs, etc are all under the same lock because
     * there shouldn't be a need to separate them all. We use a recursive mutex
     * because we need it for filters, to handle correctly the toggle_ref */
    GRecMutex        rec_mutex;
    struct col_type
    {
        const gchar     *name;
        GType            type;
        DonnaColumnType *ct;
        gpointer         ct_data;
    } column_types[NB_COL_TYPES];
    GSList          *providers;
    GHashTable      *filters;
    GHashTable      *intrefs;
    guint            intrefs_timeout;
};

struct argmt
{
    gchar        *name;
    GPatternSpec *pspec;
};

static GThread *mt;
static GLogLevelFlags show_log = G_LOG_LEVEL_DEBUG;
guint donna_debug_flags = 0;

static inline void      set_active_list             (DonnaDonna     *donna,
                                                     DonnaTreeView  *list);

static void             donna_donna_log_handler     (const gchar    *domain,
                                                     GLogLevelFlags  log_level,
                                                     const gchar    *message,
                                                     gpointer        data);
static void             donna_donna_set_property    (GObject        *object,
                                                     guint           prop_id,
                                                     const GValue   *value,
                                                     GParamSpec     *pspec);
static void             donna_donna_get_property    (GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
static void             donna_donna_finalize        (GObject        *object);

/* DonnaApp */
static void             donna_donna_ensure_focused  (DonnaApp       *app);
static void             donna_donna_set_floating_window (
                                                     DonnaApp       *app,
                                                     GtkWindow      *window);
static DonnaConfig *    donna_donna_get_config      (DonnaApp       *app);
static DonnaConfig *    donna_donna_peek_config     (DonnaApp       *app);
static DonnaProvider *  donna_donna_get_provider    (DonnaApp       *app,
                                                     const gchar    *domain);
static DonnaColumnType *donna_donna_get_columntype  (DonnaApp       *app,
                                                     const gchar    *type);
static DonnaFilter *    donna_donna_get_filter      (DonnaApp       *app,
                                                     const gchar    *filter);
static void             donna_donna_run_task        (DonnaApp       *app,
                                                     DonnaTask      *task);
static DonnaTaskManager*donna_donna_get_task_manager(DonnaApp       *app);
static DonnaTreeView *  donna_donna_get_treeview    (DonnaApp       *app,
                                                     const gchar    *name);
static gchar *          donna_donna_get_current_dirname (
                                                     DonnaApp       *app);
static gchar *          donna_donna_get_conf_filename (
                                                     DonnaApp       *app,
                                                     const gchar    *fmt,
                                                     va_list         va_args);
static gchar *          donna_donna_new_int_ref     (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
static gpointer         donna_donna_get_int_ref     (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
static gboolean         donna_donna_free_int_ref    (DonnaApp       *app,
                                                     const gchar    *intref);
static gchar *          donna_donna_parse_fl        (DonnaApp       *app,
                                                     gchar          *fl,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data,
                                                     GPtrArray     **intrefs);
static gboolean         donna_donna_trigger_fl      (DonnaApp       *app,
                                                     const gchar    *fl,
                                                     GPtrArray      *intrefs,
                                                     gboolean        blocking,
                                                     GError        **error);
static gboolean         donna_donna_emit_event      (DonnaApp       *app,
                                                     const gchar    *event,
                                                     const gchar    *fmt_source,
                                                     va_list         va_args,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data);
static gboolean         donna_donna_show_menu       (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError       **error);
static void             donna_donna_show_error      (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);
static gpointer         donna_donna_get_ct_data     (DonnaApp       *app,
                                                     const gchar    *col_name);
static DonnaTask *      donna_donna_nodes_io_task   (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     GError        **error);
static gint             donna_donna_ask             (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *btn1_icon,
                                                     const gchar    *btn1_label,
                                                     const gchar    *btn2_icon,
                                                     const gchar    *btn2_label,
                                                     va_list         va_args);
static gchar *          donna_donna_ask_text        (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *main_default,
                                                     const gchar   **other_defaults,
                                                     GError        **error);

static void
donna_donna_app_init (DonnaAppInterface *interface)
{
    interface->ensure_focused       = donna_donna_ensure_focused;
    interface->set_floating_window  = donna_donna_set_floating_window;
    interface->get_config           = donna_donna_get_config;
    interface->peek_config          = donna_donna_peek_config;
    interface->get_provider         = donna_donna_get_provider;
    interface->get_columntype       = donna_donna_get_columntype;
    interface->get_filter           = donna_donna_get_filter;
    interface->run_task             = donna_donna_run_task;
    interface->get_task_manager     = donna_donna_get_task_manager;
    interface->get_treeview         = donna_donna_get_treeview;
    interface->get_current_dirname  = donna_donna_get_current_dirname;
    interface->get_conf_filename    = donna_donna_get_conf_filename;
    interface->new_int_ref          = donna_donna_new_int_ref;
    interface->get_int_ref          = donna_donna_get_int_ref;
    interface->free_int_ref         = donna_donna_free_int_ref;
    interface->parse_fl             = donna_donna_parse_fl;
    interface->trigger_fl           = donna_donna_trigger_fl;
    interface->emit_event           = donna_donna_emit_event;
    interface->show_menu            = donna_donna_show_menu;
    interface->show_error           = donna_donna_show_error;
    interface->get_ct_data          = donna_donna_get_ct_data;
    interface->nodes_io_task        = donna_donna_nodes_io_task;
    interface->ask                  = donna_donna_ask;
    interface->ask_text             = donna_donna_ask_text;
}

static void
donna_donna_class_init (DonnaDonnaClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property   = donna_donna_set_property;
    o_class->get_property   = donna_donna_get_property;
    o_class->finalize       = donna_donna_finalize;

    g_object_class_override_property (o_class, PROP_ACTIVE_LIST, "active-list");
    g_object_class_override_property (o_class, PROP_JUST_FOCUSED, "just-focused");

    g_type_class_add_private (klass, sizeof (DonnaDonnaPrivate));
}

static gboolean
donna_donna_task_run (DonnaTask *task)
{
    donna_task_run (task);
    g_object_unref (task);
    return FALSE;
}

static GSList *
load_arrangements (DonnaConfig *config, const gchar *sce)
{
    GSList      *list   = NULL;
    GPtrArray   *arr    = NULL;
    guint        i;

    if (!donna_config_list_options (config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, sce))
        return NULL;

    for (i = 0; i < arr->len; ++i)
    {
        struct argmt *argmt;
        gchar *mask;

        if (!donna_config_get_string (config, &mask,
                    "%s/%s/mask", sce, arr->pdata[i]))
        {
            g_warning ("Arrangement '%s/%s' has no mask set, skipping",
                    sce, (gchar *) arr->pdata[i]);
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        argmt->name  = g_strdup (arr->pdata[i]);
        argmt->pspec = g_pattern_spec_new (mask);
        list = g_slist_prepend (list, argmt);
        g_free (mask);
    }
    list = g_slist_reverse (list);
    g_ptr_array_free (arr, TRUE);

    return list;
}

struct visuals
{
    gchar *name;
    gchar *icon;
    gchar *box;
    gchar *highlight;
};

static void
free_visuals (struct visuals *visuals)
{
    g_free (visuals->name);
    g_free (visuals->icon);
    g_free (visuals->box);
    g_free (visuals->highlight);
    g_slice_free (struct visuals, visuals);
}

static void
free_filter (struct filter *f)
{
    g_object_unref (f->filter);
    g_free (f);
}

static void
free_intref (struct intref *ir)
{
    if (ir->type & DONNA_ARG_IS_ARRAY)
        g_ptr_array_unref (ir->ptr);
    else if (ir->type == DONNA_ARG_TYPE_TREEVIEW || ir->type == DONNA_ARG_TYPE_NODE)
        g_object_unref (ir->ptr);
    else
        g_warning ("free_intref(): Invalid type: %d", ir->type);

    g_free (ir);
}

static gboolean
copy_and_load_conf (DonnaConfig *config, const gchar *sce, const gchar *dst)
{
    GError *err = NULL;
    gchar buf[255], *b = buf;
    gchar *file = NULL;
    gchar *data;

    if (snprintf (buf, 255, "%s/donnatella/donnatella.conf", sce) >= 255)
        b = g_strdup_printf ("%s/donnatella/donnatella.conf", sce);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP,
            g_debug3 ("Reading '%s'", b));
    if (!g_file_get_contents ((file) ? file : b, &data, NULL, &err))
    {
        g_warning ("Failed to copy configuration from '%s': %s",
                sce, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        return FALSE;
    }
    if (b != buf)
        g_free (b);
    g_free (file);

    if (snprintf (buf, 255, "%s/donnatella/donnatella.conf-ref", dst) >= 255)
        b = g_strdup_printf ("%s/donnatella/donnatella.conf-ref", dst);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to import configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return FALSE;
    }

    /* remove the "-ref" bit */
    b[strlen (b) - 4] = '\0';
    if (file)
        file[strlen (file) - 4] = '\0';

    DONNA_DEBUG (APP,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to write new configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return FALSE;
    }
    if (b != buf)
        g_free (b);
    g_free (file);

    /* takes ownership/will free data */
    donna_config_load_config (config, data);
    return TRUE;
}

/* returns TRUE if file existed (even if loading failed), else FALSE */
static gboolean
load_conf (DonnaConfig *config, const gchar *dir, gboolean is_def)
{
    GError *err = NULL;
    gchar buf[255], *b = buf;
    gchar *file = NULL;
    gchar *data;
    gboolean file_exists = FALSE;

    if (snprintf (buf, 255, "%s/donnatella/donnatella.conf%s",
                dir, (is_def) ? "-def" : "") >= 255)
        b = g_strdup_printf ("%s/donnatella/donnatella.conf%s",
                dir, (is_def) ? "-def" : "");

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP,
            g_debug3 ("Try loading '%s'", b));
    if (g_file_get_contents ((file) ? file : b, &data, NULL, &err))
    {
        file_exists = TRUE;
        if (is_def)
            donna_config_load_config_def (config, data);
        else
            donna_config_load_config (config, data);
    }
    else
    {
        file_exists = !g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        if (file_exists)
            g_warning ((is_def)
                    ? "Unable to load configuration definition from '%s': %s"
                    : "Unable to load configuration from '%s': %s",
                    b, err->message);
        g_clear_error (&err);
    }

    if (b != buf)
        g_free (b);
    g_free (file);
    return file_exists;
}

static void
load_css (const gchar *dir)
{
    GtkCssProvider *css_provider;
    gchar buf[255], *b = buf;
    gchar *file = NULL;

    if (snprintf (buf, 255, "%s/donnatella/donnatella.css", dir) >= 255)
        b = g_strdup_printf ("%s/donnatella/donnatella.css", dir);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    if (!g_file_test ((file) ? file : b, G_FILE_TEST_IS_REGULAR))
    {
        if (b != buf)
            g_free (b);
        g_free (file);
        return;
    }

    DONNA_DEBUG (APP,
            g_debug3 ("Load '%s'", b));
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (css_provider, (file) ? file : b, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
            (GtkStyleProvider *) css_provider,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    if (b != buf)
        g_free (b);
    g_free (file);
}

static void
donna_donna_init (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv;

    mt = g_thread_self ();
    g_log_set_default_handler (donna_donna_log_handler, NULL);

    priv = donna->priv = G_TYPE_INSTANCE_GET_PRIVATE (donna,
            DONNA_TYPE_DONNA, DonnaDonnaPrivate);

    g_rw_lock_init (&priv->lock);
    g_rec_mutex_init (&priv->rec_mutex);

    priv->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, "app", donna, NULL);
    priv->column_types[COL_TYPE_NAME].name = "name";
    priv->column_types[COL_TYPE_NAME].type = DONNA_TYPE_COLUMNTYPE_NAME;
    priv->column_types[COL_TYPE_SIZE].name = "size";
    priv->column_types[COL_TYPE_SIZE].type = DONNA_TYPE_COLUMNTYPE_SIZE;
    priv->column_types[COL_TYPE_TIME].name = "time";
    priv->column_types[COL_TYPE_TIME].type = DONNA_TYPE_COLUMNTYPE_TIME;
    priv->column_types[COL_TYPE_PERMS].name = "perms";
    priv->column_types[COL_TYPE_PERMS].type = DONNA_TYPE_COLUMNTYPE_PERMS;
    priv->column_types[COL_TYPE_TEXT].name = "text";
    priv->column_types[COL_TYPE_TEXT].type = DONNA_TYPE_COLUMNTYPE_TEXT;
    priv->column_types[COL_TYPE_LABEL].name = "label";
    priv->column_types[COL_TYPE_LABEL].type = DONNA_TYPE_COLUMNTYPE_LABEL;
    priv->column_types[COL_TYPE_PROGRESS].name = "progress";
    priv->column_types[COL_TYPE_PROGRESS].type = DONNA_TYPE_COLUMNTYPE_PROGRESS;
    priv->column_types[COL_TYPE_VALUE].name = "value";
    priv->column_types[COL_TYPE_VALUE].type = DONNA_TYPE_COLUMNTYPE_VALUE;

    priv->task_manager = g_object_new (DONNA_TYPE_PROVIDER_TASK, "app", donna, NULL);

    priv->pool = g_thread_pool_new ((GFunc) donna_donna_task_run, NULL,
            5, FALSE, NULL);

    priv->filters = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_filter);

    priv->visuals = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_visuals);

    priv->intrefs = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_intref);
}

G_DEFINE_TYPE_WITH_CODE (DonnaDonna, donna_donna, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_APP, donna_donna_app_init))

static void
donna_donna_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
    DonnaDonnaPrivate *priv = DONNA_DONNA (object)->priv;

    if (prop_id == PROP_ACTIVE_LIST)
        set_active_list ((DonnaDonna *) object, g_value_get_object (value));
    else if (prop_id == PROP_JUST_FOCUSED)
        priv->just_focused = g_value_get_boolean (value);
}

static void
donna_donna_get_property (GObject       *object,
                          guint          prop_id,
                          GValue        *value,
                          GParamSpec    *pspec)
{
    DonnaDonnaPrivate *priv = DONNA_DONNA (object)->priv;

    if (prop_id == PROP_ACTIVE_LIST)
        g_value_set_object (value, priv->active_list);
    else if (prop_id == PROP_JUST_FOCUSED)
        g_value_set_boolean (value, priv->just_focused);
}

static void
free_arrangements (GSList *list)
{
    GSList *l;

    for (l = list; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        g_free (argmt->name);
        g_pattern_spec_free (argmt->pspec);
        g_free (argmt);
    }
    g_slist_free (list);
}

static void
donna_donna_finalize (GObject *object)
{
    DonnaDonnaPrivate *priv;
    guint i;

    priv = DONNA_DONNA (object)->priv;

    g_rw_lock_clear (&priv->lock);
    g_rec_mutex_clear (&priv->rec_mutex);
    g_object_unref (priv->config);
    free_arrangements (priv->arrangements);
    g_hash_table_destroy (priv->filters);
    g_hash_table_destroy (priv->visuals);
    g_hash_table_destroy (priv->intrefs);
    g_thread_pool_free (priv->pool, TRUE, FALSE);

    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (priv->column_types[i].ct_data)
            donna_columntype_free_data (priv->column_types[i].ct,
                    priv->column_types[i].ct_data);
        if (priv->column_types[i].ct)
            g_object_unref (priv->column_types[i].ct);
    }

    G_OBJECT_CLASS (donna_donna_parent_class)->finalize (object);
}

static void
donna_donna_log_handler (const gchar    *domain,
                         GLogLevelFlags  log_level,
                         const gchar    *message,
                         gpointer        data)
{
    GThread *thread = g_thread_self ();
    time_t now;
    struct tm *tm;
    gchar buf[12];
    GString *str;

    if (log_level > show_log)
        return;

    now = time (NULL);
    tm = localtime (&now);
    strftime (buf, 12, "[%H:%M:%S] ", tm);
    str = g_string_new (buf);

    if (thread != mt)
        g_string_append_printf (str, "[thread %p] ", thread);

    if (log_level & G_LOG_LEVEL_ERROR)
        g_string_append (str, "** ERROR: ");
    if (log_level & G_LOG_LEVEL_CRITICAL)
        g_string_append (str, "** CRITICAL: ");
    if (log_level & G_LOG_LEVEL_WARNING)
        g_string_append (str, "WARNING: ");
    if (log_level & G_LOG_LEVEL_MESSAGE)
        g_string_append (str, "MESSAGE: ");
    if (log_level & G_LOG_LEVEL_INFO)
        g_string_append (str, "INFO: ");
    if (log_level & G_LOG_LEVEL_DEBUG)
        g_string_append (str, "DEBUG: ");
    /* custom/user log levels, for extra debug verbosity */
    if (log_level & DONNA_LOG_LEVEL_DEBUG2)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG3)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG4)
        g_string_append (str, "DEBUG: ");

    if (domain)
        g_string_append_printf (str, "[%s] ", domain);

    g_string_append (str,message);
    puts (str->str);
    g_string_free (str, TRUE);
}

void
donna_donna_ensure_focused (DonnaApp *app)
{
    DonnaDonnaPrivate *priv;

    g_return_if_fail (DONNA_IS_DONNA (app));
    priv = ((DonnaDonna *) app)->priv;

    if (!gtk_window_has_toplevel_focus (priv->window))
        gtk_window_present_with_time (priv->window, GDK_CURRENT_TIME);
}

static void
floating_window_destroy_cb (GtkWidget *w, DonnaDonna *donna)
{
    donna->priv->floating_window = NULL;
}

void
donna_donna_set_floating_window (DonnaApp *app, GtkWindow *window)
{
    DonnaDonnaPrivate *priv;

    g_return_if_fail (DONNA_IS_DONNA (app));
    priv = ((DonnaDonna *) app)->priv;

    if (priv->floating_window)
        gtk_widget_destroy (priv->floating_window);

    /* make sure all events are processed before we switch to the new window,
     * otherwise this could lead to immediate destruction of said new floating
     * window */
    while (gtk_events_pending ())
        gtk_main_iteration ();

    priv->floating_window = (GtkWidget *) window;
    g_signal_connect (window, "destroy",
            (GCallback) floating_window_destroy_cb, app);
}

DonnaConfig *
donna_donna_get_config (DonnaApp *app)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    return g_object_ref (DONNA_DONNA (app)->priv->config);
}

DonnaConfig *
donna_donna_peek_config (DonnaApp *app)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    return DONNA_DONNA (app)->priv->config;
}

static gboolean
visual_refresher (DonnaTask *task, DonnaNode *node, const gchar *name)
{
    /* FIXME: should we do something here? */
    return TRUE;
}

static void
new_node_cb (DonnaProvider *provider, DonnaNode *node, DonnaDonna *donna)
{
    gchar *fl;
    struct visuals *visuals;

    fl = donna_node_get_full_location (node);
    g_rw_lock_reader_lock (&donna->priv->lock);
    visuals = g_hash_table_lookup (donna->priv->visuals, fl);
    if (visuals)
    {
        GValue value = G_VALUE_INIT;

        if (visuals->name)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->name);
            donna_node_add_property (node, "visual-name", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }

        if (visuals->icon)
        {
            GdkPixbuf *pb;

            pb = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                    visuals->icon, 16 /* FIXME */, 0, NULL);
            if (pb)
            {
                g_value_init (&value, GDK_TYPE_PIXBUF);
                g_value_take_object (&value, pb);
                donna_node_add_property (node, "visual-icon", GDK_TYPE_PIXBUF, &value,
                        visual_refresher, NULL, NULL);
                g_value_unset (&value);
            }
        }

        if (visuals->box)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->box);
            donna_node_add_property (node, "visual-box", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }

        if (visuals->highlight)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->highlight);
            donna_node_add_property (node, "visual-highlight", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }
    }
    g_rw_lock_reader_unlock (&donna->priv->lock);
    g_free (fl);
}

DonnaProvider *
donna_donna_get_provider (DonnaApp    *app,
                          const gchar *domain)
{
    DonnaDonnaPrivate *priv;
    DonnaProvider *provider = NULL;
    GSList *l;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    priv = ((DonnaDonna *) app)->priv;

    if (streq (domain, "config"))
        return g_object_ref (priv->config);
    else if (streq (domain, "task"))
        return g_object_ref (priv->task_manager);

    g_rec_mutex_lock (&priv->rec_mutex);
    for (l = priv->providers; l; l = l->next)
        if (streq (domain, donna_provider_get_domain (l->data)))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return g_object_ref (l->data);
        }

    if (streq (domain, "fs"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_FS, "app", app, NULL);
    else if (streq (domain, "command"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_COMMAND, "app", app, NULL);
    else if (streq (domain, "exec"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_EXEC, "app", app, NULL);
    else if (streq (domain, "register"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_REGISTER, "app", app, NULL);
    else if (streq (domain, "internal"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_INTERNAL, "app", app, NULL);
    else if (streq (domain, "mark"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_MARK, "app", app, NULL);
    else
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return NULL;
    }

    g_signal_connect (provider, "new-node", (GCallback) new_node_cb, app);
    priv->providers = g_slist_prepend (priv->providers, provider);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return g_object_ref (provider);
}

DonnaColumnType *
donna_donna_get_columntype (DonnaApp       *app,
                            const gchar    *type)
{
    DonnaDonnaPrivate *priv;
    gint i;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    priv = DONNA_DONNA (app)->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            if (!priv->column_types[i].ct)
                priv->column_types[i].ct = g_object_new (
                        priv->column_types[i].type, "app", app, NULL);
            break;
        }
    }
    g_rec_mutex_unlock (&priv->rec_mutex);
    return (i < NB_COL_TYPES) ? g_object_ref (priv->column_types[i].ct) : NULL;
}

struct filter_toggle
{
    DonnaDonna *donna;
    gchar *filter_str;
};

static void
free_filter_toggle (struct filter_toggle *t)
{
    g_free (t->filter_str);
    g_free (t);
}

static gboolean
filter_remove (struct filter_toggle *t)
{
    DonnaDonnaPrivate *priv = t->donna->priv;
    struct filter *f;

    g_rec_mutex_lock (&priv->rec_mutex);
    f = g_hash_table_lookup (priv->filters, t->filter_str);
    if (f->toggle_count > 0)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return FALSE;
    }

    /* will also unref filter */
    g_hash_table_remove (priv->filters, t->filter_str);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return FALSE;
}

/* see node_toggle_ref_cb() in provider-base.c for more. Here we only add a
 * little extra: we don't unref/remove the filter (from hashtable) right away,
 * but after a little delay.
 * Mostly useful since on each location change/new arrangement, all color
 * filters are let go, then loaded again (assuming they stay active). */
static void
filter_toggle_ref_cb (DonnaDonna *donna, DonnaFilter *filter, gboolean is_last)
{
    DonnaDonnaPrivate *priv = donna->priv;
    struct filter *f;
    gchar *filter_str;

    g_rec_mutex_lock (&priv->rec_mutex);
    /* can NOT use g_object_get, as it takes a ref on the object! */
    filter_str = donna_filter_get_filter (filter);
    f = g_hash_table_lookup (priv->filters, filter_str);
    if (is_last)
    {
        struct filter_toggle *t;

        if (f->timeout)
            g_source_remove (f->timeout);
        if (--f->toggle_count > 0)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (filter_str);
            return;
        }
        t = g_new (struct filter_toggle, 1);
        t->donna = donna;
        t->filter_str = filter_str;
        f->timeout = g_timeout_add_seconds_full (G_PRIORITY_LOW,
                60 * 15, /* 15min */
                (GSourceFunc) filter_remove,
                t, (GDestroyNotify) free_filter_toggle);
    }
    else
    {
        ++f->toggle_count;
        if (f->timeout)
        {
            g_source_remove (f->timeout);
            f->timeout = 0;
        }
        g_free (filter_str);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);
}

DonnaFilter *
donna_donna_get_filter (DonnaApp    *app,
                        const gchar *filter)
{
    DonnaDonnaPrivate *priv;
    struct filter *f;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    priv = ((DonnaDonna *) app)->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    f = g_hash_table_lookup (priv->filters, filter);
    if (!f)
    {
        f = g_new (struct filter, 1);
        f->filter = g_object_new (DONNA_TYPE_FILTER,
                "app",      app,
                "filter",   filter,
                NULL);
        f->toggle_count = 1;
        f->timeout = 0;
        /* add a toggle ref, which adds a strong ref to filter */
        g_object_add_toggle_ref ((GObject *) f->filter,
                (GToggleNotify) filter_toggle_ref_cb, app);
        g_hash_table_insert (priv->filters, g_strdup (filter), f);
    }
    else
        g_object_ref (f->filter);
    g_rec_mutex_unlock (&priv->rec_mutex);

    return f->filter;
}

void
donna_donna_run_task (DonnaApp    *app,
                      DonnaTask   *task)
{
    DonnaTaskVisibility visibility;

    g_return_if_fail (DONNA_IS_DONNA (app));

    donna_task_prepare (task);
    g_object_get (task, "visibility", &visibility, NULL);
    if (visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI)
        g_main_context_invoke (NULL, (GSourceFunc) donna_donna_task_run,
                g_object_ref_sink (task));
    else if (visibility == DONNA_TASK_VISIBILITY_INTERNAL_FAST)
        donna_donna_task_run (g_object_ref_sink (task));
    else if (visibility == DONNA_TASK_VISIBILITY_PULIC)
        donna_task_manager_add_task (((DonnaDonna *) app)->priv->task_manager,
                task, NULL);
    else
        g_thread_pool_push (DONNA_DONNA (app)->priv->pool,
                g_object_ref_sink (task), NULL);
}

static DonnaArrangement *
tree_select_arrangement (DonnaTreeView  *tree,
                         const gchar    *tv_name,
                         DonnaNode      *node,
                         DonnaDonna     *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;
    DonnaArrangement *arr = NULL;
    GSList *list, *l;
    gchar _source[255];
    gchar *source[] = { _source, "arrangements" };
    guint i, max = sizeof (source) / sizeof (source[0]);
    DonnaEnabledTypes type;
    gboolean is_first = TRUE;
    gchar buf[255], *b = buf;
    gchar *location;

    if (!node)
        return NULL;

    if (snprintf (source[0], 255, "treeviews/%s/arrangements", tv_name) >= 255)
        source[0] = g_strdup_printf ("treeviews/%s/arrangements", tv_name);

    for (i = 0; i < max; ++i)
    {
        gchar *sce;

        sce = source[i];
        if (donna_config_has_category (priv->config, sce))
        {
            if (!donna_config_get_int (priv->config, (gint *) &type, "%s/type",
                        sce))
                type = DONNA_ENABLED_TYPE_ENABLED;
            switch (type)
            {
                case DONNA_ENABLED_TYPE_ENABLED:
                case DONNA_ENABLED_TYPE_COMBINE:
                    /* process */
                    break;

                case DONNA_ENABLED_TYPE_DISABLED:
                    /* flag to stop */
                    i = max;
                    break;

                case DONNA_ENABLED_TYPE_IGNORE:
                    /* next source */
                    continue;

                case DONNA_ENABLED_TYPE_UNKNOWN:
                default:
                    g_warning ("Unable to load arrangements: Invalid option '%s/type'",
                            sce);
                    /* flag to stop */
                    i = max;
                    break;
            }
        }
        else
            /* next source */
            continue;

        if (i == max)
            break;

        if (i == 0)
        {
            list = g_object_get_data ((GObject *) tree, "arrangements-masks");
            if (!list)
            {
                list = load_arrangements (priv->config, sce);
                g_object_set_data_full ((GObject *) tree, "arrangements-masks",
                        list, (GDestroyNotify) free_arrangements);
            }
        }
        else
            list = priv->arrangements;

        if (is_first)
        {
            /* get full location of node, with an added / at the end so mask can
             * easily be made for a folder & its subfodlers */
            location = donna_node_get_location (node);
            if (snprintf (buf, 255, "%s:%s/",
                        donna_node_get_domain (node), location) >= 255)
                b = g_strdup_printf ("%s:%s/",
                        donna_node_get_domain (node), location);
            g_free (location);
            is_first = FALSE;
        }

        for (l = list; l; l = l->next)
        {
            struct argmt *argmt = l->data;

            if (g_pattern_match_string (argmt->pspec, b))
            {
                if (!arr)
                {
                    arr = g_new0 (DonnaArrangement, 1);
                    arr->priority = DONNA_ARRANGEMENT_PRIORITY_NORMAL;
                }

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS))
                    donna_config_arr_load_columns (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SORT))
                    donna_config_arr_load_sort (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT))
                    donna_config_arr_load_second_sort (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS))
                    donna_config_arr_load_columns_options (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
                    donna_config_arr_load_color_filters (priv->config,
                            (DonnaApp *) donna, arr,
                            "%s/%s", sce, argmt->name);

                if ((arr->flags & DONNA_ARRANGEMENT_HAS_ALL) == DONNA_ARRANGEMENT_HAS_ALL)
                    break;
            }
        }
        /* at this point type can only be ENABLED or COMBINE */
        if (type == DONNA_ENABLED_TYPE_ENABLED || (arr /* could still be NULL */
                    /* even in COMBINE, if arr is "full" we're done */
                    && (arr->flags & DONNA_ARRANGEMENT_HAS_ALL) == DONNA_ARRANGEMENT_HAS_ALL))
            break;
    }

    /* special: color filters might have been loaded with a type COMBINE, which
     * resulted in them loaded but no flag set (in order to keep loading others
     * from other arrangements). We still don't set the flag, so that treeview
     * can keep combining with its own color filters */

    if (b != buf)
        g_free (b);
    if (source[0] != _source)
        g_free (source[0]);
    return arr;
}

static DonnaTaskManager *
donna_donna_get_task_manager (DonnaApp *app)
{
    DonnaDonnaPrivate *priv;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    priv = ((DonnaDonna *) app)->priv;

    return priv->task_manager;
}

static DonnaTreeView *
donna_load_treeview (DonnaDonna *donna, const gchar *name)
{
    DonnaTreeView *tree;

    tree = donna_donna_get_treeview ((DonnaApp *) donna, name);
    if (!tree)
    {
        /* shall we load it indeed */
        tree = (DonnaTreeView *) donna_tree_view_new ((DonnaApp *) donna, name);
        if (tree)
        {
            g_signal_connect (tree, "select-arrangement",
                    G_CALLBACK (tree_select_arrangement), donna);
            donna->priv->treeviews = g_slist_prepend (donna->priv->treeviews,
                    g_object_ref (tree));
            donna_app_treeview_loaded ((DonnaApp *) donna, tree);
        }
    }
    return tree;
}

static DonnaTreeView *
donna_donna_get_treeview (DonnaApp       *app,
                          const gchar    *name)
{
    DonnaDonnaPrivate *priv;
    DonnaTreeView *tree = NULL;
    GSList *l;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    priv = ((DonnaDonna *) app)->priv;
    for (l = priv->treeviews; l; l = l->next)
    {
        if (streq (name, donna_tree_view_get_name (l->data)))
        {
            tree = g_object_ref (l->data);
            break;
        }
    }
    return tree;
}

static gboolean
intrefs_remove (gchar *key, struct intref *ir, gpointer data)
{
    /* remove after 15min */
    return ir->last + (G_USEC_PER_SEC * 60 * 15) <= g_get_monotonic_time ();
}

static gboolean
intrefs_gc (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;
    gboolean keep_going;

    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_foreach_remove (priv->intrefs, (GHRFunc) intrefs_remove, NULL);
    keep_going = g_hash_table_size (priv->intrefs) > 0;
    if (!keep_going)
        priv->intrefs_timeout = 0;
    g_rec_mutex_unlock (&priv->rec_mutex);

    return keep_going;
}

static gchar *
donna_donna_get_current_dirname (DonnaApp       *app)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    return g_strdup (((DonnaDonna *) app)->priv->cur_dirname);
}

static gchar *
donna_donna_get_conf_filename (DonnaApp       *app,
                               const gchar    *fmt,
                               va_list         va_args)
{
    GString *str;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    str = g_string_new (g_get_user_config_dir ());
    g_string_append (str, "/donnatella/");
    g_string_append_vprintf (str, fmt, va_args);

    if (!g_get_filename_charsets (NULL))
    {
        gchar *s;
        s = g_filename_from_utf8 (str->str, -1, NULL, NULL, NULL);
        if (s)
        {
            g_string_free (str, TRUE);
            return s;
        }
    }

    return g_string_free (str, FALSE);
}

static gchar *
donna_donna_parse_fl (DonnaApp       *app,
                      gchar          *_fl,
                      const gchar    *conv_flags,
                      conv_flag_fn    conv_fn,
                      gpointer        conv_data,
                      GPtrArray     **intrefs)
{
    GString *str = NULL;
    gchar *fl = _fl;
    gchar *s = fl;

    if (G_UNLIKELY (!conv_flags || !conv_fn))
        return _fl;

    while ((s = strchr (s, '%')))
    {
        gboolean dereference = s[1] == '*';
        gboolean match;

        if (!dereference)
            match = s[1] != '\0' && strchr (conv_flags, s[1]) != NULL;
        else
            match = s[2] != '\0' && strchr (conv_flags, s[2]) != NULL;

        if (match)
        {
            DonnaArgType type;
            gpointer ptr;
            GDestroyNotify destroy = NULL;

            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            if (dereference)
                ++s;

            if (G_UNLIKELY (!conv_fn (s[1], &type, &ptr, &destroy, conv_data)))
            {
                fl = ++s;
                ++s;
                continue;
            }

            /* we don't need to test for all possible types, only those can make
             * sense. That is, it could be a ROW, but not a ROW_ID (or PATH)
             * since those only make sense the other way around (or as type of
             * ROW_ID) */

            if (type & DONNA_ARG_TYPE_TREEVIEW)
                g_string_append (str, donna_tree_view_get_name ((DonnaTreeView *) ptr));
            else if (type & DONNA_ARG_TYPE_ROW)
            {
                DonnaTreeRow *row = (DonnaTreeRow *) ptr;
                g_string_append_printf (str, "[%p;%p]", row->node, row->iter);
            }
            /* this will do nodes, array of nodes, array of strings */
            else if (type & (DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY))
            {
                if (dereference)
                {
                    if (type & DONNA_ARG_IS_ARRAY)
                    {
                        GString *str_arr;
                        GPtrArray *arr = (GPtrArray *) ptr;
                        guint i;

                        str_arr = g_string_new (NULL);
                        if (type & DONNA_ARG_TYPE_NODE)
                            for (i = 0; i < arr->len; ++i)
                            {
                                gchar *fl;
                                fl = donna_node_get_full_location (
                                        (DonnaNode *) arr->pdata[i]);
                                donna_g_string_append_quoted (str_arr, fl);
                                g_string_append_c (str_arr, ',');
                                g_free (fl);
                            }
                        else
                            for (i = 0; i < arr->len; ++i)
                            {
                                donna_g_string_append_quoted (str_arr,
                                        (gchar *) arr->pdata[i]);
                                g_string_append_c (str_arr, ',');
                            }

                        /* remove last comma */
                        g_string_truncate (str_arr, str_arr->len - 1);
                        /* str_arr is a list of quoted strings/FL, but we also
                         * need to quote the list itself */
                        donna_g_string_append_quoted (str, str_arr->str);
                        g_string_free (str_arr, TRUE);
                    }
                    else
                    {
                        gchar *fl;
                        fl = donna_node_get_full_location ((DonnaNode *) ptr);
                        donna_g_string_append_quoted (str, fl);
                        g_free (fl);
                    }
                }
                else
                {
                    gchar *s = donna_app_new_int_ref (app, type, ptr);
                    g_string_append (str, s);
                    if (intrefs)
                    {
                        if (!*intrefs)
                            *intrefs = g_ptr_array_new_with_free_func (g_free);
                        g_ptr_array_add (*intrefs, s);
                    }
                    else
                        g_free (s);
                }
            }
            else if (type & DONNA_ARG_TYPE_STRING)
                donna_g_string_append_quoted (str, (gchar *) ptr);
            else if (type & DONNA_ARG_TYPE_INT)
                g_string_append_printf (str, "%d", * (gint *) ptr);

            if (destroy)
                destroy (ptr);

            s += 2;
            fl = s;
        }
        else if (s[1] != '\0')
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            fl = ++s;
            ++s;
        }
        else
            break;
    }

    if (!str)
        return _fl;

    g_string_append (str, fl);
    g_free (_fl);
    return g_string_free (str, FALSE);
}

struct fir
{
    gboolean is_stack;
    DonnaApp *app;
    GPtrArray *intrefs;
};

static void
free_fir (struct fir *fir)
{
    guint i;

    if (fir->intrefs)
    {
        for (i = 0; i < fir->intrefs->len; ++i)
            donna_app_free_int_ref (fir->app, fir->intrefs->pdata[i]);
        g_ptr_array_unref (fir->intrefs);
    }
    if (!fir->is_stack)
        g_free (fir);
}

static void
trigger_cb (DonnaTask *task, gboolean timeout_called, struct fir *fir)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (fir->app, donna_task_get_error (task),
                "Action trigger failed");
    if (fir)
        free_fir (fir);
}

static gboolean
donna_donna_trigger_fl (DonnaApp     *app,
                        const gchar  *fl,
                        GPtrArray    *intrefs,
                        gboolean      blocking,
                        GError      **error)
{
    DonnaNode *node;
    DonnaTask *task;

    node = donna_app_get_node (app, fl, error);
    if (!node)
        return FALSE;

    task = donna_node_trigger_task (node, error);
    if (G_UNLIKELY (!task))
    {
        g_prefix_error (error, "Failed to trigger '%s': ", fl);
        g_object_unref (node);
        return FALSE;
    }
    g_object_unref (node);

    if (blocking)
        g_object_ref (task);
    else
    {
        struct fir *fir;
        fir = g_new0 (struct fir, 1);
        fir->app = app;
        fir->intrefs = intrefs;
        donna_task_set_callback (task, (task_callback_fn) trigger_cb, fir, NULL);
    }

    donna_app_run_task (app, task);

    if (blocking)
    {
        struct fir fir = { TRUE, app, intrefs };
        gboolean ret;

        donna_task_wait_for_it (task, NULL, NULL);
        ret = donna_task_get_state (task) == DONNA_TASK_DONE;
        g_object_unref (task);
        free_fir (&fir);
        return ret;
    }

    return TRUE;
}

static gint
arr_str_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (* (const gchar **) a, * (const gchar **) b);
}

static void
trigger_event (DonnaDonna   *donna,
               const gchar  *event,
               const gchar  *source,
               const gchar  *conv_flags,
               conv_flag_fn  conv_fn,
               gpointer      conv_data)
{
    GPtrArray *arr = NULL;
    guint i;

    if (!donna_config_list_options (donna->priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_OPTION, "%s/events/%s", source, event))
        return;

    g_ptr_array_sort (arr, arr_str_cmp);
    for (i = 0; i < arr->len; ++i)
    {
        GError *err = NULL;
        GPtrArray *intrefs = NULL;
        gchar *fl;

        if (donna_config_get_string (donna->priv->config, &fl, "%s/events/%s/%s",
                    source, event, arr->pdata[i]))
        {
            fl = donna_donna_parse_fl ((DonnaApp *) donna, fl,
                    conv_flags, conv_fn, conv_data, &intrefs);
            if (!donna_donna_trigger_fl ((DonnaApp *) donna, fl, intrefs,
                        FALSE, &err))
            {
                donna_app_show_error ((DonnaApp *) donna, err,
                        "Event '%s': Failed to trigger '%s'%s%s%s",
                        event, arr->pdata[i],
                        (*source != '\0') ? " from '" : "",
                        (*source != '\0') ? source : "",
                        (*source != '\0') ? "'" : "");
                g_clear_error (&err);
            }
            g_free (fl);
        }
    }

    g_ptr_array_unref (arr);
}

static gboolean
donna_donna_emit_event (DonnaApp        *app,
                        const gchar     *event,
                        const gchar     *fmt_source,
                        va_list          va_args,
                        const gchar     *conv_flags,
                        conv_flag_fn     conv_fn,
                        gpointer         conv_data)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), FALSE);

    if (fmt_source)
    {
        gchar *s = g_strdup_vprintf (fmt_source, va_args);
        trigger_event ((DonnaDonna *) app, event, s,
                conv_flags, conv_fn, conv_data);
        g_free (s);
    }

    trigger_event ((DonnaDonna *) app, event, "",
            conv_flags, conv_fn, conv_data);

    return TRUE;
}

static gchar *
donna_donna_new_int_ref (DonnaApp       *app,
                         DonnaArgType    type,
                         gpointer        ptr)
{
    DonnaDonnaPrivate *priv;
    struct intref *ir;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    priv = ((DonnaDonna *) app)->priv;

    ir = g_new (struct intref, 1);
    ir->type = type;
    if (type & DONNA_ARG_IS_ARRAY)
        ir->ptr = g_ptr_array_ref (ptr);
    else if (type & (DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_TYPE_NODE))
        ir->ptr = g_object_ref (ptr);
    else
        ir->ptr = ptr;
    ir->last = g_get_monotonic_time ();

    s = g_strdup_printf ("<%u%u>", rand (), (guint) (gintptr) ir);
    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_insert (priv->intrefs, g_strdup (s), ir);
    if (priv->intrefs_timeout == 0)
        priv->intrefs_timeout = g_timeout_add_seconds_full (G_PRIORITY_LOW,
                60 * 15, /* 15min */
                (GSourceFunc) intrefs_gc, app, NULL);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return s;
}

static gpointer
donna_donna_get_int_ref (DonnaApp       *app,
                         const gchar    *intref,
                         DonnaArgType    type)
{
    DonnaDonnaPrivate *priv;
    struct intref *ir;
    gpointer ptr = NULL;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    priv = ((DonnaDonna *) app)->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    ir = g_hash_table_lookup (priv->intrefs, intref);
    if (ir && ir->type == type)
    {
        ir->last = g_get_monotonic_time ();
        ptr = ir->ptr;
        if (ir->type & DONNA_ARG_IS_ARRAY)
            ptr = g_ptr_array_ref (ptr);
        else if (ir->type & (DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_TYPE_NODE))
            ptr = g_object_ref (ptr);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    return ptr;
}

static gboolean
donna_donna_free_int_ref (DonnaApp       *app,
                          const gchar    *intref)
{
    DonnaDonnaPrivate *priv;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    priv = ((DonnaDonna *) app)->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    /* frees key & value */
    ret = g_hash_table_remove (priv->intrefs, intref);
    g_rec_mutex_unlock (&priv->rec_mutex);

    return ret;
}

struct menu_click
{
    DonnaDonna      *donna;
    /* options are loaded, but this is used when processing clicks */
    gchar           *name;
    /* this is only used to hold references to the nodes for the menu */
    GPtrArray       *nodes;
    /* should icons be features on menuitems? */
    guint            show_icons             : 1;
    /* default to file/folder icon based on item/container if no icon set */
    guint            use_default_icons      : 1;
    /* are containers just items, submenus, or both combined? */
    guint            submenus               : 2;
    /* can children override submenus */
    guint            can_children_submenus  : 1;
    /* can children override menu definition */
    guint            can_children_menu      : 1;
    /* type of nodes to load in submenus */
    DonnaNodeType    node_type              : 2;
    /* do we "show" dot files in submenus */
    guint            show_hidden            : 1;
    /* sort options */
    guint            is_sorted              : 1;
    guint            container_first        : 1;
    guint            is_locale_based        : 1;
    DonnaSortOptions options                : 5;
    guint            sort_special_first     : 1;
};

static void
free_menu_click (struct menu_click *mc)
{
    if (mc->nodes)
        g_ptr_array_unref (mc->nodes);
    g_free (mc->name);
    g_slice_free (struct menu_click, mc);
}

static gboolean
menu_conv_flag (const gchar      c,
                DonnaArgType    *type,
                gpointer        *ptr,
                GDestroyNotify  *destroy,
                DonnaNode       *node)
{
    switch (c)
    {
        case 'N':
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = donna_node_get_location (node);
            *destroy = g_free;
            return TRUE;

        case 'n':
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = node;
            return TRUE;
    }
    return FALSE;
}

static gboolean menuitem_button_release_cb (GtkWidget           *item,
                                            GdkEventButton      *event,
                                            struct menu_click   *mc);

static void
menuitem_activate_cb (GtkWidget *item, struct menu_click *mc)
{
    GdkEventButton event;

    /* because GTK emit "activate" when selecting an item with a submenu, for
     * some reason */
    if (gtk_menu_item_get_submenu (((GtkMenuItem *) item)))
        return;

    event.state = 0;
    event.button = 1;

    menuitem_button_release_cb (item, &event, mc);
}

struct menu_trigger
{
    DonnaApp *app;
    gchar *fl;
    GPtrArray *intrefs;
};

static gboolean
menu_trigger (struct menu_trigger *mt)
{
    donna_app_trigger_fl (mt->app, mt->fl, mt->intrefs, FALSE, NULL);
    g_slice_free (struct menu_trigger, mt);
    return FALSE;
}

static gboolean
menuitem_button_release_cb (GtkWidget           *item,
                            GdkEventButton      *event,
                            struct menu_click   *mc)
{
    DonnaDonnaPrivate *priv = mc->donna->priv;
    DonnaNode *node;
    struct menu_trigger *mt;
    GPtrArray *intrefs = NULL;
    gchar *fl = NULL;
    /* longest possible is "ctrl_shift_middle_click" (len=23) */
    gchar buf[24];
    gchar *b = buf;

    /* we process it now, let's make sure activate isn't triggered; It is there
     * for when user press Enter */
    g_signal_handlers_disconnect_by_func (item, menuitem_activate_cb, mc);

    node = g_object_get_data ((GObject *) item, "node");
    if (!node)
        return FALSE;

    if (event->state & GDK_CONTROL_MASK)
    {
        strcpy (b, "ctrl_");
        b += 5;
    }
    if (event->state & GDK_SHIFT_MASK)
    {
        strcpy (b, "shift_");
        b += 6;
    }
    if (event->button == 1)
    {
        strcpy (b, "left_");
        b += 5;
    }
    else if (event->button == 2)
    {
        strcpy (b, "middle_");
        b += 7;
    }
    else if (event->button == 3)
    {
        strcpy (b, "right_");
        b += 6;
    }
    else
        return FALSE;

    strcpy (b, "click");

    if (!donna_config_get_string (priv->config, &fl, "menus/%s/%s",
                mc->name, buf))
        donna_config_get_string (priv->config, &fl, "defaults/menus/%s", buf);

    if (!fl)
    {
        if (streq (buf, "left_click"))
            /* hard-coded default for sanity */
            fl = g_strdup ("command:node_trigger (%n)");
        else
            return FALSE;
    }

    fl = donna_app_parse_fl ((DonnaApp *) mc->donna, fl, "nN",
            (conv_flag_fn) menu_conv_flag, node, &intrefs);

    /* we use an idle source to trigger it, because otherwise this could lead to
     * e.g. ask the user something (e.g. @ask_text) which would start its own
     * main loop, all that from this thread, so as a result the menu wouldn't be
     * closed (since the event hasn't finished being processed) */
    mt = g_slice_new (struct menu_trigger);
    mt->app = (DonnaApp *) mc->donna;
    mt->fl = fl;
    mt->intrefs = intrefs;
    g_idle_add ((GSourceFunc) menu_trigger, mt);

    return FALSE;
}

static gint
node_cmp (gconstpointer n1, gconstpointer n2, struct menu_click *mc)
{
    DonnaNode *node1 = * (DonnaNode **) n1;
    DonnaNode *node2 = * (DonnaNode **) n2;
    gchar *name1;
    gchar *name2;
    gint ret;

    if (!node1)
        return (node2) ? -1 : 0;
    else if (!node2)
        return 1;

    if (mc->container_first)
    {
        gboolean is_container1;
        gboolean is_container2;

        is_container1 = donna_node_get_node_type (node1) == DONNA_NODE_CONTAINER;
        is_container2 = donna_node_get_node_type (node2) == DONNA_NODE_CONTAINER;

        if (is_container1)
        {
            if (!is_container2)
            return -1;
        }
        else if (is_container2)
            return 1;
    }

    name1 = donna_node_get_name (node1);
    name2 = donna_node_get_name (node2);

    if (mc->is_locale_based)
    {
        gchar *key1;
        gchar *key2;

        key1 = donna_sort_get_utf8_collate_key (name1, -1,
                mc->options & DONNA_SORT_DOT_FIRST,
                mc->sort_special_first,
                mc->options & DONNA_SORT_NATURAL_ORDER);
        key2 = donna_sort_get_utf8_collate_key (name2, -1,
                mc->options & DONNA_SORT_DOT_FIRST,
                mc->sort_special_first,
                mc->options & DONNA_SORT_NATURAL_ORDER);

        ret = strcmp (key1, key2);

        g_free (key1);
        g_free (key2);
        g_free (name1);
        g_free (name2);
        return ret;
    }

    ret = donna_strcmp (name1, name2, mc->options);

    g_free (name1);
    g_free (name2);
    return ret;
}

struct load_submenu
{
    struct menu_click   *mc;
    /* whether we own the mc (newly allocated), or it's just a pointer to our
     * parent (therefore we need to make a copy when loading the submenu) */
    gboolean             own_mc;
    /* mc for submenu/children (if already allocated, else copy mc) */
    struct menu_click   *sub_mc;
    /* parent menu item */
    GtkMenuItem         *item;
    /* get_children task, to cancel on item's destroy */
    DonnaTask           *task;
    /* one for item, one for task */
    guint                ref_count;
    /* if not, must be free-d. Else it's on stack, also we block the task */
    gboolean             blocking;
    /* when item is destroyed, in case task is still running/being cancelled */
    gboolean             invalid;
};

static void
free_load_submenu (struct load_submenu *ls)
{
    if (g_atomic_int_dec_and_test (&ls->ref_count))
    {
        if (ls->own_mc)
            free_menu_click (ls->mc);
        if (!ls->blocking)
            /* if not blocking it was on stack */
            g_slice_free (struct load_submenu, ls);
    }
}

static void
item_destroy_cb (struct load_submenu *ls)
{
    ls->invalid = TRUE;
    if (ls->task)
        donna_task_cancel (ls->task);
    free_load_submenu (ls);
}

static GtkWidget * load_menu (struct menu_click *mc);

static void
submenu_get_children_cb (DonnaTask           *task,
                         gboolean             timeout_called,
                         struct load_submenu *ls)
{
    GtkWidget *menu;
    GPtrArray *arr;
    struct menu_click *mc;
    gboolean is_selected;

    if (ls->invalid)
    {
        free_load_submenu (ls);
        return;
    }
    ls->task = NULL;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        const GError *error;
        GtkWidget *w;

        error = donna_task_get_error (task);
        menu = gtk_menu_new ();
        w = gtk_image_menu_item_new_with_label (
                (error) ? error->message : "Failed to load children");
        gtk_widget_set_sensitive (w, FALSE);
        gtk_menu_attach ((GtkMenu *) menu, w, 0, 1, 0, 1);
        gtk_widget_show (w);
        goto set_menu;
    }

    arr = g_value_get_boxed (donna_task_get_return_value (task));
    if (!ls->mc->show_hidden)
    {
        GPtrArray *filtered = NULL;
        guint i;

        /* arr is owned by the task, we shouldn't modify it. (It could also be
         * used by e.g. a treeview to refresh its content) */
        filtered = g_ptr_array_new_with_free_func (g_object_unref);
        for (i = 0; i < arr->len; ++i)
        {
            gchar *name;

            name = donna_node_get_name (arr->pdata[i]);
            if (*name != '.')
                g_ptr_array_add (filtered, g_object_ref (arr->pdata[i]));
            g_free (name);
        }

        arr = filtered;
    }

    if (arr->len == 0)
    {
no_submenu:
        gtk_menu_item_set_submenu (ls->item, NULL);
        if (ls->mc->submenus == DONNA_ENABLED_TYPE_ENABLED)
            gtk_widget_set_sensitive ((GtkWidget *) ls->item, FALSE);
        else if (ls->mc->submenus == DONNA_ENABLED_TYPE_COMBINE)
        {
            donna_image_menu_item_set_is_combined ((DonnaImageMenuItem *) ls->item,
                    FALSE);
            if (!donna_image_menu_item_get_is_combined_sensitive (
                        (DonnaImageMenuItem *) ls->item))
            {
                gtk_widget_set_sensitive ((GtkWidget *) ls->item, FALSE);
                gtk_menu_item_deselect (ls->item);
            }
        }
        free_load_submenu (ls);
        return;
    }

    if (ls->sub_mc)
        mc = ls->sub_mc;
    else
    {
        mc = g_slice_new0 (struct menu_click);
        memcpy (mc, ls->mc, sizeof (struct menu_click));
        mc->name = g_strdup (ls->mc->name);
    }
    mc->nodes = (ls->mc->show_hidden) ? g_ptr_array_ref (arr) : arr;

    menu = load_menu (mc);
    if (G_UNLIKELY (!menu))
        goto no_submenu;
    else
        g_object_ref (menu);

set_menu:
    /* see if the item is selected (if we're not TYPE_COMBINE then it can't be,
     * since thje menu hasn't event been shown yet). If so, we need to unselect
     * it before we can add/change (if timeout_called) the submenu */
    is_selected = ls->mc->submenus == DONNA_ENABLED_TYPE_COMBINE
        && (GtkWidget *) ls->item == gtk_menu_shell_get_selected_item (
                (GtkMenuShell *) gtk_widget_get_parent ((GtkWidget *) ls->item));

    if (is_selected)
        gtk_menu_item_deselect (ls->item);
    gtk_menu_item_set_submenu (ls->item, menu);
    if (is_selected)
        gtk_menu_item_select (ls->item);
    free_load_submenu (ls);
}

static void
submenu_get_children_timeout (DonnaTask *task, struct load_submenu *ls)
{
    if (!ls->invalid)
        donna_image_menu_item_set_loading_submenu (
                (DonnaImageMenuItem *) ls->item, NULL);
}

static void
load_submenu (struct load_submenu *ls)
{
    DonnaNode *node;
    DonnaTask *task;

    if (!ls->blocking)
        g_signal_handlers_disconnect_by_func (ls->item, load_submenu, ls);

    node = g_object_get_data ((GObject *) ls->item, "node");
    if (!node)
        return;

    task = donna_node_get_children_task (node, ls->mc->node_type, NULL);

    if (ls->blocking)
        g_object_ref (task);
    else
    {
        donna_task_set_callback (task,
                (task_callback_fn) submenu_get_children_cb,
                ls, (GDestroyNotify) free_load_submenu);
        donna_task_set_timeout (task, /*FIXME*/ 800,
                (task_timeout_fn) submenu_get_children_timeout, ls, NULL);
    }

    g_atomic_int_inc (&ls->ref_count);
    ls->task = task;

    donna_app_run_task ((DonnaApp *) ls->mc->donna, task);
    if (ls->blocking)
    {
        donna_task_wait_for_it (task, NULL, NULL);
        submenu_get_children_cb (task, FALSE, ls);
        g_object_unref (task);
    }
}

#define get_boolean(var, option, def_val)   do {            \
    if (!donna_config_get_boolean (priv->config, &var,      \
                "/menus/%s/" option, name))                 \
        if (!donna_config_get_boolean (priv->config, &var,  \
                    "/defaults/menus/" option))             \
        {                                                   \
            var = def_val;                                  \
            donna_config_set_boolean (priv->config, var,    \
                    "/defaults/menus/" option);             \
        }                                                   \
} while (0)

#define get_int(var, option, def_val)   do {                \
    if (!donna_config_get_int (priv->config, &var,          \
                "/menus/%s/" option, name))                 \
        if (!donna_config_get_int (priv->config, &var,      \
                    "/defaults/menus/" option))             \
        {                                                   \
            var = def_val;                                  \
            donna_config_set_int (priv->config, var,        \
                    "/defaults/menus/" option);             \
        }                                                   \
} while (0)

static struct menu_click *
load_mc (DonnaDonna *donna, const gchar *name, GPtrArray *nodes)
{
    DonnaDonnaPrivate *priv = donna->priv;
    struct menu_click *mc;
    gboolean b;
    gint i;

    mc = g_slice_new0 (struct menu_click);
    mc->donna = donna;
    mc->name  = g_strdup (name);
    mc->nodes = nodes;
    if (nodes)
        /* because we allow to have NULL elements to be used as separator, we
         * can't just use g_object_unref() as GDestroyNotify since that will
         * cause warning when called on NULL */
        g_ptr_array_set_free_func (nodes, (GDestroyNotify) donna_g_object_unref);

    /* icon options */
    get_boolean (b, "show_icons", TRUE);
    mc->show_icons = b;
    get_boolean (b, "use_default_icons", TRUE);
    mc->use_default_icons = b;

    get_int (i, "submenus", DONNA_ENABLED_TYPE_DISABLED);
    if (i == DONNA_ENABLED_TYPE_ENABLED || i == DONNA_ENABLED_TYPE_COMBINE)
    {
        mc->submenus = i;

        /* we could have made this option a list-flags, i.e. be exactly the
         * value we want, but we wanted it to be similar to what's used in
         * commands, where you say "all" not "item,container" (as would have
         * been the case using flags) */
        get_int (i, "children", 0);
        if (i == 1)
            mc->node_type = DONNA_NODE_ITEM;
        else if (i == 2)
            mc->node_type = DONNA_NODE_CONTAINER;
        else /* if (i == 0) */
            mc->node_type = DONNA_NODE_ITEM | DONNA_NODE_CONTAINER;

        get_boolean (b, "children_show_hidden", TRUE);
        mc->show_hidden = b;
    }
    get_boolean (b, "can_children_submenus", TRUE);
    mc->can_children_submenus = b;
    get_boolean (b, "can_children_menu", TRUE);
    mc->can_children_menu = b;

    get_boolean (b, "sort", FALSE);
    mc->is_sorted = b;
    if (mc->is_sorted)
    {
        get_boolean (b, "container_first", TRUE);
        mc->container_first = b;

        get_boolean (b, "locale_based", FALSE);
        mc->is_locale_based = b;

        get_boolean (b, "natural_order", TRUE);
        if (b)
            mc->options |= DONNA_SORT_NATURAL_ORDER;

        get_boolean (b, "dot_first", TRUE);
        if (b)
            mc->options |= DONNA_SORT_DOT_FIRST;

        if (mc->is_locale_based)
        {
            get_boolean (b, "special_first", TRUE);
            mc->sort_special_first = b;
        }
        else
        {
            get_boolean (b, "dot_mixed", FALSE);
            if (b)
                mc->options |= DONNA_SORT_DOT_MIXED;

            get_boolean (b, "case_sensitive", FALSE);
            if (!b)
                mc->options |= DONNA_SORT_CASE_INSENSITIVE;

            get_boolean (b, "ignore_spunct", FALSE);
            if (b)
                mc->options |= DONNA_SORT_IGNORE_SPUNCT;
        }
    }

    return mc;
}

static GtkWidget *
load_menu (struct menu_click *mc)
{
    GtkWidget *menu;
    guint last_sep;
    guint i;
    gboolean has_items = FALSE;

    if (mc->is_sorted)
        g_ptr_array_sort_with_data (mc->nodes, (GCompareDataFunc) node_cmp, mc);

    menu = gtk_menu_new ();

    /* in case the last few "nodes" are all NULLs, make sure we don't feature
     * any separators */
    for (last_sep = mc->nodes->len - 1;
            last_sep > 0 && !mc->nodes->pdata[last_sep];
            --last_sep)
        ;

    for (i = 0; i < mc->nodes->len; ++i)
    {
        DonnaNode *node = mc->nodes->pdata[i];
        GtkWidget *item;
        GtkWidget *image = NULL;
        GdkPixbuf *icon;
        gchar *name;

        if (!node)
        {
            /* no separator as first or last item.. */
            if (G_LIKELY (i > 0 && i < last_sep
                        /* ..and no separator after a separator */
                        && mc->nodes->pdata[i - 1]))
                item = gtk_separator_menu_item_new ();
            else
                continue;
        }
        else
        {
            DonnaNodeType type;
            DonnaNodeHasValue has;
            GValue v = G_VALUE_INIT;

            type = donna_node_get_node_type (node);
            name = donna_node_get_name (node);
            item = donna_image_menu_item_new_with_label (name);
            g_free (name);

            donna_node_get (node, TRUE, "menu-is-sensitive", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN && !g_value_get_boolean (&v))
                    gtk_widget_set_sensitive (item, FALSE);
                g_value_unset (&v);
            }

            donna_node_get (node, TRUE, "menu-is-combined-sensitive", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                    donna_image_menu_item_set_is_combined_sensitive (
                            (DonnaImageMenuItem *) item,
                            g_value_get_boolean (&v));
                g_value_unset (&v);
            }

            donna_node_get (node, TRUE, "menu-is-label-bold", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                    donna_image_menu_item_set_label_bold ((DonnaImageMenuItem *) item,
                            g_value_get_boolean (&v));
                g_value_unset (&v);
            }

            donna_node_get (node, TRUE, "menu-image-selected", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == GDK_TYPE_PIXBUF)
                {
                    GtkWidget *image;

                    image = gtk_image_new_from_pixbuf (g_value_get_object (&v));
                    donna_image_menu_item_set_image_selected ((DonnaImageMenuItem *) item,
                            image);
                }
                g_value_unset (&v);
            }

            g_object_set_data ((GObject *) item, "node", node);

            if (mc->show_icons)
            {
                if (donna_node_get_icon (node, FALSE, &icon) == DONNA_NODE_VALUE_SET)
                {
                    image = gtk_image_new_from_pixbuf (icon);
                    g_object_unref (icon);
                }
                else if (mc->use_default_icons)
                {
                    if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
                        image = gtk_image_new_from_stock (GTK_STOCK_FILE,
                                GTK_ICON_SIZE_MENU);
                    else /* DONNA_NODE_CONTAINER */
                        image = gtk_image_new_from_stock (GTK_STOCK_DIRECTORY,
                                GTK_ICON_SIZE_MENU);
                }
                else
                    image = NULL;
            }

            if (type == DONNA_NODE_CONTAINER)
            {
                DonnaEnabledTypes submenus = mc->submenus;
                struct menu_click *sub_mc = NULL;

                if (mc->can_children_submenus)
                {
                    donna_node_get (node, TRUE, "menu-submenus", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_UINT)
                        {
                            submenus = g_value_get_uint (&v);
                            submenus = CLAMP (submenus, 0, 3);
                        }
                        g_value_unset (&v);
                    }
                }

                if (mc->can_children_menu
                        && (submenus == DONNA_ENABLED_TYPE_ENABLED
                            || submenus == DONNA_ENABLED_TYPE_COMBINE))
                {
                    donna_node_get (node, TRUE, "menu-menu", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_STRING)
                            sub_mc = load_mc (mc->donna, g_value_get_string (&v), NULL);
                        g_value_unset (&v);
                    }
                }

                if (submenus == DONNA_ENABLED_TYPE_ENABLED)
                {
                    struct load_submenu ls = { 0, };

                    ls.blocking = TRUE;
                    ls.mc       = mc;
                    ls.sub_mc   = sub_mc;
                    ls.item     = (GtkMenuItem *) item;

                    if (submenus != mc->submenus)
                    {
                        if (!sub_mc)
                        {
                            /* load the sub_mc now, since we'll change option
                             * submenus in mc */
                            ls.sub_mc           = g_slice_new0 (struct menu_click);
                            memcpy (ls.sub_mc, mc, sizeof (struct menu_click));
                            ls.sub_mc->name     = g_strdup (mc->name);
                            ls.sub_mc->nodes    = NULL;
                        }

                        ls.own_mc       = TRUE;
                        ls.mc           = g_slice_new0 (struct menu_click);
                        memcpy (ls.mc, mc, sizeof (struct menu_click));
                        ls.mc->name     = NULL;
                        ls.mc->nodes    = NULL;
                        ls.mc->submenus = submenus;
                    }

                    load_submenu (&ls);
                }
                else if (submenus == DONNA_ENABLED_TYPE_COMBINE)
                {
                    struct load_submenu *ls;

                    ls = g_slice_new0 (struct load_submenu);
                    ls->mc          = mc;
                    ls->sub_mc      = sub_mc;
                    ls->item        = (GtkMenuItem *) item;
                    ls->ref_count = 1;

                    if (submenus != mc->submenus)
                    {
                        if (!sub_mc)
                        {
                            /* load the sub_mc now, since we'll change option
                             * submenus in mc */
                            ls->sub_mc           = g_slice_new0 (struct menu_click);
                            memcpy (ls->sub_mc, mc, sizeof (struct menu_click));
                            ls->sub_mc->name     = g_strdup (mc->name);
                            ls->sub_mc->nodes    = NULL;
                        }

                        ls->own_mc       = TRUE;
                        ls->mc           = g_slice_new0 (struct menu_click);
                        memcpy (ls->mc, mc, sizeof (struct menu_click));
                        ls->mc->name     = NULL;
                        ls->mc->nodes    = NULL;
                        ls->mc->submenus = submenus;
                    }

                    donna_image_menu_item_set_is_combined (
                            (DonnaImageMenuItem *) item, TRUE);
                    g_signal_connect_swapped (item, "load-submenu",
                            (GCallback) load_submenu, ls);
                    g_signal_connect_swapped (item, "destroy",
                            (GCallback) item_destroy_cb, ls);
                }
            }

            gtk_image_menu_item_set_image ((GtkImageMenuItem *) item, image);
        }

        /* we use button-release because that's what's handled by
         * DonnaImageMenuItem, as the button-press-event is used by GTK and
         * couldn't be blocked. */
        gtk_widget_add_events (item, GDK_BUTTON_RELEASE_MASK);
        g_signal_connect (item, "button-release-event",
                (GCallback) menuitem_button_release_cb, mc);
        g_signal_connect (item, "activate", (GCallback) menuitem_activate_cb, mc);

        gtk_widget_show (item);
        gtk_menu_attach ((GtkMenu *) menu, item, 0, 1, i, i + 1);
        has_items = TRUE;
    }

    if (G_UNLIKELY (!has_items))
    {
        g_object_unref (g_object_ref_sink (menu));
        return NULL;
    }

    g_signal_connect_swapped (menu, "destroy", (GCallback) free_menu_click, mc);
    return menu;
}

static gboolean
donna_donna_show_menu (DonnaApp       *app,
                       GPtrArray      *nodes,
                       const gchar    *name,
                       GError       **error)
{
    DonnaDonnaPrivate *priv;
    struct menu_click *mc;
    GtkMenu *menu;

    g_return_val_if_fail (DONNA_IS_DONNA (app), FALSE);
    priv = ((DonnaDonna *) app)->priv;

    mc = load_mc ((DonnaDonna *) app, name, nodes);
    /* menu will not be packed anywhere, so we need to take ownership and handle
     * it when done, i.e. on "unmap-event". It will trigger the widget's destroy
     * which is when we'll free mc */
    menu = (GtkMenu *) load_menu (mc);
    if (G_UNLIKELY (!menu))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot show/popup an empty menu");
        free_menu_click (mc);
        return FALSE;
    }
    menu = g_object_ref_sink (menu);
    gtk_widget_add_events ((GtkWidget *) menu, GDK_STRUCTURE_MASK);
    g_signal_connect (menu, "unmap-event", (GCallback) g_object_unref, NULL);

    gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 0,
            gtk_get_current_event_time ());
    return TRUE;
}

static void
donna_donna_show_error (DonnaApp       *app,
                        const gchar    *title,
                        const GError   *error)
{
    DonnaDonnaPrivate *priv;
    GtkWidget *w;

    g_return_if_fail (DONNA_IS_DONNA (app));
    priv = DONNA_DONNA (app)->priv;

    w = gtk_message_dialog_new (priv->window,
            GTK_DIALOG_DESTROY_WITH_PARENT | ((priv->exiting) ? GTK_DIALOG_MODAL : 0),
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            title);
    gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w, "%s",
            (error) ? error->message : "");
    g_signal_connect_swapped (w, "response", (GCallback) gtk_widget_destroy, w);
    gtk_widget_show_all (w);
    if (G_UNLIKELY (priv->exiting))
        /* if this happens while exiting (i.e. after main window was closed
         * (hidden), e.g. during a task from event "exit") then we make sure the
         * user gets to see/read the error, by blocking until he's closed it */
        gtk_dialog_run ((GtkDialog *) w);
}

static gpointer
donna_donna_get_ct_data (DonnaApp *app, const gchar *col_name)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    gchar *type = NULL;
    guint i;

    donna_config_get_string (priv->config, &type, "columns/%s/type", col_name);

    g_rec_mutex_lock (&priv->rec_mutex);
    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            /* should never be possible, since filter has the ct */
            if (G_UNLIKELY (!priv->column_types[i].ct))
                priv->column_types[i].ct = g_object_new (
                        priv->column_types[i].type, "app", app, NULL);
            if (!priv->column_types[i].ct_data)
                donna_columntype_refresh_data (priv->column_types[i].ct,
                        NULL, col_name, NULL, &priv->column_types[i].ct_data);
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (type);
            return priv->column_types[i].ct_data;
        }
    }
    /* Again: this cannot happen, since the filter has the ct */
    g_rec_mutex_unlock (&priv->rec_mutex);
    g_free (type);
    return NULL;
}

static DonnaTask *
donna_donna_nodes_io_task (DonnaApp       *app,
                           GPtrArray      *nodes,
                           DonnaIoType     io_type,
                           DonnaNode      *dest,
                           GError        **error)
{
    DonnaProvider *provider;
    DonnaTask *task;
    guint i;

    if (G_UNLIKELY (nodes->len == 0))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_EMPTY,
                "Cannot perform IO: no nodes given");
        return NULL;
    }

    /* make sure all nodes are from the same provider */
    provider = donna_node_peek_provider (nodes->pdata[0]);
    for (i = 1; i < nodes->len; ++i)
    {
        if (provider != donna_node_peek_provider (nodes->pdata[i]))
        {
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Cannot perform IO: nodes are not all from the same provider/domain.");
            return NULL;
        }
    }

    task = donna_provider_io_task (provider, io_type, TRUE, nodes, dest, error);
    if (!task && dest && provider != donna_node_peek_provider (dest))
    {
        g_clear_error (error);
        /* maybe the IO can be done by dest's provider */
        task = donna_provider_io_task (donna_node_peek_provider (dest),
                io_type, FALSE, nodes, dest, error);
    }

    if (!task)
    {
        g_prefix_error (error, "Couldn't to perform IO operation: ");
        return NULL;
    }

    return task;
}

struct ask
{
    GMainLoop *loop;
    GtkWidget *win;
    gint response;
};

static void
btn_clicked (GObject *btn, struct ask *ask)
{
    ask->response = GPOINTER_TO_INT (g_object_get_data (btn, "response"));
    gtk_widget_destroy (ask->win);
}

static gint
donna_donna_ask (DonnaApp       *app,
                 const gchar    *title,
                 const gchar    *details,
                 const gchar    *btn1_icon,
                 const gchar    *btn1_label,
                 const gchar    *btn2_icon,
                 const gchar    *btn2_label,
                 va_list         va_args)
{
    DonnaDonnaPrivate *priv;
    struct ask ask = { NULL, };
    GtkWidget *area;
    GtkBox *box;
    GtkWidget *btn;
    GtkWidget *w;
    gint i = 0;

    g_return_val_if_fail (DONNA_IS_DONNA (app), 0);
    priv = ((DonnaDonna *) app)->priv;

    ask.win = gtk_message_dialog_new (priv->window,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            title);

    if (details)
    {
        if (streqn (details, "markup:", 7))
            gtk_message_dialog_format_secondary_markup ((GtkMessageDialog *) ask.win,
                    "%s", details + 7);
        else
            gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) ask.win,
                    "%s", details);
    }

    area = gtk_dialog_get_action_area ((GtkDialog *) ask.win);
    box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous (box, TRUE);
    gtk_container_add ((GtkContainer *) area, (GtkWidget *) box);

    btn = gtk_button_new_with_label ((btn1_label) ? btn1_label : "Yes");
    w = gtk_image_new_from_icon_name ((btn1_icon) ? btn1_icon : "gtk-yes",
            GTK_ICON_SIZE_MENU);
    if (w)
        gtk_button_set_image ((GtkButton *) btn, w);
    g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
    g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
    gtk_box_pack_end (box, btn, FALSE, TRUE, 0);

    btn = gtk_button_new_with_label ((btn2_label) ? btn2_label : "No");
    w = gtk_image_new_from_icon_name ((btn2_icon) ? btn2_icon : "gtk-no",
            GTK_ICON_SIZE_MENU);
    if (w)
        gtk_button_set_image ((GtkButton *) btn, w);
    g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
    g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
    gtk_box_pack_end (box, btn, FALSE, TRUE, 0);

    for (;;)
    {
        const gchar *s;

        s = va_arg (va_args, const gchar *);
        if (!s)
            break;
        btn = gtk_button_new_with_label (s);

        s = va_arg (va_args, const gchar *);
        if (s)
        {
            w = gtk_image_new_from_icon_name (s, GTK_ICON_SIZE_MENU);
            if (w)
                gtk_button_set_image ((GtkButton *) btn, w);
        }

        g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
        g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
        gtk_box_pack_end (box, btn, FALSE, TRUE, 0);
    }

    ask.loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (ask.win, "destroy",
            (GCallback) g_main_loop_quit, ask.loop);
    gtk_widget_show_all (ask.win);
    g_main_loop_run (ask.loop);

    return ask.response;
}

struct ask_text
{
    GtkWindow   *win;
    GtkEntry    *entry;
    gchar       *s;
};

static void
btn_ok_cb (struct ask_text *data)
{
    data->s = g_strdup (gtk_entry_get_text (data->entry));
    gtk_widget_destroy ((GtkWidget *) data->win);
}

static gboolean
key_press_cb (struct ask_text *data, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
        gtk_widget_destroy ((GtkWidget *) data->win);
    return FALSE;
}

static gchar *
donna_donna_ask_text (DonnaApp       *app,
                      const gchar    *title,
                      const gchar    *details,
                      const gchar    *main_default,
                      const gchar   **other_defaults,
                      GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    GtkStyleContext *context;
    GMainLoop *loop;
    struct ask_text data = { NULL, };
    GtkBox *box;
    GtkBox *btn_box;
    GtkLabel *lbl;
    GtkWidget *w;

    data.win = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name ((GtkWidget *) data.win, "ask-text");
    gtk_window_set_transient_for (data.win, priv->window);
    gtk_window_set_destroy_with_parent (data.win, TRUE);
    gtk_window_set_default_size (data.win, 230, -1);
    gtk_window_set_decorated (data.win, FALSE);
    gtk_container_set_border_width ((GtkContainer *) data.win, 4);

    box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) data.win, (GtkWidget *) box);

    w = gtk_label_new (title);
    lbl = (GtkLabel *) w;
    gtk_label_set_selectable ((GtkLabel *) w, TRUE);
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "title");
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);

    if (details)
    {
        w = gtk_label_new (details);
        gtk_label_set_selectable ((GtkLabel *) w, TRUE);
        gtk_misc_set_alignment ((GtkMisc *) w, 0, 0.5);
        context = gtk_widget_get_style_context (w);
        gtk_style_context_add_class (context, "details");
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);
    }

    if (other_defaults)
    {
        w = gtk_combo_box_text_new_with_entry ();
        data.entry = (GtkEntry *) gtk_bin_get_child ((GtkBin *) w);
        for ( ; *other_defaults; ++other_defaults)
            gtk_combo_box_text_append_text ((GtkComboBoxText *) w, *other_defaults);
    }
    else
    {
        w = gtk_entry_new ();
        data.entry = (GtkEntry *) w;
    }
    g_signal_connect_swapped (data.entry, "activate",
            (GCallback) btn_ok_cb, &data);
    g_signal_connect (data.entry, "key-press-event",
            (GCallback) _key_press_ctrl_a_cb, NULL);
    g_signal_connect_swapped (data.entry, "key-press-event",
            (GCallback) key_press_cb, &data);

    if (main_default)
        gtk_entry_set_text (data.entry, main_default);
    gtk_box_pack_start (box, w, FALSE, FALSE, 10);


    btn_box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_end (box, (GtkWidget *) btn_box, FALSE, FALSE, 4);

    w = gtk_button_new_with_label ("Ok");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) btn_ok_cb, &data);
    gtk_box_pack_end (btn_box, w, FALSE, FALSE, 2);

    w = gtk_button_new_with_label ("Cancel");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) gtk_widget_destroy, data.win);
    gtk_box_pack_end (btn_box, w, FALSE, FALSE, 2);


    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (data.win, "destroy", (GCallback) g_main_loop_quit, loop);
    gtk_widget_show_all ((GtkWidget *) data.win);
    gtk_widget_grab_focus ((GtkWidget *) data.entry);
    gtk_label_select_region (lbl, 0, 0);
    g_main_loop_run (loop);

    return data.s;
}

static gboolean
window_delete_event_cb (GtkWidget *window, GdkEvent *event, DonnaDonna *donna)
{
    gtk_widget_hide (window);
    gtk_main_quit ();
    return TRUE;
}

static gboolean
just_focused_expired (DonnaDonna *donna)
{
    donna->priv->just_focused = FALSE;
    return FALSE;
}

static gboolean
focus_in_event_cb (GtkWidget *w, GdkEvent *event, DonnaDonna *donna)
{
    donna->priv->just_focused = TRUE;
    g_timeout_add (42, (GSourceFunc) just_focused_expired, donna);
    if (donna->priv->floating_window)
        gtk_widget_destroy (donna->priv->floating_window);
    return FALSE;
}

static gchar *
parse_string (DonnaDonna *donna, gchar *fmt)
{
    DonnaDonnaPrivate *priv = donna->priv;
    GString *str = NULL;
    gchar *s = fmt;
    DonnaNode *node;
    gchar *ss;

    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'a':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                g_string_append (str, donna_tree_view_get_name (priv->active_list));
                s += 2;
                fmt = s;
                break;

            case 'L':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                node = donna_tree_view_get_location (priv->active_list);
                if (G_LIKELY (node))
                {
                    const gchar *domain;
                    gint type;

                    domain = donna_node_get_domain (node);
                    if (!donna_config_get_int (priv->config, &type,
                                "donna/domain_%s", domain))
                        type = (streq ("fs", domain))
                            ? TITLE_DOMAIN_LOCATION : TITLE_DOMAIN_FULL_LOCATION;

                    if (type == TITLE_DOMAIN_LOCATION)
                        ss = donna_node_get_location (node);
                    else if (type == TITLE_DOMAIN_FULL_LOCATION)
                        ss = donna_node_get_full_location (node);
                    else if (type == TITLE_DOMAIN_CUSTOM)
                        if (!donna_config_get_string (priv->config, &ss,
                                    "donna/custom_%s", domain))
                            ss = donna_node_get_name (node);

                    g_string_append (str, ss);
                    g_free (ss);
                    g_object_unref (node);
                }
                s += 2;
                fmt = s;
                break;

            case 'l':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                node = donna_tree_view_get_location (priv->active_list);
                if (G_LIKELY (node))
                {
                    ss = donna_node_get_full_location (node);
                    g_string_append (str, ss);
                    g_free (ss);
                    g_object_unref (node);
                }
                s += 2;
                fmt = s;
                break;

            case 'v':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                g_string_append (str, /* FIXME PACKAGE_VERSION */ "0.00");
                s += 2;
                fmt = s;
                break;

            default:
                s += 2;
                break;
        }
    }

    if (!str)
        return NULL;

    g_string_append (str, fmt);
    return g_string_free (str, FALSE);
}

static void
refresh_window_title (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;
    gchar *def = "%L - Donnatella";
    gchar *fmt;
    gchar *str;

    if (!donna_config_get_string (priv->config, &fmt, "donna/title"))
        fmt = def;

    str = parse_string (donna, fmt);
    gtk_window_set_title (priv->window, (str) ? str : fmt);
    g_free (str);
    if (fmt != def)
        g_free (fmt);
}

static void
update_cur_dirname (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;
    DonnaNode *node;
    gchar *s;

    g_object_get (priv->active_list, "location", &node, NULL);
    if (G_UNLIKELY (!node))
        return;

    if (!streq ("fs", donna_node_get_domain (node)))
    {
        g_object_unref (node);
        return;
    }

    s = priv->cur_dirname;
    priv->cur_dirname = donna_node_get_location (node);
    g_free (s);
    g_object_unref (node);
}

static void
active_location_changed (GObject *object, GParamSpec *spec, DonnaDonna *donna)
{
    update_cur_dirname (donna);
    refresh_window_title (donna);
}

static void
switch_statuses_source (DonnaDonna *donna, guint source, DonnaStatusProvider *sp)
{
    DonnaDonnaPrivate *priv = donna->priv;
    GSList *l;

    for (l = priv->statuses; l; l = l->next)
    {
        struct status *status = l->data;

        if (status->source == source)
        {
            GError *err = NULL;
            struct provider *provider;
            guint i;

            for (i = 0; i < status->providers->len; ++i)
            {
                provider = &g_array_index (status->providers, struct provider, i);
                if (provider->sp == sp)
                    break;
            }

            if (i >= status->providers->len)
            {
                struct provider p;

                p.sp = sp;
                p.id = donna_status_provider_create_status (p.sp,
                        status->name, &err);
                if (p.id == 0)
                {
                    g_warning ("Failed to connect statusbar area '%s' to new active-list: "
                            "create_status() failed: %s",
                            status->name, err->message);
                    g_clear_error (&err);
                    /* this simply makes sure the area is blank/not connected to
                     * any provider anymore */
                    donna_status_bar_update_area (priv->sb, status->name,
                            NULL, 0, NULL);
                    continue;
                }
                g_array_append_val (status->providers, p);
                provider = &g_array_index (status->providers, struct provider,
                        status->providers->len - 1);
            }

            if (!donna_status_bar_update_area (priv->sb, status->name,
                        provider->sp, provider->id, &err))
            {
                g_warning ("Failed to connect statusbar area '%s' to new active-list: "
                        "update_area() failed: %s",
                        status->name, err->message);
                g_clear_error (&err);
                /* this simply makes sure the area is blank/not connected to
                 * any provider anymore */
                donna_status_bar_update_area (priv->sb, status->name,
                        NULL, 0, NULL);
            }
        }
    }
}

static inline void
set_active_list (DonnaDonna *donna, DonnaTreeView *list)
{
    DonnaDonnaPrivate *priv = donna->priv;

    if (priv->sid_active_location > 0)
        g_signal_handler_disconnect (priv->active_list, priv->sid_active_location);
    priv->sid_active_location = g_signal_connect (list, "notify::location",
            (GCallback) active_location_changed, donna);

    switch_statuses_source (donna, ST_SCE_ACTIVE, (DonnaStatusProvider *) list);

    priv->active_list = g_object_ref (list);
    update_cur_dirname (donna);
    refresh_window_title (donna);
    g_object_notify ((GObject *) donna, "active-list");
}

static void
window_set_focus_cb (GtkWindow *window, GtkWidget *widget, DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;

    if (DONNA_IS_TREE_VIEW (widget))
    {
        priv->focused_tree = (DonnaTreeView *) widget;
        switch_statuses_source (donna, ST_SCE_FOCUSED,
                (DonnaStatusProvider *) widget);

        if (!donna_tree_view_is_tree ((DonnaTreeView *) widget)
                && (DonnaTreeView *) widget != priv->active_list)
        {
            gboolean skip;
            if (donna_config_get_boolean (priv->config, &skip,
                        "treeviews/%s/not_active_list",
                        donna_tree_view_get_name ((DonnaTreeView *) widget)) && skip)
                return;

            set_active_list (donna, (DonnaTreeView *) widget);
        }
    }
}

static GtkWidget *
load_widget (DonnaDonna  *donna,
             gchar      **def,
             gchar      **active_list_name,
             GtkWidget  **active_list_widget)
{
    DonnaDonnaPrivate *priv = donna->priv;
    GtkWidget *w;
    gchar *end;
    gchar *sep = NULL;

    skip_blank (*def);

    for (end = *def; end; ++end)
    {
        if (*end == '(')
        {
            if (end - *def == 4 && (streqn (*def, "boxH", 4)
                        || streqn (*def, "boxV", 4)))
            {
                GtkBox *box;

                box = (GtkBox *) gtk_box_new (((*def)[3] == 'H')
                        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL,
                        0);
                *def = end + 1;
                for (;;)
                {
                    w = load_widget (donna, def, active_list_name, active_list_widget);
                    if (!w)
                    {
                        g_object_unref (g_object_ref_sink (box));
                        return NULL;
                    }

                    gtk_box_pack_start (box, w, TRUE, TRUE, 0);

                    if (**def == ',')
                        ++*def;
                    else if (**def != ')')
                    {
                        g_debug("expected ',' or ')': %s", *def);
                        g_object_unref (g_object_ref_sink (box));
                        return NULL;
                    }
                    else
                        break;
                }
                ++*def;
                return (GtkWidget *) box;
            }
            else if (end - *def == 5 && (streqn (*def, "paneH", 5)
                        || streqn (*def, "paneV", 5)))
            {
                GtkPaned *paned;
                gboolean is_fixed;

                paned = (GtkPaned *) gtk_paned_new (((*def)[4] == 'H')
                        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
                *def = end + 1;

                for ( ; isblank (**def); ++*def)
                    ;
                is_fixed = (**def == '!');
                if (is_fixed)
                    ++*def;
                w = load_widget (donna, def, active_list_name, active_list_widget);
                if (!w)
                {
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                gtk_paned_pack1 (paned, w, !is_fixed, TRUE);

                if (**def == '@')
                {
                    gint pos = 0;

                    for (++*def; **def >= '0' && **def <= '9'; ++*def)
                        pos = pos * 10 + **def - '0';
                    gtk_paned_set_position (paned, pos);
                }

                if (**def != ',')
                {
                    g_debug("missing second item in pane: %s", *def);
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                ++*def;
                for ( ; isblank (**def); ++*def)
                    ;
                is_fixed = (**def == '!');
                if (is_fixed)
                    ++*def;
                w = load_widget (donna, def, active_list_name, active_list_widget);
                if (!w)
                {
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                gtk_paned_pack2 (paned, w, !is_fixed, TRUE);

                if (**def != ')')
                {
                    g_debug("only 2 items per pane: %s", *def);
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }
                ++*def;
                return (GtkWidget *) paned;
            }
        }
        else if (*end == ':')
            sep = end;
        else if (*end == ',' || *end == '@' || *end == ')' || *end == '\0')
        {
            gchar e = *end;

            if (!sep)
            {
                g_debug("missing ':' with item name: %s", *def);
                return NULL;
            }

            *sep = '\0';
            if (streq (*def, "treeview"))
            {
                DonnaTreeView *tree;

                *def = sep + 1;
                *end = '\0';

                w = gtk_scrolled_window_new (NULL, NULL);
                tree = donna_load_treeview (donna, *def);
                if (!donna_tree_view_is_tree (tree) && !priv->active_list)
                {
                    gboolean skip;
                    if (!donna_config_get_boolean (priv->config, &skip,
                                "treeviews/%s/not_active_list",
                                donna_tree_view_get_name (tree)) || !skip)
                    {
                        if (streq (*active_list_name,
                                    donna_tree_view_get_name (tree)))
                        {
                            priv->active_list = tree;
                            *active_list_widget = (GtkWidget *) tree;
                        }
                        else if (!*active_list_widget)
                            *active_list_widget = (GtkWidget *) tree;
                    }
                }
                gtk_container_add ((GtkContainer *) w, (GtkWidget *) tree);
                *end = e;
            }
            else if (streq (*def, "toolbar"))
                w = gtk_toolbar_new ();
            else
            {
                g_debug("invalid item type: %s", *def);
                *sep = ':';
                return NULL;
            }
            *sep = ':';

            *def = end;
            return w;
        }
    }
    return NULL;
}

static inline gint
create_gui (DonnaDonna *donna)
{
    DonnaApp            *app = (DonnaApp *) donna;
    DonnaDonnaPrivate   *priv = donna->priv;
    GtkWindow           *window;
    GtkWidget           *active_list_widget = NULL;
    GtkWidget           *w;
    gchar               *active_list_name;
    gchar               *s;
    gchar               *ss;
    gchar               *def;
    gchar               *areas;
    gint                 width;
    gint                 height;

    window = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    priv->window = g_object_ref (window);

    g_signal_connect (window, "focus-in-event",
            (GCallback) focus_in_event_cb, app);
    g_signal_connect (window, "delete-event",
            (GCallback) window_delete_event_cb, donna);

    if (!donna_config_get_string (priv->config, &s, "donna/layout"))
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: no layout set");
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        return 1;
    }

    if (!donna_config_get_string (priv->config, &ss, "layouts/%s", s))
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: layout '%s' not defined", s);
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        g_free (s);
        return 1;
    }
    g_free (s);
    s = ss;

    if (!donna_config_get_string (priv->config, &active_list_name,
                "donna/active_list"))
        active_list_name = NULL;

    def = s;
    w = load_widget ((DonnaDonna *) app, &def, &active_list_name, &active_list_widget);
    g_free (s);
    if (!w)
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: invalid layout");
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        return 2;
    }
    gtk_container_add ((GtkContainer *) window, w);

    g_free (active_list_name);
    if (!priv->active_list)
    {
        if (!active_list_widget)
        {
            w = gtk_message_dialog_new (NULL,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "Unable to load interface: no active-list found");
            gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w,
                    "You need at least one treeview in mode List to be defined in your layout.");
            gtk_dialog_run ((GtkDialog *) w);
            gtk_widget_destroy (w);
            return 3;
        }
    }
    priv->active_list = NULL;
    set_active_list ((DonnaDonna *) app, (DonnaTreeView *) active_list_widget);
    priv->focused_tree = (DonnaTreeView *) active_list_widget;

    /* status bar */
    if (donna_config_get_string (priv->config, &areas, "statusbar/areas"))
    {
        GtkWidget *box;

        priv->sb = g_object_new (DONNA_TYPE_STATUS_BAR, NULL);
        s = areas;
        for (;;)
        {
            GError *err = NULL;
            struct status *status;
            struct provider provider;
            gboolean expand;
            gchar *sce;
            gchar *e;

            e = strchr (s, ',');
            if (e)
                *e = '\0';

            if (!donna_config_get_string (priv->config, &sce,
                        "statusbar/%s/source", s))
            {
                g_warning ("Unable to load statusbar area '%s', no source specified",
                        s);
                goto next;
            }

            status = g_new0 (struct status, 1);
            status->name = g_strdup (s);
            status->providers = g_array_new (FALSE, FALSE, sizeof (struct provider));
            if (streq (sce, ":active"))
            {
                status->source = ST_SCE_ACTIVE;
                provider.sp = (DonnaStatusProvider *) priv->active_list;
            }
            else if (streq (sce, ":focused"))
            {
                status->source = ST_SCE_FOCUSED;
                provider.sp = (DonnaStatusProvider *) priv->focused_tree;
            }
            else if (streq (sce, ":task"))
            {
                status->source = ST_SCE_TASK;
                provider.sp = (DonnaStatusProvider *) priv->task_manager;
            }
#if 0
            else if (streq (s, "donna"))
                status->source = ST_SCE_DONNA;
#endif
            else
            {
                g_free (status->name);
                g_free (status);
                g_warning ("Unable to load statusbar area '%s', invalid source: '%s'",
                        s, sce);
                g_free (sce);
                goto next;
            }
            g_free (sce);

            provider.id = donna_status_provider_create_status (provider.sp,
                    status->name, &err);
            if (provider.id == 0)
            {
                g_free (status->name);
                g_free (status);
                g_warning ("Unable to load statusbar area '%s', failed to init provider: %s",
                        s, err->message);
                g_clear_error (&err);
                goto next;
            }
            g_array_append_val (status->providers, provider);

            if (!donna_config_get_int (priv->config, &width,
                        "statusbar/%s/width", status->name))
                width = -1;
            if (!donna_config_get_boolean (priv->config, &expand,
                        "statusbar/%s/expand", status->name))
                expand = TRUE;
            donna_status_bar_add_area (priv->sb, status->name,
                    provider.sp, provider.id, width, expand, &err);

            priv->statuses = g_slist_prepend (priv->statuses, status);
next:
            if (!e)
                break;
            else
                s = e + 1;
        }
        g_free (areas);

        w = g_object_ref (gtk_bin_get_child ((GtkBin *) window));
        gtk_container_remove ((GtkContainer *) window, w);
        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add ((GtkContainer *) window, box);
        gtk_box_pack_start ((GtkBox *) box, w, TRUE, TRUE, 0);
        g_object_unref (w);
        gtk_box_pack_end ((GtkBox *) box, (GtkWidget *) priv->sb, FALSE, FALSE, 0);
    }

    /* sizing */
    if (!donna_config_get_int (priv->config, &width, "donna/width"))
        width = -1;
    if (!donna_config_get_int (priv->config, &height, "donna/height"))
        height = -1;
    gtk_window_set_default_size (window, width, height);

    refresh_window_title ((DonnaDonna *) app);
    gtk_widget_show_all ((GtkWidget *) window);
    gtk_widget_grab_focus (active_list_widget);
    g_signal_connect (window, "set-focus",
            (GCallback) window_set_focus_cb, app);

    return 0;
}

static inline void
init_donna (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;
    GPtrArray *arr = NULL;
    const gchar *main_dir;
    const gchar * const *extra_dirs;
    const gchar * const *dir;
    const gchar * const *first;

    /* get config dirs */
    main_dir = g_get_user_config_dir ();
    extra_dirs = g_get_system_config_dirs ();

    /* load config definitions: merge user & system ones */
    load_conf (priv->config, main_dir, TRUE);
    for (dir = extra_dirs; *dir; ++dir)
        load_conf (priv->config, *dir, TRUE);

    /* load config: load user one. If there's none, copy the system one over,
     * and keep another copy as "reference" for future merging */
    if (!load_conf (priv->config, main_dir, FALSE))
    {
        for (dir = extra_dirs; *dir; ++dir)
            if (copy_and_load_conf (priv->config, *dir, main_dir))
                break;
    }

    /* CSS - At same priority, the last one loaded takes precedence, so we need
     * to load system ones first (in reverse order), then the user one */
    first = extra_dirs;
    for (dir = extra_dirs; *dir; ++dir)
        ;
    if (dir != first)
    {
        for (--dir; dir != first; --dir)
            load_css (*dir);
        load_css (*dir);
    }
    load_css (main_dir);

    /* compile patterns of arrangements' masks */
    priv->arrangements = load_arrangements (priv->config, "arrangements");

    /* preload some required providers */
    g_object_unref (donna_donna_get_provider ((DonnaApp *) donna, "command"));
    g_object_unref (donna_donna_get_provider ((DonnaApp *) donna, "register"));
    g_object_unref (donna_donna_get_provider ((DonnaApp *) donna, "internal"));
    g_object_unref (donna_donna_get_provider ((DonnaApp *) donna, "mark"));

    if (donna_config_list_options (priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, "visuals"))
    {
        guint i;

        for (i = 0; i < arr->len; ++i)
        {
            struct visuals *visuals;
            gchar *s;

            if (!donna_config_get_string (priv->config, &s, "visuals/%s/node",
                    arr->pdata[i]))
                continue;

            visuals = g_slice_new0 (struct visuals);
            donna_config_get_string (priv->config, &visuals->name,
                    "visuals/%s/name", arr->pdata[i]);
            donna_config_get_string (priv->config, &visuals->icon,
                    "visuals/%s/icon", arr->pdata[i]);
            donna_config_get_string (priv->config, &visuals->box,
                    "visuals/%s/box", arr->pdata[i]);
            donna_config_get_string (priv->config, &visuals->highlight,
                    "visuals/%s/highlight", arr->pdata[i]);
            g_hash_table_insert (priv->visuals, s, visuals);
        }
    }
}

int
main (int argc, char *argv[])
{
    DonnaDonna *donna;
    gint rc;

    setlocale (LC_ALL, "");
    gtk_init (&argc, &argv);

    g_main_context_acquire (g_main_context_default ());
    donna = g_object_new (DONNA_TYPE_DONNA, NULL);

    /* load config, css arrangements, required providers, etc */
    init_donna (donna);
    /* create & show the main window */
    rc = create_gui (donna);
    if (G_UNLIKELY (rc != 0))
        return rc;

    donna_app_emit_event ((DonnaApp *) donna, "start", NULL, NULL, NULL, NULL);

    /* in the off-chance something before already led to closing the app (could
     * happen e.g. if something had started its own mainloop (e.g. in event
     * "start" there was a command that does, like ask_text) and the user then
     * closed the main window */
    if (G_LIKELY (gtk_widget_get_realized ((GtkWidget *) donna->priv->window)))
        gtk_main ();

    donna->priv->exiting = TRUE;
    donna_app_emit_event ((DonnaApp *) donna, "exit", NULL, NULL, NULL, NULL);

    /* let's make sure all (internal) tasks (e.g. triggered from event "exit")
     * are done before we die */
    g_thread_pool_stop_unused_threads ();
    while (g_thread_pool_get_num_threads (donna->priv->pool) > 0)
    {
        if (gtk_events_pending ())
            gtk_main_iteration ();
        g_thread_pool_stop_unused_threads ();
    }
    gtk_widget_destroy ((GtkWidget *) donna->priv->window);
    g_main_context_release (g_main_context_default ());

    g_object_unref (donna);
    return rc;
}
