
#include <gtk/gtk.h>
#include "donna.h"
#include "sharedstring.h"
#include "provider-config.h"
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "macros.h"

enum
{
    COL_TYPE_NAME = 0,
    NB_COL_TYPES
};

struct _DonnaDonnaPrivate
{
    DonnaConfig *config;
    GSList      *arrangements;
    struct col_type
    {
        const gchar           *name;
        column_type_loader_fn  load;
        DonnaColumnType       *ct;
    } column_types[NB_COL_TYPES];
};

struct argmt
{
    DonnaSharedString   *name;
    GPatternSpec        *pspec;
};

static GObject *    donna_donna_constructor     (GType                   type,
                                                 guint                   n_params,
                                                 GObjectConstructParam  *params);
static void         donna_donna_finalize        (GObject                *object);

static void
donna_donna_class_init (DonnaDonnaClass *klass)
{
    GObjectClass *o_class;

    /* register the new fundamental type SharedString */
    donna_shared_string_register ();

    o_class = G_OBJECT_CLASS (klass);
    o_class->constructor = donna_donna_constructor;
    o_class->finalize    = donna_donna_finalize;

    g_type_class_add_private (klass, sizeof (DonnaDonnaPrivate));
}

static void
donna_donna_init (DonnaDonna *donna)
{
    DonnaDonnaPrivate *priv;
    GPtrArray *arr = NULL;
    guint i;

    priv = donna->priv = G_TYPE_INSTANCE_GET_PRIVATE (donna,
            DONNA_TYPE_DONNA, DonnaDonnaPrivate);

    priv->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, NULL);
    priv->column_types[COL_TYPE_NAME].name = "name";
    priv->column_types[COL_TYPE_NAME].load = donna_column_type_name_new;

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
        DonnaSharedString *ss;
        const gchar *s;

        /* ignore "tree" and "list" arrangements */
        s = arr->pdata[i];
        if (s[0] < '0' || s[0] > '9')
            continue;

        if (!donna_config_get_shared_string (priv->config, &ss,
                    "arrangements/%s/mask",
                    s))
        {
            g_warning ("Arrangement '%s' has no mask set, skipping", s);
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        argmt->name  = donna_shared_string_new_dup (s);
        argmt->pspec = g_pattern_spec_new (donna_shared_string (ss));
        priv->arrangements = g_slist_append (priv->arrangements, argmt);
        donna_shared_string_unref (ss);
    }
    g_ptr_array_free (arr, TRUE);

skip_arrangements:
    return;
}

G_DEFINE_TYPE (DonnaDonna, donna_donna, G_TYPE_OBJECT)

static GObject *donna = NULL;
static GObject *
donna_donna_constructor (GType                   type,
                         guint                   n_params,
                         GObjectConstructParam  *params)
{
    GObject *obj;

    if (!donna)
        obj = donna = G_OBJECT_CLASS (donna_donna_parent_class)->constructor (
                type, n_params, params);
    else
        obj = g_object_ref (donna);

    return obj;
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

        donna_shared_string_unref (argmt->name);
        g_pattern_spec_free (argmt->pspec);
        g_free (argmt);
    }

    G_OBJECT_CLASS (donna_donna_parent_class)->finalize (object);
}

DonnaConfig *
donna_app_get_config (DonnaDonna *donna)
{
    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);
    return g_object_ref (donna->priv->config);
}

DonnaProvider *
donna_app_get_provider (DonnaDonna     *donna,
                        const gchar    *domain)
{
    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);
}

DonnaColumnType *
donna_app_get_columntype (DonnaDonna     *donna,
                          const gchar    *type)
{
    DonnaDonnaPrivate *priv;
    gint i;

    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);
    g_return_val_if_fail (type != NULL, NULL);

    priv = donna->priv;

    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            if (!priv->column_types[i].ct)
                priv->column_types[i].ct = priv->column_types[i].load (donna);
            break;
        }
    }
    return (i < NB_COL_TYPES) ? g_object_ref (priv->column_types[i].ct) : NULL;
}

