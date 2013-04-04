
#include <locale.h>
#include <gtk/gtk.h>
#include "donna.h"
#include "debug.h"
#include "app.h"
#include "provider-config.h"
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "macros.h"

guint donna_debug_flags = 0;

enum
{
    PROP_0,

    PROP_ACTIVE_LIST,

    NB_PROPS
};

enum
{
    COL_TYPE_NAME = 0,
    NB_COL_TYPES
};

struct _DonnaDonnaPrivate
{
    DonnaConfig     *config;
    GSList          *arrangements;
    GThreadPool     *pool;
    DonnaTreeView   *active_list;
    struct col_type
    {
        const gchar           *name;
        column_type_loader_fn  load;
        DonnaColumnType       *ct;
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
static DonnaConfig *    donna_donna_get_config      (DonnaApp       *app);
static DonnaProvider *  donna_donna_get_provider    (DonnaApp       *app,
                                                     const gchar    *domain);
static DonnaColumnType *donna_donna_get_columntype  (DonnaApp       *app,
                                                     const gchar    *type);
static gchar *          donna_donna_get_arrangement (DonnaApp       *app,
                                                     DonnaNode      *node);
static void             donna_donna_run_task        (DonnaApp       *app,
                                                     DonnaTask      *task);
static DonnaTreeView *  donna_donna_get_treeview    (DonnaApp       *app,
                                                     const gchar    *name);

static void
donna_donna_app_init (DonnaAppInterface *interface)
{
    interface->get_config       = donna_donna_get_config;
    interface->get_provider     = donna_donna_get_provider;
    interface->get_columntype   = donna_donna_get_columntype;
    interface->get_arrangement  = donna_donna_get_arrangement;
    interface->run_task         = donna_donna_run_task;
    interface->get_treeview     = donna_donna_get_treeview;
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

    g_type_class_add_private (klass, sizeof (DonnaDonnaPrivate));
}

static void
donna_donna_task_run (DonnaTask *task)
{
    donna_task_run (task);
    g_object_unref (task);
}

static void
donna_donna_init (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv;
    GPtrArray *arr = NULL;
    guint i;

    mt = g_thread_self ();
    g_log_set_default_handler (donna_donna_log_handler, NULL);

    priv = donna->priv = G_TYPE_INSTANCE_GET_PRIVATE (donna,
            DONNA_TYPE_DONNA, DonnaDonnaPrivate);

    priv->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, NULL);
    priv->column_types[COL_TYPE_NAME].name = "name";
    priv->column_types[COL_TYPE_NAME].load = donna_column_type_name_new;

    priv->pool = g_thread_pool_new ((GFunc) donna_donna_task_run, NULL,
            5, FALSE, NULL);

    /* load the config */
    /* TODO */

    /* compile patterns of arrangements' masks */
    if (!donna_config_list_options (priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_CATEGORY,
                "arrangements"))
    {
        g_warning ("Unable to load arrangements");
        goto skip_arrangements;
    }

    for (i = 0; i < arr->len; ++i)
    {
        struct argmt *argmt;
        gchar *ss;
        const gchar *s;

        /* ignore "tree" and "list" arrangements */
        s = arr->pdata[i];
        if (s[0] < '0' || s[0] > '9')
            continue;

        if (!donna_config_get_string (priv->config, &ss,
                    "arrangements/%s/mask",
                    s))
        {
            g_warning ("Arrangement '%s' has no mask set, skipping", s);
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        argmt->name  = g_strdup (s);
        argmt->pspec = g_pattern_spec_new (ss);
        priv->arrangements = g_slist_prepend (priv->arrangements, argmt);
        g_free (ss);
    }
    priv->arrangements = g_slist_reverse (priv->arrangements);
    g_ptr_array_free (arr, TRUE);

skip_arrangements:
    return;
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
}

static void
donna_donna_finalize (GObject *object)
{
    DonnaDonnaPrivate *priv;
    GSList *l;

    priv = DONNA_DONNA (object)->priv;

    g_object_unref (priv->config);

    for (l = priv->arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        g_free (argmt->name);
        g_pattern_spec_free (argmt->pspec);
        g_free (argmt);
    }

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

DonnaConfig *
donna_donna_get_config (DonnaApp *app)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    return g_object_ref (DONNA_DONNA (app)->priv->config);
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
    g_return_val_if_fail (type != NULL, NULL);

    priv = DONNA_DONNA (app)->priv;

    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            if (!priv->column_types[i].ct)
                priv->column_types[i].ct = priv->column_types[i].load (app);
            break;
        }
    }
    return (i < NB_COL_TYPES) ? g_object_ref (priv->column_types[i].ct) : NULL;
}

