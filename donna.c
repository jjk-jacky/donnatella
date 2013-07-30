
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
#include "macros.h"

guint donna_debug_flags = 0;

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

enum types
{
    TYPE_UNKNOWN = 0,
    TYPE_ENABLED,
    TYPE_DISABLED,
    TYPE_COMBINE,
    TYPE_IGNORE
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

struct reg
{
    DonnaRegisterType type;
    GSList *list; /* list of full locations */
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
    /* visuals are under a RW lock so everyone can read them at the same time
     * (e.g. creating nodes, get_children() & the likes). The write operation
     * should be quite rare. */
    GRWLock          lock;
    GHashTable      *visuals;
    /* ct, providers, filters, intrefs & registers are all under the same lock
     * because there shouldn't be a need to separate them all. We use a
     * recursive mutex because we need it for filters, to handle correctly the
     * toggle_ref */
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
    GHashTable      *registers;
};

struct argmt
{
    gchar        *name;
    GPatternSpec *pspec;
};

static GThread *mt;
static GLogLevelFlags show_log = G_LOG_LEVEL_DEBUG;

static GdkAtom atom_gnome;
static GdkAtom atom_kde;
static GdkAtom atom_uris;

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
static gchar *          donna_donna_new_int_ref     (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
static gpointer         donna_donna_get_int_ref     (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
static gboolean         donna_donna_free_int_ref    (DonnaApp       *app,
                                                     const gchar    *intref);
static gboolean         donna_donna_show_menu       (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError       **error);
static void             donna_donna_show_error      (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);
static gpointer         donna_donna_get_ct_data     (DonnaApp       *app,
                                                     const gchar    *col_name);
static gboolean         donna_donna_drop_register   (DonnaApp       *app,
                                                     const gchar    *name,
                                                     GError        **error);
static gboolean         donna_donna_set_register    (DonnaApp       *app,
                                                     const gchar    *name,
                                                     DonnaRegisterType type,
                                                     GPtrArray      *nodes,
                                                     GError        **error);
static gboolean         donna_donna_add_to_register (DonnaApp       *app,
                                                     const gchar    *name,
                                                     GPtrArray      *nodes,
                                                     GError        **error);
static gboolean         donna_donna_set_register_type (
                                                     DonnaApp       *app,
                                                     const gchar    *name,
                                                     DonnaRegisterType type,
                                                     GError        **error);
static gboolean         donna_donna_get_register_nodes (
                                                     DonnaApp       *app,
                                                     const gchar    *name,
                                                     DonnaDropRegister drop,
                                                     DonnaRegisterType *type,
                                                     GPtrArray     **nodes,
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
    interface->new_int_ref          = donna_donna_new_int_ref;
    interface->get_int_ref          = donna_donna_get_int_ref;
    interface->free_int_ref         = donna_donna_free_int_ref;
    interface->show_menu            = donna_donna_show_menu;
    interface->show_error           = donna_donna_show_error;
    interface->get_ct_data          = donna_donna_get_ct_data;
    interface->drop_register        = donna_donna_drop_register;
    interface->set_register         = donna_donna_set_register;
    interface->add_to_register      = donna_donna_add_to_register;
    interface->set_register_type    = donna_donna_set_register_type;
    interface->get_register_nodes   = donna_donna_get_register_nodes;
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

    atom_gnome = gdk_atom_intern_static_string ("x-special/gnome-copied-files");
    atom_kde   = gdk_atom_intern_static_string ("application/x-kde-cutselection");
    atom_uris  = gdk_atom_intern_static_string ("text/uri-list");
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
                DONNA_CONFIG_OPTION_TYPE_CATEGORY, sce))
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

static void
free_register (struct reg *reg)
{
    if (reg->list)
        g_slist_free_full (reg->list, g_free);
    g_free (reg);
}

static void
donna_donna_init (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv;
    GPtrArray *arr = NULL;

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

    /* load the config */
    /* TODO */

    /* compile patterns of arrangements' masks */
    priv->arrangements = load_arrangements (priv->config, "arrangements");

    priv->filters = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_filter);

    priv->visuals = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_visuals);

