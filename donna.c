
#include <locale.h>
#include <gtk/gtk.h>
#include "donna.h"
#include "debug.h"
#include "app.h"
#include "provider.h"
#include "provider-config.h"
#include "columntype.h"
#include "columntype-name.h"
#include "columntype-size.h"
#include "columntype-time.h"
#include "columntype-perms.h"
#include "columntype-text.h"
#include "node.h"
#include "filter.h"
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

struct _DonnaDonnaPrivate
{
    GtkWindow       *window;
    GtkWidget       *floating_window;
    gboolean         just_focused;
    DonnaConfig     *config;
    GSList          *treeviews;
    GHashTable      *filters;
    GSList          *arrangements;
    GHashTable      *visuals;
    GThreadPool     *pool;
    DonnaTreeView   *active_list;
    struct col_type
    {
        const gchar     *name;
        GType            type;
        DonnaColumnType *ct;
    } column_types[NB_COL_TYPES];
};

struct argmt
{
    gchar        *name;
    GPatternSpec *pspec;
};

static GThread *mt;
static GLogLevelFlags show_log = G_LOG_LEVEL_DEBUG;

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
static DonnaTreeView *  donna_donna_get_treeview    (DonnaApp       *app,
                                                     const gchar    *name);
static void             donna_donna_show_error      (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);

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
    interface->get_treeview         = donna_donna_get_treeview;
    interface->show_error           = donna_donna_show_error;
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

static void
donna_donna_task_run (DonnaTask *task)
{
    donna_task_run (task);
    g_object_unref (task);
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
donna_donna_init (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv;
    GPtrArray *arr = NULL;

    mt = g_thread_self ();
    g_log_set_default_handler (donna_donna_log_handler, NULL);

    priv = donna->priv = G_TYPE_INSTANCE_GET_PRIVATE (donna,
            DONNA_TYPE_DONNA, DonnaDonnaPrivate);

    priv->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, NULL);
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

    priv->pool = g_thread_pool_new ((GFunc) donna_donna_task_run, NULL,
            5, FALSE, NULL);

    /* load the config */
    /* TODO */

    /* compile patterns of arrangements' masks */
    priv->arrangements = load_arrangements (priv->config, "arrangements");

    priv->filters = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, g_object_unref);

    priv->visuals = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_visuals);

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
    {
        if (priv->active_list)
            g_object_unref (priv->active_list);
        priv->active_list = g_value_dup_object (value);
    }
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

    priv = DONNA_DONNA (object)->priv;

    g_object_unref (priv->config);
    free_arrangements (priv->arrangements);
    g_hash_table_destroy (priv->filters);
    g_hash_table_destroy (priv->visuals);
    g_thread_pool_free (priv->pool, TRUE, FALSE);

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

DonnaProvider *
donna_donna_get_provider (DonnaApp    *app,
                          const gchar *domain)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    /* TODO */
}

DonnaColumnType *
donna_donna_get_columntype (DonnaApp       *app,
                            const gchar    *type)
{
    DonnaDonnaPrivate *priv;
    gint i;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    priv = DONNA_DONNA (app)->priv;

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
    return (i < NB_COL_TYPES) ? g_object_ref (priv->column_types[i].ct) : NULL;
}

DonnaFilter *
donna_donna_get_filter (DonnaApp    *app,
                        const gchar *filter)
{
    DonnaDonnaPrivate *priv;
    DonnaFilter *filter_obj;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);

    priv = ((DonnaDonna *) app)->priv;

    filter_obj = g_hash_table_lookup (priv->filters, filter);
    if (!filter_obj)
    {
        filter_obj = g_object_new (DONNA_TYPE_FILTER,
                "app",      app,
                "filter",   filter,
                NULL);
        g_hash_table_insert (priv->filters, g_strdup (filter),
                g_object_ref (filter_obj));
    }

    return filter_obj;
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
    /* FIXME else if (visibility == DONNA_TASK_VISIBILITY_PULIC)
     *      add_to_task_manager (task); */
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

DonnaTreeView *
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
    if (error)
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (w), "%s",
                error->message);
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show_all (w);
}

void
donna_donna_set_window (DonnaDonna *donna, GtkWindow *win)
{
    g_return_if_fail (DONNA_IS_DONNA (donna));
    donna->priv->window = g_object_ref (win);
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
    g_free (fl);
}