DonnaSharedString *
donna_app_get_arrangement (DonnaDonna     *donna,
                           DonnaNode      *node)
{
    DonnaDonnaPrivate *priv;
    GSList *l;
    DonnaSharedString *location;
    const gchar *domain;
    DonnaSharedString *arr;
    gchar  buf[255];
    gchar *b = buf;
    gsize  len;

    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    priv = donna->priv;

    /* get full location of node */
    donna_node_get (node, FALSE, "domain", &domain, "location", &location, NULL);
    len = snprintf (buf, 255, "%s:%s", domain, donna_shared_string (location));
    if (len >= 255)
        b = g_strdup_printf ("%s:%s", domain, donna_shared_string (location));
    donna_shared_string_unref (location);

    arr = NULL;
    for (l = priv->arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        if (g_pattern_match_string (argmt->pspec, b))
        {
            arr = donna_shared_string_ref (argmt->name);
            break;
        }
    }
    if (b != buf)
        g_free (b);

    return arr;
}

static void
_run_task (DonnaTask *task)
{
    donna_task_run (task);
    g_object_unref (task);
}

void
donna_app_run_task (DonnaDonna     *donna,
                    DonnaTask      *task)
{
    g_return_if_fail (DONNA_IS_DONNA (donna));
    g_return_if_fail (DONNA_IS_TASK (task));

    /* FIXME thread pool */
    g_thread_unref (g_thread_new ("run-task",
                (GThreadFunc) _run_task,
                g_object_ref_sink (task)));
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
    DonnaConfig *config = donna_app_get_config (d);
    gboolean v;
    if (donna_config_get_boolean (config, &v, "columns/name/sort_natural_order"))
        donna_config_set_boolean (config, !v, "columns/name/sort_natural_order");
    else
        donna_config_set_boolean (config, FALSE, "columns/name/sort_natural_order");
    g_object_unref (config);

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
    donna_app_run_task (d, task);
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
    GtkTreeModel    *list_model;
    GtkListStore    *list_store;
    GtkWidget       *_list;
    GtkTreeView     *list;

    gtk_init (&argc, &argv);
    d= g_object_new (DONNA_TYPE_DONNA, NULL);

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

    /* paned to host tree & list */
    _paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    paned = GTK_PANED (_paned);
    gtk_box_pack_start (box, _paned, TRUE, TRUE, 0);
    gtk_widget_show (_paned);

    /* tree */
    DonnaConfig *config = donna_app_get_config (d);
    donna_config_set_uint (config, 1, "treeviews/tree/mode");
    donna_config_set_string_dup (config, "name", "treeviews/tree/arrangement/sort");
    g_object_unref (config);
    _tree = donna_tree_view_new (d, "tree");
    tree = GTK_TREE_VIEW (_tree);
    /* scrolled window */
    _scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_paned_pack1 (paned, _scrolled_window, FALSE, TRUE);
    gtk_widget_show (_scrolled_window);
    /* size */
    gtk_container_add (GTK_CONTAINER (_scrolled_window), _tree);
    gtk_widget_show (_tree);

    /* list */
    list_store = gtk_list_store_new (1, G_TYPE_STRING);
    list_model = GTK_TREE_MODEL (list_store);
    _list = gtk_tree_view_new_with_model (list_model);
    list = GTK_TREE_VIEW (_list);
    gtk_paned_pack2 (paned, _list, TRUE, TRUE);
    gtk_widget_show (_list);

    /* tb signals */
    GtkTreeModel *model;
    model = gtk_tree_view_get_model (tree);
    g_signal_connect (G_OBJECT (tb_btn), "clicked",
            G_CALLBACK (tb_fill_tree_clicked_cb), tree);
    g_signal_connect (G_OBJECT (tb_btn2), "clicked",
            G_CALLBACK (tb_new_root_clicked_cb), tree);

    DonnaTask *task;
    const GValue *value;
    DonnaNode *node;

    task = donna_provider_get_node_task (DONNA_PROVIDER (provider_fs), "/");
    donna_task_run (task);
    value = donna_task_get_return_value (task);
    node = g_value_get_object (value);
    donna_tree_view_add_root (DONNA_TREE_VIEW (tree), node);
    g_object_unref (task);

    /* show everything */
    gtk_window_set_default_size (window, 420, 230);
    gtk_paned_set_position (paned, 230);
    gtk_widget_show (_window);
    gtk_main ();

    return 0;
}