    priv->intrefs = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_intref);

    priv->registers = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_register);

    if (donna_config_list_options (priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_CATEGORY, "visuals"))
    {
        gint i;

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
    enum types type;
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
                type = TYPE_ENABLED;
            switch (type)
            {
                case TYPE_ENABLED:
                case TYPE_COMBINE:
                    /* process */
                    break;

                case TYPE_DISABLED:
                    /* flag to stop */
                    i = max;
                    break;

                case TYPE_IGNORE:
                    /* next source */
                    continue;

                case TYPE_UNKNOWN:
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
        if (type == TYPE_ENABLED || (arr /* could still be NULL */
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
    ir->ptr  = ptr;
    ir->last = g_get_monotonic_time ();

    s = g_strdup_printf ("<%u%u>", rand (), ir);
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
    /* are containers just items, submenus, or both combined? */
    guint            submenus           : 2;
    /* type of nodes to load in submenus */
    DonnaNodeType    node_type          : 2;
    /* do we "show" dot files in submenus */
    guint            show_hidden        : 1;
    /* sort options */
    guint            is_sorted          : 1;
    guint            container_first    : 1;
    guint            is_locale_based    : 1;
    DonnaSortOptions options            : 5;
    guint            sort_special_first : 1;
};

static void
free_menu_click (struct menu_click *mc)
{
    g_ptr_array_unref (mc->nodes);
    g_free (mc->name);
    g_slice_free (struct menu_click, mc);
}

static gboolean
menu_conv_flag (const gchar  c,
                DonnaArgType type,
                gboolean     dereference,
                DonnaApp    *app,
                gpointer    *out,
                DonnaNode   *node)
{
    GString *str = *out;
    gchar *s;

    switch (c)
    {
        case 'N':
            if (type == DONNA_ARG_TYPE_NOTHING)
            {
                if (node)
                {
                    s = donna_node_get_location (node);
                    g_string_append (str, s);
                    g_free (s);
                }
            }
            else
                return FALSE;
            return TRUE;

        case 'n':
            if (type == DONNA_ARG_TYPE_NOTHING)
            {
                if (node)
                {
                    if (dereference)
                        s = donna_node_get_full_location (node);
                    else
                        s = donna_app_new_int_ref (app, DONNA_ARG_TYPE_NODE,
                                g_object_ref (node));
                    g_string_append (str, s);
                    g_free (s);
                }
            }
            else if ((type & DONNA_ARG_TYPE_NODE) && node)
                *out = g_object_ref (node);
            else
                return FALSE;
            return TRUE;;
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

static gboolean
menuitem_button_release_cb (GtkWidget           *item,
                            GdkEventButton      *event,
                            struct menu_click   *mc)
{
    DonnaDonnaPrivate *priv = mc->donna->priv;
    DonnaNode *node;
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
            fl = g_strdup ("command:node_activate (%n,0)");
        else
            return FALSE;
    }

    g_object_ref (node);
    _donna_command_parse_run ((DonnaApp *) mc->donna, FALSE, "nN",
            (_conv_flag_fn) menu_conv_flag, node, g_object_unref, fl);
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
    if (g_atomic_int_dec_and_test (&ls->ref_count) && !ls->blocking)
        /* if not blocking it was on stack */
        g_slice_free (struct load_submenu, ls);
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
    struct load_submenu local_ls;
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
        guint i;

        for (i = 0; i < arr->len; )
        {
            gchar *name;

            name = donna_node_get_name (arr->pdata[i]);
            if (*name == '.')
                /* move last item into i; hence no increment */
                g_ptr_array_remove_index_fast (arr, i);
            else
                ++i;
            g_free (name);
        }
    }

    if (arr->len == 0)
    {
        gtk_menu_item_set_submenu (ls->item, NULL);
        if (ls->mc->submenus == TYPE_ENABLED)
            gtk_widget_set_sensitive ((GtkWidget *) ls->item, FALSE);
        else if (ls->mc->submenus == TYPE_COMBINE)
            donna_image_menu_item_set_is_combined ((DonnaImageMenuItem *) ls->item,
                    FALSE);
        free_load_submenu (ls);
        return;
    }

    mc = g_slice_new0 (struct menu_click);
    memcpy (mc, ls->mc, sizeof (struct menu_click));
    mc->name  = g_strdup (ls->mc->name);
    mc->nodes = g_ptr_array_ref (arr);

    menu = g_object_ref (load_menu (mc));

set_menu:
    /* see if the item is selected (if we're not TYPE_COMBINE then it can't be,
     * since thje menu hasn't event been shown yet). If so, we need to unselect
     * it before we can add/change (if timeout_called) the submenu */
    is_selected = ls->mc->submenus == TYPE_COMBINE
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

    donna_task_set_callback (task,
            (task_callback_fn) submenu_get_children_cb,
            ls,
            (ls->blocking) ? NULL : (GDestroyNotify) free_load_submenu);
    if (ls->blocking)
        donna_task_set_can_block (g_object_ref_sink (task));
    else
        donna_task_set_timeout (task, /*FIXME*/ 800,
                (task_timeout_fn) submenu_get_children_timeout, ls, NULL);

    g_atomic_int_inc (&ls->ref_count);
    ls->task = task;

    donna_app_run_task ((DonnaApp *) ls->mc->donna, task);
    if (ls->blocking)
    {
        donna_task_wait_for_it (task);
        g_object_unref (task);
    }
}

static GtkWidget *
load_menu (struct menu_click *mc)
{
    GtkWidget *menu;
    guint i;

    if (mc->is_sorted)
        g_ptr_array_sort_with_data (mc->nodes, (GCompareDataFunc) node_cmp, mc);

    menu = gtk_menu_new ();
    g_signal_connect_swapped (menu, "destroy", (GCallback) free_menu_click, mc);

    for (i = 0; i < mc->nodes->len; ++i)
    {
        DonnaNode *node = mc->nodes->pdata[i];
        GtkWidget *item;
        GtkWidget *image;
        GdkPixbuf *icon;
        gchar *name;

        if (!node)
            item = gtk_separator_menu_item_new ();
        else
        {
            name = donna_node_get_name (node);
            item = donna_image_menu_item_new_with_label (name);
            g_free (name);

            g_object_set_data ((GObject *) item, "node", node);

            if (donna_node_get_icon (node, FALSE, &icon) == DONNA_NODE_VALUE_SET)
            {
                image = gtk_image_new_from_pixbuf (icon);
                g_object_unref (icon);
            }
            else if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
                image = gtk_image_new_from_stock (GTK_STOCK_FILE, GTK_ICON_SIZE_MENU);
            else /* DONNA_NODE_CONTAINER */
            {
                image = gtk_image_new_from_stock (GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU);

                if (mc->submenus == TYPE_ENABLED)
                {
                    struct load_submenu ls = { 0, };

                    ls.blocking = TRUE;
                    ls.item = (GtkMenuItem *) item;
                    ls.mc = mc;

                    load_submenu (&ls);
                }
                else if (mc->submenus == TYPE_COMBINE)
                {
                    struct load_submenu *ls;

                    ls = g_slice_new0 (struct load_submenu);
                    ls->mc = mc;
                    ls->item = (GtkMenuItem *) item;
                    ls->ref_count = 1;

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
    }

    return menu;
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

static gboolean
donna_donna_show_menu (DonnaApp       *app,
                       GPtrArray      *nodes,
                       const gchar    *name,
                       GError       **error)
{
    DonnaDonnaPrivate *priv;
    struct menu_click *mc;
    GtkMenu *menu;
    gboolean b;
    guint i;

    g_return_val_if_fail (DONNA_IS_DONNA (app), FALSE);
    priv = ((DonnaDonna *) app)->priv;

    mc = g_slice_new0 (struct menu_click);
    mc->donna = (DonnaDonna *) app;
    mc->name  = g_strdup (name);
    mc->nodes = nodes;

    get_int (i, "submenus", TYPE_DISABLED);
    if (i == TYPE_ENABLED || i == TYPE_COMBINE)
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

    get_boolean (b, "sort", TRUE);
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

    /* menu will not be packed anywhere, so we need to take ownership and handle
     * it when done, i.e. on "deactivate". It will trigger the widget's destroy
     * which is when we'll free mc */
    menu = g_object_ref_sink (load_menu (mc));
    g_signal_connect (menu, "deactivate", (GCallback) g_object_unref, NULL);

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
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            title);
    gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w, "%s",
            (error) ? error->message : "");
    g_signal_connect_swapped (w, "response", (GCallback) gtk_widget_destroy, w);
    gtk_widget_show_all (w);
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

static void
clipboard_get (GtkClipboard     *clipboard,
               GtkSelectionData *sd,
               guint             info,
               DonnaApp         *app)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    struct reg *reg;
    GSList *l;
    GString *str;

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = g_hash_table_lookup (priv->registers, "+");
    if (G_UNLIKELY (!reg))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_warning ("Donna: clipboard_get() for CLIPBOARD triggered while register '+' doesn't exist");
        return;
    }

    str = g_string_new (NULL);
    if (info < 3)
        g_string_append_printf (str, "%s\n",
                (reg->type == DONNA_REGISTER_CUT) ? "cut" : "copy");

    for (l = reg->list; l; l = l->next)
    {
        GError *err = NULL;
        gchar *s;

        s = g_filename_to_uri (l->data, NULL, &err);
        if (G_UNLIKELY (!s))
        {
            g_warning ("Donna: clipboard_get() for CLIPBOARD: Failed to convert '%s' to URI: %s",
                    l->data, err->message);
            g_clear_error (&err);
        }
        else
        {
            g_string_append (str, s);
            g_string_append_c (str, '\n');
            g_free (s);
        }
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    gtk_selection_data_set (sd, (info == 1) ? atom_gnome
            : ((info == 2) ? atom_kde : atom_uris), 8, str->str, str->len);
    g_string_free (str, TRUE);
}

static void
clipboard_clear (GtkClipboard *clipboard, DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv = donna->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_remove (priv->registers, "+");
    g_rec_mutex_unlock (&priv->rec_mutex);
}

static inline gboolean
take_clipboard_ownership (DonnaApp *app, gboolean clear)
{
    GtkClipboard *clipboard;
    GtkTargetEntry targets[] = {
        { "x-special/gnome-copied-files", 0, 1 },
        { "application/x-kde-cutselection", 0, 2 },
        { "text/uri-list", 0, 3 },
    };
    gboolean ret;

    clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (G_UNLIKELY (!clipboard))
        return FALSE;
    ret = gtk_clipboard_set_with_owner (clipboard, targets, 3,
            (GtkClipboardGetFunc) clipboard_get,
            (GtkClipboardClearFunc) clipboard_clear,
            (GObject *) app);
    if (ret && clear)
        gtk_clipboard_clear (clipboard);

    return ret;
}

static gboolean
get_from_clipboard (GSList              **list,
                    DonnaRegisterType    *type,
                    GString             **str,
                    GError              **error)
{
    GtkClipboard *clipboard;
    GtkSelectionData *sd;
    GdkAtom *atoms;
    GdkAtom atom;
    const gchar *s_list;
    const gchar *s, *e;
    gint nb;
    gint i;

    clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (!gtk_clipboard_wait_for_targets (clipboard, &atoms, &nb))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "No files available on CLIPBOARD");
        return FALSE;
    }

    for (i = 0; i < nb; ++i)
    {
        if (atoms[i] == atom_gnome || atoms[i] == atom_kde
                || atoms[i] == atom_uris)
        {
            atom = atoms[i];
            break;
        }
    }
    if (i >= nb)
    {
        g_free (atoms);
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "No supported format for files available in CLIPBOARD");
        return FALSE;
    }

    sd = gtk_clipboard_wait_for_contents (clipboard, atom);
    if (!sd)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Failed to get content from CLIPBOARD");
        return FALSE;
    }

    s = s_list = (const gchar *) gtk_selection_data_get_data (sd);
    if (atom != atom_uris)
    {
        e = strchr (s, '\n');
        if (streqn (s, "cut", 3))
            *type = DONNA_REGISTER_CUT;
        else if (streqn (s, "copy", 4))
            *type = DONNA_REGISTER_COPY;
        else
        {
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Invalid data from CLIPBOARD, unknown operation '%.*s'",
                    (gint) (e - s), s);
            gtk_selection_data_free (sd);
            return FALSE;
        }
        s = e + 1;
    }
    else
        *type = DONNA_REGISTER_UNKNOWN;

    *list = NULL;
    while ((e = strchr (s, '\n')))
    {
        GError *err = NULL;
        gchar buf[255], *b;
        gchar *filename;
        gint len;

        len = (gint) (e - s);
        if (len < 255)
        {
            b = buf;
            snprintf (b, 255, "%.*s", len, s);
        }
        else
            b = g_strdup_printf ("%.*s", len, s);

        filename = g_filename_from_uri (b, NULL, &err);
        if (G_UNLIKELY (!filename))
        {
            if (!*str)
                *str = g_string_new (NULL);

            g_string_append_printf (*str, "\n- Failed to get filename from '%s': %s",
                    b, err->message);
            g_clear_error (&err);
            if (b != buf)
                g_free (b);
            s = e + 1;
            continue;
        }
        if (b != buf)
            g_free (b);

        *list = g_slist_prepend (*list, filename);
        s = e + 1;
    }
    gtk_selection_data_free (sd);
    return TRUE;
}