#include "treeview.h"
#include "provider.h"
#include "provider-fs.h"
#include "task.h"

static DonnaProviderFs *provider_fs;
static DonnaDonna *d;

static void
window_destroy_cb (GtkWidget *window, gpointer data)
{
    gtk_main_quit ();
}

static void
tb_fill_tree_clicked_cb (GtkToolButton *tb_btn, DonnaTreeView *tree)
{
    DonnaTask *task;
    const GValue *value;
    DonnaNode *node;

    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs),
            "/home/jjacky/donnatella/donna.c", NULL);
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    donna_tree_view_set_location (tree, node, NULL);

    return;

    /*******************************/

    DonnaConfig *config = donna_app_peek_config (DONNA_APP (d));
    gboolean v;
    if (donna_config_get_boolean (config, &v, "columns/name/sort_natural_order"))
        donna_config_set_boolean (config, !v, "columns/name/sort_natural_order");
    else
        donna_config_set_boolean (config, FALSE, "columns/name/sort_natural_order");
    return;

    /* FIXME */
    GtkTreeSortable *sortable;
    sortable = GTK_TREE_SORTABLE (gtk_tree_model_filter_get_model (
                GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (
                        GTK_TREE_VIEW (tree)))));
    gtk_tree_sortable_set_sort_column_id (sortable, 0, GTK_SORT_DESCENDING);
    gtk_tree_sortable_set_sort_column_id (sortable, 0, GTK_SORT_ASCENDING);
}

static void
new_root_cb (DonnaTask *task, gboolean timeout_called, gpointer data)
{
    DonnaNode       *node;
    const GValue    *value;

    value = donna_task_get_return_value (task);
    node = DONNA_NODE (g_value_get_object (value));

    /* FIXME if (!node) */
    donna_tree_view_add_root (DONNA_TREE_VIEW (data), node);
}

static void
tb_new_root_clicked_cb (GtkToolButton *tb_btn, DonnaTreeView *tree)
{
    DonnaTask *task;

    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs),
            "/", NULL);
    donna_task_set_callback (task, new_root_cb, tree, NULL);
    donna_app_run_task (DONNA_APP (d), task);
}

static void
tb_del_node_clicked_cb (GtkToolButton *tb_btn, DonnaTreeView *tree)
{
    DonnaNode *node;
    gchar *s;

    node = donna_tree_view_get_location (tree);
    if (!node)
    {
        g_info ("Tree has no current location");
        return;
    }

    s = donna_node_get_location (node);
    g_info ("Tree's location: %s", s);
    g_free (s);
    g_object_unref (node);
}

static void
tb_add_node_clicked_cb (GtkToolButton *tb_btn, DonnaTreeView *tree)
{
    DonnaTask *task;
    DonnaNode *node;
    const GValue *value;
    GValue val = G_VALUE_INIT;
    gchar *s;

    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs),
            "/tmp/test/foobar", NULL);
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    s = g_strdup ("name");
    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, "barfoo");
    donna_tree_view_set_node_property (tree, node, s, &val, NULL);
    g_value_unset (&val);
    g_free (s);
    g_object_unref (node);
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