gchar *
donna_donna_get_arrangement (DonnaApp   *app,
                             DonnaNode  *node)
{
    DonnaDonnaPrivate *priv;
    GSList *l;
    gchar *location;
    const gchar *domain;
    gchar *arr;
    gchar  buf[255];
    gchar *b = buf;
    gsize  len;

    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    priv = DONNA_DONNA (app)->priv;

    /* get full location of node */
    donna_node_get (node, FALSE, "domain", &domain, "location", &location, NULL);
    len = snprintf (buf, 255, "%s:%s", domain, location);
    if (len >= 255)
        b = g_strdup_printf ("%s:%s", domain, location);
    g_free (location);

    arr = NULL;
    for (l = priv->arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        if (g_pattern_match_string (argmt->pspec, b))
        {
            arr = g_strdup (argmt->name);
            break;
        }
    }
    if (b != buf)
        g_free (b);

    return arr;
}

void
donna_donna_run_task (DonnaApp    *app,
                      DonnaTask   *task)
{
    g_return_if_fail (DONNA_IS_DONNA (app));
    g_return_if_fail (DONNA_IS_TASK (task));

    /* FIXME if task is public, add to task manager */
    g_thread_pool_push (DONNA_DONNA (app)->priv->pool,
            g_object_ref_sink (task), NULL);
}


static DonnaTreeView *
donna_donna_get_treeview (DonnaApp       *app,
                          const gchar    *name)
{
    g_return_val_if_fail (DONNA_IS_DONNA (app), NULL);
    /* FIXME */
    return DONNA_DONNA (app)->priv->active_list;
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
            "/home/jjacky/donnatella");
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    donna_tree_view_set_location (tree, node, NULL);

    return;

    /*******************************/

    DonnaConfig *config = donna_app_get_config (DONNA_APP (d));
    gboolean v;
    if (donna_config_get_boolean (config, &v, "columns/name/sort_natural_order"))
        donna_config_set_boolean (config, !v, "columns/name/sort_natural_order");
    else
        donna_config_set_boolean (config, FALSE, "columns/name/sort_natural_order");
    g_object_unref (config);
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
            "/tmp/test");
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

    donna_node_get (node, FALSE, "location", &s, NULL);
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
            "/tmp/test/foobar");
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

    provider_fs = g_object_new (DONNA_TYPE_PROVIDER_FS, NULL);

    /* main window */
    _window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    window = GTK_WINDOW (_window);

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
    DonnaConfig *config = donna_app_get_config (DONNA_APP (d));
    donna_config_set_uint (config, 1, "treeviews/tree/mode");
    donna_config_set_string (config, "name", "treeviews/tree/arrangement/sort");
    g_object_unref (config);
    _tree = donna_tree_view_new (DONNA_APP (d), "tree");
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
    _list = donna_tree_view_new (DONNA_APP (d), "list");
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
            G_CALLBACK (tb_fill_tree_clicked_cb), tree);
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
    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/");
    g_object_ref_sink (task);
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_dup_object (value);
    g_object_unref (task);
    donna_tree_view_set_location (DONNA_TREE_VIEW (tree), node, NULL);
    g_object_unref (node);
#endif

    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/tmp/test");
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