static inline gboolean
is_valid_register_name (const gchar **name, GError **error)
{
    /* if no name was given (i.e. empty string) we use default name/reg "_" */
    if (**name == '\0')
    {
        *name = "_";
        return TRUE;
    }

    /* register names must start with a letter. Only exceptions are the special
     * names:
     * "+"  is the name for CLIPBOARD (i.e. the system clipboard)
     * "_"  is the name of our "default" register (see above)
     */
    if ((**name >= 'a' && **name <= 'z') || (**name >= 'A' && **name <= 'Z')
            || streq (*name, "+") || streq (*name, "_"))
        return TRUE;

    g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_INVALID_NAME,
            "Invalid register name: '%s'", *name);
    return FALSE;
}

static gboolean
donna_donna_drop_register (DonnaApp       *app,
                           const gchar    *name,
                           GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    gboolean ret;

    if (!is_valid_register_name (&name, error))
        return FALSE;

    if (*name == '+')
        take_clipboard_ownership (app, TRUE);
    else
    {
        g_rec_mutex_lock (&priv->rec_mutex);
        ret = g_hash_table_remove (priv->registers, name);
        g_rec_mutex_unlock (&priv->rec_mutex);
    }

    return ret;
}

static inline void
add_node_to_reg (struct reg *reg, DonnaNode *node, gboolean is_clipboard)
{
    GSList *l;
    gchar *s;

    if (is_clipboard)
        s = donna_node_get_location (node);
    else
        s = donna_node_get_full_location (node);

    /* avoid dupes */
    for (l = reg->list; l; l = l->next)
        if (streq (l->data, s))
        {
            g_free (s);
            return;
        }
    reg->list = g_slist_prepend (reg->list, s);
}