int
main (int argc, char *argv[])
{
    GtkWidget       *_window;
    GtkWindow       *window;
    GtkWidget       *_box;
    GtkBox          *box;
    GtkWidget       *_tb;
    GtkToolbar      *tb;
    GtkWidget       *_paned;
    GtkPaned        *paned;
    GtkWidget       *_scrolled_window;
    GtkWidget       *_tree;
    GtkTreeView     *tree;
    GtkWidget       *_list;
    GtkTreeView     *list;

    setlocale (LC_ALL, "");
    gtk_init (&argc, &argv);
    d = g_object_new (DONNA_TYPE_DONNA, NULL);

    /* CSS */
    GtkCssProvider *css_provider;
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (css_provider, "donnatella.css", NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
            (GtkStyleProvider *) css_provider,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    provider_fs = g_object_new (DONNA_TYPE_PROVIDER_FS, NULL);
    g_signal_connect (provider_fs, "new-node", (GCallback) new_node_cb, d);

    /* main window */
    _window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    window = GTK_WINDOW (_window);
    donna_donna_set_window (d, window);

    g_signal_connect (window, "focus-in-event",
            (GCallback) focus_in_event_cb, d);
    g_signal_connect (G_OBJECT (window), "destroy",
            G_CALLBACK (window_destroy_cb), NULL);

    gtk_window_set_title (window, "Donnatella");

    _box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    box = GTK_BOX (_box);
    gtk_container_add (GTK_CONTAINER (window), _box);
    gtk_widget_show (_box);

    /* toolbar */
    _tb = gtk_toolbar_new ();
    tb = GTK_TOOLBAR (_tb);
    gtk_toolbar_set_icon_size (tb, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_box_pack_start (box, _tb, FALSE, FALSE, 0);
    gtk_widget_show (_tb);

    GtkToolItem *tb_btn;
    tb_btn = gtk_tool_button_new_from_stock (GTK_STOCK_APPLY);
    gtk_toolbar_insert (tb, tb_btn, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn));

    GtkToolItem *tb_btn2;
    tb_btn2 = gtk_tool_button_new_from_stock (GTK_STOCK_REFRESH);
    gtk_toolbar_insert (tb, tb_btn2, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn2));

    GtkToolItem *tb_btn3;
    tb_btn3 = gtk_tool_button_new_from_stock (GTK_STOCK_REMOVE);
    gtk_toolbar_insert (tb, tb_btn3, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn3));

    GtkToolItem *tb_btn4;
    tb_btn4 = gtk_tool_button_new_from_stock (GTK_STOCK_ADD);
    gtk_toolbar_insert (tb, tb_btn4, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn4));

    /* paned to host tree & list */
    _paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    paned = GTK_PANED (_paned);
    gtk_box_pack_start (box, _paned, TRUE, TRUE, 0);
    gtk_widget_show (_paned);

    /* tree */
    DonnaConfig *config = donna_app_peek_config (DONNA_APP (d));
    donna_config_set_uint (config, 1, "treeviews/tree/mode");
    donna_config_set_string (config, "name", "treeviews/tree/arrangement/sort");
    _tree = (GtkWidget *) donna_load_treeview (d, "tree");
    tree = GTK_TREE_VIEW (_tree);
    /* scrolled window */
    _scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_paned_pack1 (paned, _scrolled_window, FALSE, TRUE);
    gtk_widget_show (_scrolled_window);
    /* size */
    gtk_container_add (GTK_CONTAINER (_scrolled_window), _tree);
    gtk_widget_show (_tree);

    /* list */
    donna_config_set_string (config, "name", "treeviews/list/arrangement/sort");
    _list = (GtkWidget *) donna_load_treeview (d, "list");
    list = GTK_TREE_VIEW (_list);
    /* scrolled window */
    _scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_paned_pack2 (paned, _scrolled_window, TRUE, TRUE);
    gtk_widget_show (_scrolled_window);
    /* size */
    gtk_container_add (GTK_CONTAINER (_scrolled_window), _list);
    gtk_widget_show (_list);

    /* tb signals */
    GtkTreeModel *model;
    model = gtk_tree_view_get_model (tree);
    g_signal_connect (G_OBJECT (tb_btn), "clicked",
            G_CALLBACK (tb_fill_tree_clicked_cb), list);
    g_signal_connect (G_OBJECT (tb_btn2), "clicked",
            G_CALLBACK (tb_new_root_clicked_cb), tree);
    g_signal_connect (G_OBJECT (tb_btn3), "clicked",
            G_CALLBACK (tb_del_node_clicked_cb), tree);
    g_signal_connect (G_OBJECT (tb_btn4), "clicked",
            G_CALLBACK (tb_add_node_clicked_cb), tree);

    DonnaTask *task;
    const GValue *value;
    DonnaNode *node;

    d->priv->active_list = DONNA_TREE_VIEW (list);
    g_object_notify (G_OBJECT (d), "active-list");

#if 0
    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/", NULL);
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    donna_tree_view_set_location (DONNA_TREE_VIEW (tree), node, NULL);
    g_object_unref (node);
#endif

    //task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/tmp/test", NULL);
    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/home/jjacky/issue", NULL);
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    donna_tree_view_set_location (DONNA_TREE_VIEW (list), node, NULL);
    g_object_unref (node);

    /* show everything */
    gtk_window_set_default_size (window, 1080, 420);
    gtk_paned_set_position (paned, 230);
    gtk_widget_show (_window);
    gtk_main ();

    return 0;
}