static gboolean
donna_donna_set_register (DonnaApp       *app,
                          const gchar    *name,
                          DonnaRegisterType type,
                          GPtrArray      *nodes,
                          GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    DonnaProvider *pfs;
    struct reg *reg;
    gboolean is_clipboard;
    guint i;

    if (!is_valid_register_name (&name, error))
        return FALSE;

    is_clipboard = *name == '+';
    if (is_clipboard)
        pfs = donna_donna_get_provider (app, "fs");

    reg = g_new (struct reg, 1);
    reg->type = type;
    reg->list = NULL;
    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            add_node_to_reg (reg, nodes->pdata[i], is_clipboard);

    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_insert (priv->registers, g_strdup (name), reg);
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (is_clipboard)
        take_clipboard_ownership (app, FALSE);

    return TRUE;
}

static gboolean
donna_donna_add_to_register (DonnaApp       *app,
                             const gchar    *name,
                             GPtrArray      *nodes,
                             GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    DonnaProvider *pfs;
    struct reg *reg;
    gboolean is_clipboard;
    guint i;

    if (!is_valid_register_name (&name, error))
        return FALSE;

    is_clipboard = *name == '+';
    if (is_clipboard)
        pfs = donna_donna_get_provider (app, "fs");

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = g_hash_table_lookup (priv->registers, name);
    if (!reg)
    {
        if (is_clipboard)
        {
            GString *str = NULL;

            reg = g_new0 (struct reg, 1);
            if (!get_from_clipboard (&reg->list, &reg->type, &str, error))
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                g_prefix_error (error, "Couldn't append files to CLIPBOARD: ");
                g_free (reg);
                return FALSE;
            }
            else if (str)
            {
                g_warning ("Failed to get some files from CLIPBOARD: %s", str->str);
                g_string_free (str, TRUE);
            }
            g_hash_table_insert (priv->registers, g_strdup (name), reg);
            take_clipboard_ownership (app, FALSE);
        }
        else
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_NOT_FOUND,
                    "Cannot add to register '%s', it doesn't exist.", name);
            return FALSE;
        }
    }

    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            add_node_to_reg (reg, nodes->pdata[i], is_clipboard);

    g_rec_mutex_unlock (&priv->rec_mutex);
    return TRUE;
}

static gboolean
donna_donna_set_register_type (DonnaApp       *app,
                               const gchar    *name,
                               DonnaRegisterType type,
                               GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    struct reg *reg;
    gboolean is_clipboard;

    if (!is_valid_register_name (&name, error))
        return FALSE;

    is_clipboard = *name == '+';
    g_rec_mutex_lock (&priv->rec_mutex);
    reg = g_hash_table_lookup (priv->registers, name);
    if (!reg)
    {
        if (is_clipboard)
        {
            GString *str = NULL;

            reg = g_new0 (struct reg, 1);
            if (!get_from_clipboard (&reg->list, &reg->type, &str, error))
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                g_prefix_error (error, "Couldn't set register type of CLIPBOARD: ");
                g_free (reg);
                return FALSE;
            }
            else if (str)
            {
                g_warning ("Failed to get some files from CLIPBOARD: %s", str->str);
                g_string_free (str, TRUE);
            }
            g_hash_table_insert (priv->registers, g_strdup (name), reg);
            take_clipboard_ownership (app, FALSE);
        }
        else
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_NOT_FOUND,
                    "Cannot set type of register '%s', it doesn't exist.", name);
            return FALSE;
        }
    }

    reg->type = type;

    g_rec_mutex_unlock (&priv->rec_mutex);
    return TRUE;
}

static gboolean
donna_donna_get_register_nodes (DonnaApp       *app,
                                const gchar    *name,
                                DonnaDropRegister drop,
                                DonnaRegisterType *type,
                                GPtrArray     **nodes,
                                GError        **error)
{
    DonnaDonnaPrivate *priv = ((DonnaDonna *) app)->priv;
    DonnaProvider *pfs = NULL;
    struct reg *reg = NULL;
    DonnaRegisterType reg_type;
    GSList *list = NULL;
    GSList *l;
    GString *str = NULL;

    if (!is_valid_register_name (&name, error))
        return FALSE;

    if (*name == '+')
    {
        if (!get_from_clipboard (&list, &reg_type, &str, error))
            return FALSE;

        pfs = donna_donna_get_provider (app, "fs");
    }
    else
    {
        g_rec_mutex_lock (&priv->rec_mutex);
        reg = g_hash_table_lookup (priv->registers, name);
        if (!reg)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_NOT_FOUND,
                    "Cannot get nodes from register '%s', it doesn't exist.", name);
            return FALSE;
        }
        list = reg->list;
        reg_type = reg->type;
    }

    *nodes = g_ptr_array_new_full (g_slist_length (list), g_object_unref);
    for (l = list; l; l = l->next)
    {
        DonnaTask *task;

        if (pfs)
            task = donna_provider_get_node_task (pfs, l->data, NULL);
        else
            task = donna_app_get_node_task (app, l->data);
        donna_task_set_can_block (g_object_ref_sink (task));
        donna_donna_run_task (app, task);
        donna_task_wait_for_it (task);

        if (donna_task_get_state (task) == DONNA_TASK_DONE)
            g_ptr_array_add (*nodes,
                    g_value_dup_object (donna_task_get_return_value (task)));
        else if (error)
        {
            const GError *e = donna_task_get_error (task);

            if (!str)
                str = g_string_new (NULL);

            if (e)
                g_string_append_printf (str, "\n- Failed to get node for '%s': %s",
                        (gchar *) l->data, e->message);
            else
                g_string_append_printf (str, "\n- Failed to get node for '%s'",
                        (gchar *) l->data);
        }

        g_object_unref (task);
    }

    if (str)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Not all nodes from register '%s' could be loaded:\n%s",
                name, str->str);
        g_string_free (str, TRUE);
    }

    if (type)
        *type = reg_type;

    if (reg)
    {
        if (drop == DONNA_DROP_REGISTER_ALWAYS
                || (drop == DONNA_DROP_REGISTER_ON_CUT
                    && reg_type == DONNA_REGISTER_CUT))
            g_hash_table_remove (priv->registers, name);

        g_rec_mutex_unlock (&priv->rec_mutex);
    }
    else
    {
        g_slist_free_full (list, g_free);

        if (drop == DONNA_DROP_REGISTER_ALWAYS
                || (drop == DONNA_DROP_REGISTER_ON_CUT
                    && reg_type == DONNA_REGISTER_CUT))
            take_clipboard_ownership (app, TRUE);
    }

    return TRUE;
}

static void
window_destroy_cb (GtkWidget *window, gpointer data)
{
    gtk_main_quit ();
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
    g_timeout_add (420, (GSourceFunc) just_focused_expired, donna);
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
active_location_changed (GObject *object, GParamSpec *spec, DonnaDonna *donna)
{
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

static void
set_tree_location (DonnaTask *task, gboolean timeout_called, DonnaTreeView *tree)
{
    if (donna_task_get_state (task) != DONNA_TASK_DONE)
        return;
    donna_tree_view_set_location (tree,
            g_value_get_object (donna_task_get_return_value (task)),
            NULL);
}

static GtkWidget *
load_widget (DonnaDonna  *donna,
             gchar      **def,
             GSList     **list,
             gchar      **active_list_name,
             GtkWidget  **active_list_widget)
{
    DonnaDonnaPrivate *priv = donna->priv;
    GtkWidget *w;
    gchar *end;
    gchar *sep = NULL;

    for ( ; isblank (**def); ++*def)
        ;

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
                    w = load_widget (donna, def, list,
                            active_list_name, active_list_widget);
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
                w = load_widget (donna, def, list,
                        active_list_name, active_list_widget);
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
                w = load_widget (donna, def, list,
                        active_list_name, active_list_widget);
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
                DonnaTask *task;
                gchar *s = NULL;

                *def = sep + 1;
                *end = '\0';

                w = gtk_scrolled_window_new (NULL, NULL);
                tree = donna_load_treeview (donna, *def);
                if (!donna_tree_view_is_tree (tree))
                {
                    if (!priv->active_list)
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
                    if (!donna_config_get_string (donna_donna_peek_config (
                                    (DonnaApp *) donna),
                                &s, "treeviews/%s/location", *def))
                    {
                        gchar *pwd;

                        pwd = getcwd (NULL, 0);
                        if (pwd)
                        {
                            s = g_strdup_printf ("fs:%s", pwd);
                            free (pwd);
                        }
                    }
                    if (s)
                    {
                        task = donna_app_get_node_task ((DonnaApp *) donna, s);
                        donna_task_set_callback (task,
                                (task_callback_fn) set_tree_location, tree, NULL);
                        *list = g_slist_prepend (*list, task);
                        g_free (s);
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

int
main (int argc, char *argv[])
{
    DonnaApp            *app;
    DonnaDonnaPrivate   *priv;
    GtkCssProvider      *css_provider;
    GtkWindow           *window;
    GtkWidget           *active_list_widget = NULL;
    GtkWidget           *w;
    gchar               *active_list_name;
    GSList              *list = NULL;
    GSList              *l;
    gchar               *s;
    gchar               *ss;
    gchar               *def;
    gchar               *areas;
    gint                 width;
    gint                 height;

    setlocale (LC_ALL, "");
    gtk_init (&argc, &argv);

    app = g_object_new (DONNA_TYPE_DONNA, NULL);
    priv = ((DonnaDonna *) app)->priv;

    /* CSS */
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (css_provider, "donnatella.css", NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
            (GtkStyleProvider *) css_provider,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* main window */
    window = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    priv->window = g_object_ref (window);

    g_signal_connect (window, "focus-in-event",
            (GCallback) focus_in_event_cb, app);
    g_signal_connect (window, "destroy",
            (GCallback) window_destroy_cb, NULL);

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
    w = load_widget ((DonnaDonna *) app, &def, &list,
            &active_list_name, &active_list_widget);
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

    /* now that everything is realized, we can trigger task to set tree's
     * location. Before that could lead to issue as set_location() might get
     * treeview to wanna refresh, scroll, etc which needs it to be realized to
     * work */
    for (l = list; l; l = l->next)
        donna_donna_run_task (app, l->data);
    g_slist_free (list);

    gtk_main ();
    return 0;
}
