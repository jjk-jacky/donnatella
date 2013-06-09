
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <gtk/gtk.h>
#include <string.h>             /* strchr(), strncmp() */
#include "treeview.h"
#include "treestore.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "command.h"
#include "macros.h"
#include "renderer.h"
#include "columntype-name.h"    /* DONNA_TYPE_COLUMNTYPE_NAME */
#include "cellrenderertext.h"
#include "colorfilter.h"
#include "closures.h"

enum
{
    PROP_0,

    PROP_LOCATION,

    NB_PROPS
};

enum
{
    SIGNAL_SELECT_ARRANGEMENT,
    NB_SIGNALS
};

enum
{
    DONNA_TREE_COL_NODE = 0,
    DONNA_TREE_COL_EXPAND_STATE,
    /* TRUE when expanded, back to FALSE only when manually collapsed, as
     * opposed to GTK default including collapsing a parent. This will allow to
     * preserve expansion when collapsing a parent */
    DONNA_TREE_COL_EXPAND_FLAG,
    DONNA_TREE_COL_ROW_CLASS,
    DONNA_TREE_COL_NAME,
    DONNA_TREE_COL_ICON,
    DONNA_TREE_COL_BOX,
    DONNA_TREE_COL_HIGHLIGHT,
    DONNA_TREE_COL_CLICKS,
    /* which of name, icon, box and/or highlight are locals (else from node).
     * Also includes clicks even though it's not a visual/can't come from node */
    DONNA_TREE_COL_VISUALS,
    DONNA_TREE_NB_COLS
};

enum
{
    DONNA_LIST_COL_NODE = 0,
    DONNA_LIST_NB_COLS
};

/* this column exists in both modes, and must have the same id */
#define DONNA_TREE_VIEW_COL_NODE     0

enum tree_expand
{
    DONNA_TREE_EXPAND_UNKNOWN = 0,  /* not known if node has children */
    DONNA_TREE_EXPAND_NONE,         /* node doesn't have children */
    DONNA_TREE_EXPAND_NEVER,        /* never expanded, children unknown */
    DONNA_TREE_EXPAND_WIP,          /* we have a running task getting children */
    DONNA_TREE_EXPAND_PARTIAL,      /* minitree: only some children are listed */
    DONNA_TREE_EXPAND_MAXI,         /* (was) expanded, children are there */
};

#define ROW_CLASS_MINITREE          "minitree-unknown"
#define ROW_CLASS_PARTIAL           "minitree-partial"

enum
{
    DONNA_TREE_VIEW_MODE_LIST = 0,
    DONNA_TREE_VIEW_MODE_TREE,
};

enum tree_sync
{
    DONNA_TREE_SYNC_NONE = 0,
    DONNA_TREE_SYNC_NODES,
    DONNA_TREE_SYNC_NODES_KNOWN_CHILDREN,
    DONNA_TREE_SYNC_NODES_CHILDREN,
    DONNA_TREE_SYNC_FULL
};

enum
{
    RENDERER_TEXT,
    RENDERER_PIXBUF,
    RENDERER_PROGRESS,
    RENDERER_COMBO,
    RENDERER_TOGGLE,
    RENDERER_SPINNER,
    NB_RENDERERS
};

enum
{
    SORT_CONTAINER_FIRST = 0,
    SORT_CONTAINER_FIRST_ALWAYS,
    SORT_CONTAINER_MIXED
};

enum
{
    DRAW_NOTHING = 0,
    DRAW_WAIT,
    DRAW_EMPTY
};

enum
{
    SELECT_HIGHLIGHT_FULL_ROW = 0,
    SELECT_HIGHLIGHT_COLUMN,
    SELECT_HIGHLIGHT_UNDERLINE,
    SELECT_HIGHLIGHT_COLUMN_UNDERLINE
};

struct visuals
{
    /* iter of the root, or an invalid iter (stamp==0) and user_data if the
     * number of the root, e.g. same as path_to_string */
    GtkTreeIter  root;
    gchar       *name;
    GdkPixbuf   *icon;
    gchar       *box;
    gchar       *highlight;
    /* not a visual, but treated the same */
    gchar       *clicks;
};

struct col_prop
{
    gchar             *prop;
    GtkTreeViewColumn *column;
};

struct as_col
{
    GtkTreeViewColumn *column;
    GPtrArray *tasks;
    guint nb;
};

struct active_spinners
{
    DonnaNode   *node;
    GArray      *as_cols;   /* struct as_col[] */
};

struct provider_signals
{
    DonnaProvider   *provider;
    guint            nb_nodes;
    gulong           sid_node_updated;
    gulong           sid_node_removed;
    gulong           sid_node_children;
    gulong           sid_node_new_child;
};

struct column
{
    /* required when passed as data to handle Ctrl+click on column header */
    DonnaTreeView       *tree;
    gchar               *name;
    GtkTreeViewColumn   *column;
    /* renderers used in columns, indexed as per columntype */
    GPtrArray           *renderers;
    /* label in the header (for title, since we handle it ourself) */
    GtkWidget           *label;
    /* our arrow for secondary sort order */
    GtkWidget           *second_arrow;
    gint                 sort_id;
    DonnaColumnType     *ct;
    gpointer             ct_data;
    /* state of mouse button when click on column header, used to handle a
     * Ctrl+click to (un)set the secondary sort order */
    gboolean             pressed;
    gboolean             ctrl_held;
};

struct _DonnaTreeViewPrivate
{
    DonnaApp            *app;
    gulong               option_set_sid;
    gulong               option_removed_sid;

    /* tree name */
    gchar               *name;

    /* tree store */
    DonnaTreeStore      *store;
    /* list of struct column */
    GSList              *columns;
    /* not in list above
     * list: empty column on the right
     * tree: non-visible column used as select-highlight-column when UNDERLINE */
    GtkTreeViewColumn   *blank_column;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* main column is the one, w/out full_row_select, that can select rows.
     * In mode tree it's also the expander one (in list expander is hidden) */
    GtkTreeViewColumn   *main_column;

    /* main/second sort columns */
    GtkTreeViewColumn   *sort_column;
    GtkTreeViewColumn   *second_sort_column;
    /* since it's not part of GtkTreeSortable */
    GtkSortType          second_sort_order;

    /* current arrangement */
    DonnaArrangement    *arrangement;

    /* properties used by our columns */
    GArray              *col_props;

    /* handling of spinners on columns (when setting node properties) */
    GPtrArray           *active_spinners;
    guint                active_spinners_id;
    guint                active_spinners_pulse;

    /* current location */
    DonnaNode           *location;
    /* Tree: iter of current location */
    GtkTreeIter          location_iter;
    /* Tree: iter of futur location, used to ensure it is visible */
    GtkTreeIter          future_location_iter;

    /* List: future location (task get_children running) */
    DonnaNode           *future_location;

    /* tree: list of iters for roots, in order */
    GSList              *roots;
    /* hashtable of nodes & their iters on TV */
    GHashTable          *hashtable;

    /* list of iters to be used by callbacks. Because we use iters in cb's data,
     * we need to ensure they stay valid. We only use iters from the store, and
     * they are persistent. However, the row could be removed, thus the iter
     * wouldn't be valid anymore.
     * To handle this, whenver an iter is used in a cb's data, a pointer is
     * added in this list. When a row is removed, any iter pointing to that row
     * is removed, that way in the cb we can check if the iter is still there or
     * not. If not, it means it's invalid/the row was removed. */
    GSList              *watched_iters;

    /* providers we're connected to */
    GPtrArray           *providers;

    /* list of props on nodes being refreshed (see refresh_node_prop_cb) */
    GMutex               refresh_node_props_mutex;
    GSList              *refresh_node_props;

    /* Tree: list we're synching with */
    DonnaTreeView       *sync_with;
    gulong               sid_sw_location_changed;
    gulong               sid_active_list_changed;

    /* info about last event, used to handle single, double & slow-dbl clicks */
    GdkEventButton      *last_event;
    guint                last_event_timeout; /* it was a single-click */
    gboolean             last_event_expired; /* after sgl-clk, could get a slow-dbl */
    /* when a renderer goes edit-mode, we need the editing-started signal to get
     * the editable */
    guint                renderer_editing_started_sid;
    /* editable is kept so we can make it abort editing when the user clicks
     * away (e.g. blank space, another row, etc) */
    GtkCellEditable     *renderer_editable;
    /* this one is needed to clear/disconnect when editing is done */
    guint                renderer_editable_remove_widget_sid;

    /* Tree: keys are ful locations, values are GSList of struct visuals. The
     * idea is that the list of loaded when loading for a tree file, so we can
     * load visuals only when adding the nodes (e.g. on expanding).
     * In minitree, we also put them back in there when nodes are removed. */
    GHashTable          *tree_visuals;
    /* Tree: which visuals to load from node */
    DonnaTreeVisual      node_visuals;

    /* "cached" options */
    guint                mode               : 1;
    guint                node_types         : 2;
    guint                show_hidden        : 1;
    guint                sort_groups        : 2; /* containers (always) first/mixed */
    guint                full_row_select    : 1;
    guint                select_highlight   : 2;
    /* mode Tree */
    guint                is_minitree        : 1;
    guint                sync_mode          : 3;
    guint                sync_scroll        : 1; /* only used if GTK_IS_JJK */
    guint                auto_focus_sync    : 1;
    /* mode List */
    guint                draw_state         : 2;
    guint                focusing_click     : 1;
    /* from current arrangement */
    guint                second_sort_sticky : 1;
};

static GParamSpec * donna_tree_view_props[NB_PROPS] = { NULL, };
static guint        donna_tree_view_signals[NB_SIGNALS] = { 0, };

/* our internal renderers */
enum
{
    INTERNAL_RENDERER_SPINNER = 0,
    INTERNAL_RENDERER_PIXBUF,
    NB_INTERNAL_RENDERERS
};
static GtkCellRenderer *int_renderers[NB_INTERNAL_RENDERERS] = { NULL, };

/* iters only uses stamp & user_data */
#define itereq(i1, i2)      \
    ((i1)->stamp == (i2)->stamp && (i1)->user_data == (i2)->user_data)

#define watch_iter(tree, iter)  \
    tree->priv->watched_iters = g_slist_prepend (tree->priv->watched_iters, iter)
#define remove_watch_iter(tree, iter)   \
    tree->priv->watched_iters = g_slist_remove (tree->priv->watched_iters, iter)

#define is_tree(tree)       ((tree)->priv->mode == DONNA_TREE_VIEW_MODE_TREE)

#define set_es(priv, iter, es)                          \
    donna_tree_store_set ((priv)->store, iter,          \
            DONNA_TREE_COL_EXPAND_STATE,    es,         \
            DONNA_TREE_COL_ROW_CLASS,                   \
            (((es) == DONNA_TREE_EXPAND_PARTIAL)        \
             ? ROW_CLASS_PARTIAL                        \
             : ((es) == DONNA_TREE_EXPAND_NONE          \
                 || (es) == DONNA_TREE_EXPAND_MAXI)     \
             ? NULL : ROW_CLASS_MINITREE),              \
            -1)

/* internal from provider-config.c */
gchar *_donna_config_get_string_tree_column (DonnaConfig   *config,
                                             const gchar   *tv_name,
                                             const gchar   *col_name,
                                             gboolean       is_clicks,
                                             const gchar   *arr_name,
                                             const gchar   *def_cat,
                                             const gchar   *opt_name,
                                             gchar         *def_val);


static inline struct column *
                    get_column_by_column                (DonnaTreeView *tree,
                                                         GtkTreeViewColumn *column);
static inline struct column *
                    get_column_by_name                  (DonnaTreeView *tree,
                                                         const gchar *name);
static inline void load_node_visuals                    (DonnaTreeView *tree,
                                                         GtkTreeIter   *iter,
                                                         DonnaNode     *node,
                                                         gboolean       allow_refresh);
static gboolean add_node_to_tree                        (DonnaTreeView *tree,
                                                         GtkTreeIter   *parent,
                                                         DonnaNode     *node,
                                                         GtkTreeIter   *row);
static GtkTreeIter *get_current_root_iter               (DonnaTreeView *tree);
static GtkTreeIter *get_closest_iter_for_node           (DonnaTreeView *tree,
                                                         DonnaNode     *node,
                                                         DonnaProvider *provider,
                                                         const gchar   *location,
                                                         GtkTreeIter   *skip_root,
                                                         gboolean      *is_match);
static GtkTreeIter *get_best_existing_iter_for_node     (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        even_collapsed);
static GtkTreeIter *get_iter_expanding_if_needed        (DonnaTreeView *tree,
                                                         GtkTreeIter   *iter_root,
                                                         DonnaNode     *node,
                                                         gboolean       only_accessible,
                                                         gboolean      *was_match);
static GtkTreeIter *get_best_iter_for_node              (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        add_root_if_needed,
                                                         GError        **error);
static GtkTreeIter * get_root_iter                      (DonnaTreeView   *tree,
                                                         GtkTreeIter     *iter);
static struct active_spinners * get_as_for_node         (DonnaTreeView   *tree,
                                                         DonnaNode       *node,
                                                         guint           *index,
                                                         gboolean         create);
static inline void scroll_to_iter                       (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean scroll_to_current                       (DonnaTreeView  *tree);
static void check_children_post_expand                  (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean select_arrangement_accumulator      (GSignalInvocationHint  *hint,
                                                     GValue                 *return_accu,
                                                     const GValue           *return_handler,
                                                     gpointer                data);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);
static void free_active_spinners (struct active_spinners *as);

static gboolean donna_tree_view_button_press_event  (GtkWidget      *widget,
                                                     GdkEventButton *event);
static gboolean donna_tree_view_button_release_event(GtkWidget      *widget,
                                                     GdkEventButton *event);
static void     donna_tree_view_row_activated       (GtkTreeView    *treev,
                                                     GtkTreePath    *path,
                                                     GtkTreeViewColumn *column);
static gboolean donna_tree_view_test_expand_row     (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_collapsed       (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_expanded        (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_get_property        (GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
static gboolean donna_tree_view_draw                (GtkWidget      *widget,
                                                     cairo_t        *cr);
static void     donna_tree_view_finalize            (GObject        *object);

G_DEFINE_TYPE (DonnaTreeView, donna_tree_view, GTK_TYPE_TREE_VIEW);

static void
donna_tree_view_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;
    GtkWidgetClass *w_class;
    GObjectClass *o_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
    tv_class->row_activated = donna_tree_view_row_activated;
    tv_class->row_expanded  = donna_tree_view_row_expanded;
    tv_class->row_collapsed = donna_tree_view_row_collapsed;
    tv_class->test_expand_row = donna_tree_view_test_expand_row;

    w_class = GTK_WIDGET_CLASS (klass);
    w_class->draw = donna_tree_view_draw;
    w_class->button_press_event = donna_tree_view_button_press_event;
    w_class->button_release_event = donna_tree_view_button_release_event;

    o_class = G_OBJECT_CLASS (klass);
    o_class->get_property   = donna_tree_view_get_property;
    o_class->finalize       = donna_tree_view_finalize;

    donna_tree_view_props[PROP_LOCATION] =
        g_param_spec_object ("location", "location",
                "Current location of the treeview",
                DONNA_TYPE_NODE,
                G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_tree_view_props);

    donna_tree_view_signals[SIGNAL_SELECT_ARRANGEMENT] =
        g_signal_new ("select-arrangement",
                DONNA_TYPE_TREE_VIEW,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaTreeViewClass, select_arrangement),
                select_arrangement_accumulator,
                NULL,
                g_cclosure_user_marshal_POINTER__STRING_OBJECT,
                G_TYPE_POINTER,
                2,
                G_TYPE_STRING,
                DONNA_TYPE_NODE);

    gtk_widget_class_install_style_property (w_class,
            g_param_spec_int ("highlighted-size", "Highlighted size",
                "Size of extra highlighted bit on the right",
                0,  /* minimum */
                8,  /* maximum */
                3,  /* default */
                G_PARAM_READABLE));

    g_type_class_add_private (klass, sizeof (DonnaTreeViewPrivate));

    if (!int_renderers[INTERNAL_RENDERER_SPINNER])
        int_renderers[INTERNAL_RENDERER_SPINNER] =
            gtk_cell_renderer_spinner_new ();
    if (!int_renderers[INTERNAL_RENDERER_PIXBUF])
        int_renderers[INTERNAL_RENDERER_PIXBUF] =
            gtk_cell_renderer_pixbuf_new ();
}

static void
donna_tree_view_init (DonnaTreeView *tv)
{
    DonnaTreeViewPrivate *priv;

    priv = tv->priv = G_TYPE_INSTANCE_GET_PRIVATE (tv, DONNA_TYPE_TREE_VIEW,
            DonnaTreeViewPrivate);
    /* we can't use g_hash_table_new_full and set a destroy, because we will
     * be replacing values often (since head of GSList can change) but don't
     * want the old value to be free-d, obviously */
    priv->hashtable = g_hash_table_new (g_direct_hash, g_direct_equal);

    priv->providers = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_provider_signals);
    g_mutex_init (&priv->refresh_node_props_mutex);
    priv->col_props = g_array_new (FALSE, FALSE, sizeof (struct col_prop));
    g_array_set_clear_func (priv->col_props, (GDestroyNotify) free_col_prop);
    priv->active_spinners = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_active_spinners);
}

static void
free_col_prop (struct col_prop *cp)
{
    g_free (cp->prop);
}

static void
free_provider_signals (struct provider_signals *ps)
{
    if (ps->sid_node_updated)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_updated);
    if (ps->sid_node_removed)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_removed);
    if (ps->sid_node_children)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_children);
    if (ps->sid_node_new_child)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_new_child);
    g_object_unref (ps->provider);
    g_free (ps);
}

static void
free_active_spinners (struct active_spinners *as)
{
    g_object_unref (as->node);
    g_array_free (as->as_cols, TRUE);
    g_free (as);
}

static void
free_hashtable (gpointer key, GSList *list, gpointer data)
{
    g_slist_free_full (list, (GDestroyNotify) gtk_tree_iter_free);
}

static void
free_visuals (struct visuals *visuals)
{
    g_free (visuals->name);
    if (visuals->icon)
        g_object_unref (visuals->icon);
    g_free (visuals->box);
    g_free (visuals->highlight);
    g_free (visuals->clicks);
    g_slice_free (struct visuals, visuals);
}

static void
donna_tree_view_get_property (GObject        *object,
                              guint           prop_id,
                              GValue         *value,
                              GParamSpec     *pspec)
{
    DonnaTreeViewPrivate *priv = DONNA_TREE_VIEW (object)->priv;

    if (prop_id == PROP_LOCATION)
        g_value_set_object (value, priv->location);
}

static void
free_column (struct column *_col)
{
    g_free (_col->name);
    g_ptr_array_unref (_col->renderers);
    donna_columntype_free_data (_col->ct, _col->ct_data);
    g_object_unref (_col->ct);
    g_slice_free (struct column, _col);
}

/* for use from finalize only */
static gboolean
free_tree_visuals (gpointer key, GSList *l)
{
    /* this frees the data */
    g_slist_free_full (l, (GDestroyNotify) free_visuals);
    /* this will take care of free-ing the key */
    return TRUE;
}

static void
donna_tree_view_finalize (GObject *object)
{
    DonnaTreeViewPrivate *priv;

    priv = DONNA_TREE_VIEW (object)->priv;
    g_hash_table_foreach (priv->hashtable, (GHFunc) free_hashtable, NULL);
    g_hash_table_destroy (priv->hashtable);
    g_ptr_array_free (priv->providers, TRUE);
    g_mutex_clear (&priv->refresh_node_props_mutex);
    g_array_free (priv->col_props, TRUE);
    g_ptr_array_free (priv->active_spinners, TRUE);
    g_slist_free_full (priv->columns, (GDestroyNotify) free_column);
    if (priv->tree_visuals)
        g_hash_table_foreach_remove (priv->tree_visuals,
                (GHRFunc) free_tree_visuals, NULL);

    G_OBJECT_CLASS (donna_tree_view_parent_class)->finalize (object);
}

struct scroll_data
{
    DonnaTreeView   *tree;
    GtkTreeIter     *iter;
};

static gboolean
idle_scroll_to_iter (struct scroll_data *data)
{
    scroll_to_iter (data->tree, data->iter);
    g_slice_free (struct scroll_data, data);
    return FALSE;
}

static void
sync_with_location_changed_cb (GObject       *object,
                               GParamSpec    *pspec,
                               DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev;
    GtkTreeSelection *sel;
    GtkTreeIter *iter = NULL;
    DonnaNode *node;

    g_object_get (object, "location", &node, NULL);
    if (node == priv->location)
    {
        g_object_unref (node);
        return;
    }

    switch (priv->sync_mode)
    {
        case DONNA_TREE_SYNC_NODES:
            iter = get_best_existing_iter_for_node (tree, node, FALSE);
            break;

        case DONNA_TREE_SYNC_NODES_KNOWN_CHILDREN:
            iter = get_best_existing_iter_for_node (tree, node, TRUE);
            break;

        case DONNA_TREE_SYNC_NODES_CHILDREN:
            iter = get_best_iter_for_node (tree, node, FALSE, NULL);
            break;

        case DONNA_TREE_SYNC_FULL:
            iter = get_best_iter_for_node (tree, node, TRUE, NULL);
            break;
    }

    treev = (GtkTreeView *) tree;
    sel = gtk_tree_view_get_selection (treev);
    if (iter)
    {
        GtkTreePath *path;

        if (priv->future_location_iter.stamp == 0)
        {
            gboolean was_visible;

            /* future_location_iter wasn't set, so no rows were added to the
             * tree. But, we still might have found an already existing one that
             * wasn't visible, so let's make sure it is visible (or else we
             * wouldn't be able to get a path, etc) */
            priv->future_location_iter = *iter;
            donna_tree_store_refresh_visibility (priv->store, iter, &was_visible);
            if (!was_visible)
                /* we just "added" a row to the tree, since it wasn't visible.
                 * But since it wasn't, if it has a "fake node" to provide an
                 * expander, that node (still) isn't visible, so we should make
                 * it.
                 * Technically, it could very well have a bunch of children,
                 * that should also be made visible then, e.g. if the row was
                 * expanded, and then made non-visible. */
                donna_tree_store_refilter (priv->store, iter);
        }

        gtk_tree_selection_set_mode (sel, GTK_SELECTION_BROWSE);
        /* we select the new row and put the cursor on it (required to get
         * things working when collapsing the parent) */
        path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, iter);
        if (priv->sync_mode == DONNA_TREE_SYNC_NODES_KNOWN_CHILDREN)
        {
            GtkTreePath *p;
            gint i, depth, *indices;

            /* we're doing here the same as gtk_tree_view_expand_to_path() only
             * without expanding the row at path itself (only parents to it) */

            indices = gtk_tree_path_get_indices_with_depth (path, &depth);
            --depth;
            p = gtk_tree_path_new ();
            for (i = 0; i < depth; ++i)
            {
                gtk_tree_path_append_index (p, indices[i]);
                gtk_tree_view_expand_row (treev, p, FALSE);
            }
            gtk_tree_path_free (p);
        }

#ifdef GTK_IS_JJK
        /* this beauty will put focus & select the row, without doing any
         * scrolling whatsoever. What a wonderful thing! :) */
        gtk_tree_view_set_focused_row (treev, path);
        gtk_tree_selection_select_path (sel, path);
#else
        /* Note: set_cursor() will check and do some minimum scrolling if iter
         * isn't visible. We don't want that, but do our own scrolling (w/
         * different align values), yet we don't care and just call
         * scroll_to_iter afterwards.
         *
         * This works, because whatever set_cursor did (scrolling wise) isn't
         * yet reflected on visible_rect (some triggers not having been
         * processed yet).
         * Therefore, we will still be basing our calculations of visibility on
         * the state of things before set_cursor, and will be triggered if it
         * was, thus overwriting what it did.
         *
         * This is why we shall simply call our scroll functions after. One
         * thing not to do is try and use gtk_main_iteration do get drawing
         * events to be processed, as that could get task's callbacks to be
         * processed, thus adding children and doing all sorts of (scrolling)
         * stuff we don't want (esp. if they rely on us having changed the
         * current location, which we might not have done yet here).
         */

        /* Well: turns out this isn't good, and we need correct values because
         * otherwise we can get the impression that a row is visible even though
         * it's totally not, which would lead to no scrolling done when it
         * should have. Grr.... */

        /* Note: this is actually not working quite perfecly, for reasons
         * unknown. Apparently when rows were expanded below the visible area,
         * set_cursor will trigger some (invalid) scrolling due to the fact that
         * all presize/validate_rows trigger haven't yet been processed I would
         * assume, or something.
         * In the end, the row gets into view (hopefully), but because of it we
         * don't do our own scrolling (already in view) and so the alignment
         * isn't as expected.
         * Kinda sucks, get the jjk-patch to fix it :) */
        gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
#endif
        gtk_tree_path_free (path);

        if (priv->sync_scroll)
        {
            struct scroll_data *data;
            data = g_slice_new (struct scroll_data);
            data->tree = tree;
            data->iter = iter;

            /* the reason we use a timeout here w/ a magic number, is that
             * expanding rows had GTK install some triggers
             * (presize/validate_rows) that are required to be processed for
             * things to work, i.e. if we try to call get_background_area now
             * (which scroll_to_iter does to calculate visibility) we get BS
             * values.  I couldn't find a proper way around it, idle w/ low
             * priority doesn't do it, only a timeout seems to work. About 15
             * should be enough to do the trick, so we're hoping that 42 will
             * always work */
            g_timeout_add (42, (GSourceFunc) idle_scroll_to_iter, data);
        }
    }
    else
    {
        /* unselect, but allow a new selection to be made (will then switch
         * automatically back to SELECTION_BROWSE) */
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
        gtk_tree_selection_unselect_all (sel);

        if (priv->sync_mode == DONNA_TREE_SYNC_NODES
                || priv->sync_mode == DONNA_TREE_SYNC_NODES_KNOWN_CHILDREN)
        {
            gchar *location;

            location = donna_node_get_location (node);
            iter = get_closest_iter_for_node (tree, node,
                    donna_node_peek_provider (node), location,
                    NULL, NULL);
            g_free (location);
            if (iter)
            {
                GtkTreePath *path;

                /* we don't want to select anything here, just put focus on the
                 * closest accessible parent we just found, also put that iter
                 * into view */

                path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->store), iter);
#ifdef GTK_IS_JJK
                gtk_tree_view_set_focused_row (treev, path);
#else
                gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
                gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
                gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
#endif
                gtk_tree_path_free (path);

                if (priv->sync_scroll)
                    scroll_to_iter (tree, iter);
            }
        }
    }

    priv->future_location_iter.stamp = 0;
    g_object_unref (node);
}

static void
active_list_changed_cb (GObject         *object,
                        GParamSpec      *pspec,
                        DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    GError *err = NULL;

    if (priv->sync_with)
    {
        g_signal_handler_disconnect (priv->sync_with, priv->sid_sw_location_changed);
        g_object_unref (priv->sync_with);
    }
    g_object_get (object, "active-list", &priv->sync_with, NULL);
    priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
            "notify::location",
            G_CALLBACK (sync_with_location_changed_cb), tree);

    g_object_get (priv->sync_with, "location", &node, NULL);
    if (!node)
        return;

    if (!donna_tree_view_set_location (tree, node, &err))
    {
        gchar *location = donna_node_get_location (node);
        donna_app_show_error (priv->app, err, "Treeview '%s': Unable to change location to '%s:%s'",
                priv->name,
                donna_node_get_domain (node),
                location);
        g_free (location);
    }
    g_object_unref (node);
}

struct option_data
{
    DonnaTreeView *tree;
    gchar *option;
};

static gboolean
real_option_cb (struct option_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean is_column = FALSE;
    gchar buf[255], *b = buf;
    gchar *opt;
    GSList *l;
    gsize len;

    len = snprintf (buf, 255, "/treeviews/%s/", priv->name);
    if (len >= 255)
        b = g_strdup_printf ("/treeviews/%s/", priv->name);

    if (streqn (data->option, b, len))
    {
        if (streqn (data->option + len, "columns/", 8))
        {
            opt = data->option + len + 8;
            is_column = TRUE;
        }
        else
        {
            /* treeview option */
        }
    }
    else
    {
        /* columns option */
        opt = data->option + 9; /* 9 == strlen ("/columns/") */
        is_column = TRUE;
    }

    if (is_column)
    {
        gchar *s;

        s = strchr (opt, '/');
        if (s)
        {
            struct column *_col;

            /* is this change about a column we are using right now? */
            *s = '\0';
            _col = get_column_by_name (data->tree, opt);
            *s = '/';
            if (_col)
            {
                if (streq (s + 1, "title"))
                {
                    gchar *s = NULL;

                    /* we know we will get a value, but it might not be from the
                     * config changed that occured, since the value might be
                     * overridden by current arrangement, etc */
                    s = donna_config_get_string_column (
                            donna_app_peek_config (priv->app),
                            priv->name, _col->name,
                            priv->arrangement->columns_options,
                            NULL, "title", s);
                    gtk_tree_view_column_set_title (_col->column, s);
                    gtk_label_set_text ((GtkLabel *) _col->label, s);
                    g_free (s);
                }
                else if (streq (s + 1, "width"))
                {
                    gint w = 0;

                    /* we know we will get a value, but it might not be from the
                     * config changed that occured, since the value might be
                     * overridden by current arrangement, etc */
                    w = donna_config_get_int_column (
                            donna_app_peek_config (priv->app),
                            priv->name, _col->name,
                            priv->arrangement->columns_options,
                            NULL, "width", w);
                    gtk_tree_view_column_set_fixed_width (_col->column, w);
                }
                else
                {
                    DonnaColumnTypeNeed need;

                    /* ask the ct if something needs to happen */
                    need = donna_columntype_refresh_data (_col->ct,
                            priv->name, _col->name,
                            priv->arrangement->columns_options, &_col->ct_data);
                    if (need & DONNA_COLUMNTYPE_NEED_RESORT)
                    {
                        GtkTreeSortable *sortable;
                        gint cur_sort_id;
                        GtkSortType cur_sort_order;

                        /* trigger a resort */

                        sortable = (GtkTreeSortable *) priv->store;
                        gtk_tree_sortable_get_sort_column_id (sortable,
                                &cur_sort_id, &cur_sort_order);
                        gtk_tree_sortable_set_sort_column_id (sortable,
                                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                cur_sort_order);
                        gtk_tree_sortable_set_sort_column_id (sortable,
                                cur_sort_id, cur_sort_order);
                    }
                    if (need & DONNA_COLUMNTYPE_NEED_REDRAW)
                        gtk_widget_queue_draw ((GtkWidget *) data->tree);
                }
            }
        }
    }

    g_free (data);
    return FALSE;
}

static void
option_cb (DonnaConfig *config, const gchar *option, DonnaTreeView *tree)
{
    gchar buf[255], *b = buf;
    gsize len;
    gboolean match;

    /* options we care about are ones for the tree (in "/treeviews/<NAME>") or
     * for one of our columns:
     * /treeviews/<NAME>/columns/<NAME>
     * /columns/<NAME>
     * This excludes options in the current arrangement, but that's
     * okay/expected: arrangement are loaded/"created" on location change.
     *
     * Here we can only check if the option starts with "/treeviews/<NAME>" or
     * "/columns/" and that's it, to loop through our columns we need the GTK
     * lock, i.e. go in main thread */

    len = snprintf (buf, 255, "/treeviews/%s/", tree->priv->name);
    if (len >= 255)
        b = g_strdup_printf ("/treeviews/%s/", tree->priv->name);

    match = streqn (option, b, len) || streqn (option, "/columns/", 9);
    if (b != buf)
        g_free (b);

    if (match)
    {
        struct option_data *data;

        data = g_new (struct option_data, 1);
        data->tree = tree;
        data->option = g_strdup (option);
        g_main_context_invoke (NULL, (GSourceFunc) real_option_cb, data);
    }
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    gint val;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,removed} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;
    config = donna_app_peek_config (priv->app);

    if (donna_config_get_int (config, &val,
                "treeviews/%s/mode", priv->name))
        priv->mode = CLAMP (val, 0, 1);
    else
    {
        g_warning ("Treeview '%s': Unable to find mode, defaulting to list",
                priv->name);
        /* set default */
        val = priv->mode = DONNA_TREE_VIEW_MODE_LIST;
        donna_config_set_int (config, val,
                "treeviews/%s/mode", priv->mode);
    }

    if (donna_config_get_boolean (config, (gboolean *) &val,
                "treeviews/%s/show_hidden", priv->name))
        priv->show_hidden = (gboolean) val;
    else
    {
        /* set default */
        val = priv->show_hidden = TRUE;
        donna_config_set_boolean (config, (gboolean) val,
                "treeviews/%s/show_hidden", priv->name);
    }

    if (donna_config_get_int (config, &val,
                "treeviews/%s/node_types", priv->name))
        priv->node_types = CLAMP (val, 0, 3);
    else
    {
        /* set default */
        val = DONNA_NODE_CONTAINER;
        if (!is_tree (tree))
            val |= DONNA_NODE_ITEM;
        priv->node_types = val;
        donna_config_set_int (config, val,
                "treeviews/%s/node_types", priv->name);
    }

    if (donna_config_get_int (config, &val,
                "treeviews/%s/sort_groups", priv->name))
        priv->sort_groups = CLAMP (val, 0, 2);
    else
    {
        /* set default */
        val = SORT_CONTAINER_FIRST;
        priv->sort_groups = val;
        donna_config_set_int (config, val,
                "treeviews/%s/sort_groups", priv->name);
    }

    if (donna_config_get_boolean (config, (gboolean *) &val,
                "treeviews/%s/full_row_select", priv->name))
        priv->full_row_select = (gboolean) val;
    else
    {
        /* set default */
        val = priv->full_row_select = FALSE;
        donna_config_set_boolean (config, (gboolean) val,
                "treeviews/%s/full_row_select", priv->name);
    }

    if (donna_config_get_int (config, &val,
                "treeviews/%s/select_highlight", priv->name))
        priv->select_highlight = CLAMP (val, 0, 3);
    else
    {
        /* set default */
        val = (is_tree (tree)) ? SELECT_HIGHLIGHT_COLUMN : SELECT_HIGHLIGHT_COLUMN_UNDERLINE;
        priv->select_highlight = val;
        donna_config_set_int (config, val,
                "treeviews/%s/select_highlight", priv->name);
    }

    if (is_tree (tree))
    {
        gchar *s;

        if (donna_config_get_int (config, &val,
                    "treeviews/%s/node_visuals", priv->name))
            priv->node_visuals = val;

        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/is_minitree", priv->name))
            priv->is_minitree = (gboolean) val;
        else
        {
            /* set default */
            val = priv->is_minitree = FALSE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeviews/%s/is_minitree", priv->name);
        }

        if (donna_config_get_int (config, &val,
                    "treeviews/%s/sync_mode", priv->name))
            priv->sync_mode = CLAMP (val, 0, 4);
        else
        {
            /* set default */
            val = priv->sync_mode = DONNA_TREE_SYNC_FULL;
            donna_config_set_int (config, val,
                    "treeviews/%s/sync_mode", priv->name);
        }

        if (donna_config_get_string (config, &s,
                    "treeviews/%s/sync_with", priv->name))
            priv->sync_with = donna_app_get_treeview (priv->app, s);
        else
        {
            /* active list */
            g_object_get (priv->app, "active-list", &priv->sync_with, NULL);
            priv->sid_active_list_changed = g_signal_connect (priv->app,
                    "notify::active-list",
                    G_CALLBACK (active_list_changed_cb), tree);
        }

        if (priv->sync_with)
            priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                    "notify::location",
                    G_CALLBACK (sync_with_location_changed_cb), tree);

#ifdef GTK_IS_JJK
        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/sync_scroll", priv->name))
            priv->sync_scroll = (gboolean) val;
        else
        {
            /* set default */
            val = priv->sync_scroll = TRUE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeviews/%s/sync_scroll", priv->name);
        }
#else
        priv->sync_scroll = TRUE;
#endif

        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/auto_focus_sync", priv->name))
            priv->auto_focus_sync = (gboolean) val;
        else
        {
            /* set default */
            val = priv->auto_focus_sync = FALSE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeviews/%s/auto_focus_sync", priv->name);
        }
    }
    else
    {
        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/focusing_click", priv->name))
            priv->focusing_click = (gboolean) val;
        else
        {
            /* set default */
            val = priv->focusing_click = TRUE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeviews/%s/focusing_click", priv->name);
        }
    }

    /* listen to config changes */
    if (priv->option_set_sid == 0)
        priv->option_set_sid = g_signal_connect (config, "option-set",
                (GCallback) option_cb, tree);
    if (priv->option_removed_sid == 0)
        priv->option_removed_sid = g_signal_connect (config, "option-removed",
                (GCallback) option_cb, tree);
}

static gboolean
is_watched_iter_valid (DonnaTreeView *tree, GtkTreeIter *iter, gboolean remove)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *l, *prev = NULL;

    l = priv->watched_iters;
    while (l)
    {
        if (l->data == iter)
        {
            if (remove)
            {
                if (prev)
                    prev->next = l->next;
                else
                    priv->watched_iters = l->next;

                g_slist_free_1 (l);
            }
            return TRUE;
        }
        else
        {
            prev = l;
            l = prev->next;
        }
    }
    return FALSE;
}

static inline struct column *
get_column_by_column (DonnaTreeView *tree, GtkTreeViewColumn *column)
{
    GSList *l;

    for (l = tree->priv->columns; l; l = l->next)
        if (((struct column *) l->data)->column == column)
            return l->data;
    return NULL;
}

static inline struct column *
get_column_by_name (DonnaTreeView *tree, const gchar *name)
{
    GSList *l;

    for (l = tree->priv->columns; l; l = l->next)
        if (streq (((struct column *) l->data)->name, name))
            return l->data;
    return NULL;
}

static void
show_err_on_task_failed (DonnaTask      *task,
                         gboolean        timeout_called,
                         DonnaTreeView  *tree)
{
    if (donna_task_get_state (task) != DONNA_TASK_FAILED)
        return;

    donna_app_show_error (tree->priv->app, donna_task_get_error (task),
            "Treeview '%s': Failed to trigger node", tree->priv->name);
}

typedef void (*node_children_extra_cb) (DonnaTreeView *tree, GtkTreeIter *iter);

struct node_children_data
{
    DonnaTreeView           *tree;
    GtkTreeIter              iter;
    gboolean                 scroll_to_current;
    node_children_extra_cb   extra_callback;
};

static void
free_node_children_data (struct node_children_data *data)
{
    remove_watch_iter (data->tree, &data->iter);
    g_slice_free (struct node_children_data, data);
}

/* mode tree only */
static void
node_get_children_tree_timeout (DonnaTask                   *task,
                                struct node_children_data   *data)
{
    GtkTreePath *path;

    /* we're slow to get the children, let's just show the fake node ("please
     * wait...") */
    if (!is_watched_iter_valid (data->tree, &data->iter, FALSE))
        return;
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
            &data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);
}

struct ctv_data
{
    DonnaTreeView *tree;
    GtkTreeIter   *iter;
};

static gboolean
clean_tree_visuals (gchar *fl, GSList *list, struct ctv_data *data)
{
    GSList *l, *next;

    for (l = list; l; l = next)
    {
        struct visuals *visuals = l->data;
        next = l->next;

        if (itereq (data->iter, &visuals->root))
        {
            free_visuals (visuals);
            list = g_slist_delete_link (list, l);
        }
    }

    return list == NULL;
}

/* similar to gtk_tree_store_remove() this will set iter to next row at that
 * level, or invalid it if it pointer to the last one.
 * Returns TRUE if iter is still valid, else FALSE */
/* Note: the reason we don't put this as handler for the store's row-deleted
 * signal is that that signal happens *after* the row has been deleted, and
 * therefore there are no iter. But we *need* an iter here, to take care of our
 * hashlist of, well, iters. This is also why we also have special handling of
 * removing an iter w/ children. */
static gboolean
remove_row_from_tree (DonnaTreeView *tree,
                      GtkTreeIter   *iter,
                      gboolean       is_removal)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    DonnaNode *node;
    DonnaProvider *provider;
    guint i;
    GSList *l, *prev = NULL;
    GtkTreeIter parent;
    gboolean ret;

    /* get the node */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);
    if (node)
    {
        GSList *list;

        /* get its provider */
        provider = donna_node_peek_provider (node);
        /* and update the nb of nodes we have for this provider */
        for (i = 0; i < priv->providers->len; ++i)
        {
            struct provider_signals *ps = priv->providers->pdata[i];

            if (ps->provider == provider)
            {
                if (--ps->nb_nodes == 0)
                {
                    /* this will disconnect handlers from provider & free memory */
                    g_ptr_array_remove_index_fast (priv->providers, i);
                    break;
                }
            }
        }

        /* remove iter for that row in hashtable */
        l = list = g_hash_table_lookup (priv->hashtable, node);
        while (l)
        {
            if (itereq (iter, (GtkTreeIter *) l->data))
            {
                if (prev)
                    prev->next = l->next;
                else
                    list = l->next;

                gtk_tree_iter_free (l->data);
                g_slist_free_1 (l);
                break;
            }
            else
            {
                prev = l;
                l = l->next;
            }
        }
        if (list)
            g_hash_table_insert (priv->hashtable, node, list);
        else
            g_hash_table_remove (priv->hashtable, node);

        if (is_tree (tree))
        {
            /* removing a root? */
            if (donna_tree_store_iter_depth (priv->store, iter) == 0)
            {
                GSList *l;

                for (l = priv->roots; l; l = l->next)
                    if (itereq (iter, (GtkTreeIter *) l->data))
                    {
                        priv->roots = g_slist_delete_link (priv->roots, l);
                        break;
                    }

                /* also means we need to clean tree_visuals for anything that
                 * was under that root. Removing a root means forgetting any and
                 * all tree visuals under there. */

                if (priv->tree_visuals)
                {
                    struct ctv_data data = { .tree = tree, .iter = iter };

                    g_hash_table_foreach_remove (priv->tree_visuals,
                            (GHRFunc) clean_tree_visuals, &data);
                    if (g_hash_table_size (priv->tree_visuals) == 0)
                    {
                        g_hash_table_unref (priv->tree_visuals);
                        priv->tree_visuals = NULL;
                    }
                }
            }
            else if (!is_removal)
            {
                DonnaTreeVisual v;

                /* place any tree_visuals back there to remember them when the
                 * node comes back */

                gtk_tree_model_get (model, iter,
                        DONNA_TREE_COL_VISUALS,     &v,
                        -1);
                if (v > 0)
                {
                    struct visuals *visuals;
                    GtkTreeIter *root;
                    gchar *fl;
                    GSList *l = NULL;

                    visuals = g_slice_new0 (struct visuals);
                    root = get_root_iter (tree, iter);
                    visuals->root = *root;

                    /* we can't just get everything, since there might be
                     * node_visuals applied */
                    if (v & DONNA_TREE_VISUAL_NAME)
                        gtk_tree_model_get (model, iter,
                                DONNA_TREE_COL_NAME,        &visuals->name,
                                -1);
                    if (v & DONNA_TREE_VISUAL_ICON)
                        gtk_tree_model_get (model, iter,
                                DONNA_TREE_COL_ICON,        &visuals->icon,
                                -1);
                    if (v & DONNA_TREE_VISUAL_BOX)
                        gtk_tree_model_get (model, iter,
                                DONNA_TREE_COL_BOX,         &visuals->box,
                                -1);
                    if (v & DONNA_TREE_VISUAL_HIGHLIGHT)
                        gtk_tree_model_get (model, iter,
                                DONNA_TREE_COL_HIGHLIGHT,   &visuals->highlight,
                                -1);
                    /* not a visual, but treated the same */
                    if (v & DONNA_TREE_VISUAL_CLICKS)
                        gtk_tree_model_get (model, iter,
                                DONNA_TREE_COL_CLICKS,      &visuals->clicks,
                                -1);

                    fl = donna_node_get_full_location (node);

                    if (priv->tree_visuals)
                        l = g_hash_table_lookup (priv->tree_visuals, fl);
                    else
                        priv->tree_visuals = g_hash_table_new_full (
                                g_str_hash, g_str_equal,
                                g_free, NULL);

                    l = g_slist_prepend (l, visuals);
                    g_hash_table_insert (priv->tree_visuals, fl, l);
                }
            }
        }

        g_object_unref (node);
    }

    /* remove all watched_iters to this row */
    l = priv->watched_iters;
    while (l)
    {
        if (itereq (iter, (GtkTreeIter *) l->data))
        {
            GSList *next = l->next;

            if (prev)
                prev->next = next;
            else
                priv->watched_iters = next;

            g_slist_free_1 (l);
            l = next;
        }
        else
        {
            prev = l;
            l = l->next;
        }
    }

    if (is_tree (tree))
    {
        GtkTreeIter child;

        /* get the parent, in case we're removing its last child */
        donna_tree_store_iter_parent (priv->store, &parent, iter);
        /* we need to remove all children before we remove the row, so we can
         * have said children processed correctly (through here) as well */
        if (donna_tree_store_iter_children (priv->store, &child, iter))
            while (remove_row_from_tree (tree, &child, is_removal))
                ;
    }
    /* now we can remove the row */
    ret = donna_tree_store_remove (priv->store, iter);
    /* we have a parent, it has no more children, update expand state */
    if (is_tree (tree) && parent.stamp != 0
            && !donna_tree_store_iter_has_child (priv->store, &parent))
    {
        enum tree_expand es;

        gtk_tree_model_get (model, &parent,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        if (es == DONNA_TREE_EXPAND_PARTIAL)
        {
            es = DONNA_TREE_EXPAND_UNKNOWN;
            /* add a fake row */
            donna_tree_store_insert_with_values (priv->store, NULL, &parent, 0,
                    DONNA_TREE_COL_NODE,    NULL,
                    -1);
        }
        else
            es = DONNA_TREE_EXPAND_NONE;
        set_es (priv, &parent, es);
    }

    return ret;
}

static void
set_children (DonnaTreeView *tree,
              GtkTreeIter   *iter,
              GPtrArray     *children,
              gboolean       expand)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;

    if (!is_tree (tree))
        return;

    if (children->len == 0)
    {
        GtkTreeIter child;

        /* set new expand state */
        set_es (priv, iter, DONNA_TREE_EXPAND_NONE);
        if (donna_tree_store_iter_children (priv->store, &child, iter))
            while (remove_row_from_tree (tree, &child, TRUE))
                ;
    }
    else
    {
        GSList *list = NULL;
        enum tree_expand es;
        guint i;
        gboolean has_children = FALSE;

        gtk_tree_model_get (model, iter,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        if (es == DONNA_TREE_EXPAND_MAXI || es == DONNA_TREE_EXPAND_PARTIAL)
        {
            GtkTreeIter i;

            donna_tree_store_iter_children (priv->store, &i, iter);
            do
            {
                list = g_slist_prepend (list, gtk_tree_iter_copy (&i));
            } while (donna_tree_store_iter_next (priv->store, &i));
            list = g_slist_reverse (list);
        }
        else
            es = DONNA_TREE_EXPAND_UNKNOWN;

        for (i = 0; i < children->len; ++i)
        {
            GtkTreeIter row;
            DonnaNode *node = children->pdata[i];

            /* in case we got children from a node_children signal, and there's
             * more types that we care for */
            if (!(donna_node_get_node_type (node) & priv->node_types))
                continue;

            /* shouldn't be able to fail/return FALSE */
            if (!add_node_to_tree (tree, iter, node, &row))
            {
                gchar *location = donna_node_get_location (node);
                g_critical ("Treeview '%s': failed to add node for '%s:%s'",
                        tree->priv->name,
                        donna_node_get_domain (node),
                        location);
                g_free (location);
            }
            else if (es)
            {
                GSList *l, *prev = NULL;

                /* remove the iter for that row */
                l = list;
                while (l)
                {
                    if (itereq ((GtkTreeIter *) l->data, &row))
                    {
                        if (prev)
                            prev->next = l->next;
                        else
                            list = l->next;

                        gtk_tree_iter_free (l->data);
                        g_slist_free_1 (l);
                        break;
                    }
                    prev = l;
                    l = prev->next;
                }
            }
            if (!has_children)
                has_children = donna_tree_store_iter_is_visible (priv->store, &row);
        }
        /* remove rows not in children */
        while (list)
        {
            GSList *l;

            l = list;
            remove_row_from_tree (tree, l->data, TRUE);
            gtk_tree_iter_free (l->data);
            list = l->next;
            g_slist_free_1 (l);
        }

        /* has_children could be FALSE when e.g. we got children from a
         * node_children signal, but none match our node_types */
        es = (has_children) ? DONNA_TREE_EXPAND_MAXI : DONNA_TREE_EXPAND_NONE;
        /* set new expand state */
        set_es (priv, iter, es);
        /* we might have to remove the fake node */
        if (es == DONNA_TREE_EXPAND_NONE)
        {
            GtkTreeIter child;

            if (donna_tree_store_iter_children (priv->store, &child, iter))
                do
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &child,
                            DONNA_TREE_COL_NODE,    &node,
                            -1);
                    if (!node)
                        remove_row_from_tree (tree, &child, FALSE);
                    else
                        g_object_unref (node);
                } while (donna_tree_store_iter_next (priv->store, &child));
        }
        if (has_children && expand)
        {
            GtkTreePath *path;

            /* and make sure the row gets expanded (since we "blocked" it when
             * clicked */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
            gtk_tree_path_free (path);
        }
    }
}

/* mode tree only */
static void
node_get_children_tree_cb (DonnaTask                   *task,
                           gboolean                     timeout_called,
                           struct node_children_data   *data)
{
    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
    {
        free_node_children_data (data);
        return;
    }

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        GtkTreeModel      *model;
        GtkTreePath       *path;
        DonnaNode         *node;
        gchar             *location;
        const GError      *error;

        /* collapse the node & set it to UNKNOWN (it might have been NEVER
         * before, but we don't know) so if the user tries an expansion again,
         * it is tried again. */
        model = GTK_TREE_MODEL (data->tree->priv->store);
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
                &data->iter);
        gtk_tree_view_collapse_row (GTK_TREE_VIEW (data->tree), path);
        gtk_tree_path_free (path);
        set_es (data->tree->priv, &data->iter, DONNA_TREE_EXPAND_UNKNOWN);

        /* explain ourself */
        gtk_tree_model_get (model, &data->iter,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        error = donna_task_get_error (task);
        location = donna_node_get_location (node);
        donna_app_show_error (data->tree->priv->app, error,
                "Treeview '%s': Failed to get children for node '%s:%s'",
                data->tree->priv->name,
                donna_node_get_domain (node),
                location);
        g_free (location);
        g_object_unref (node);

        free_node_children_data (data);
        return;
    }

    set_children (data->tree, &data->iter,
            g_value_get_boxed (donna_task_get_return_value (task)),
            TRUE /* expand row */);

    if (data->scroll_to_current)
        scroll_to_current (data->tree);

    /* for check_children_post_expand() or full_expand_children() */
    if (data->extra_callback)
        data->extra_callback (data->tree, &data->iter);

    free_node_children_data (data);
}

/* mode tree only */
/* this should only be used for rows that we want expanded and are either
 * NEVER or UNKNOWN */
static gboolean
expand_row (DonnaTreeView           *tree,
            GtkTreeIter             *iter,
            gboolean                 scroll_current,
            node_children_extra_cb   extra_callback)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    DonnaNode *node;
    DonnaTask *task;
    struct node_children_data *data;
    GSList *list;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_NODE,    &node,
            -1);
    if (!node)
    {
        g_warning ("Treeview '%s': expand_row() failed to get node from model",
                priv->name);
        return FALSE;
    }

    /* is there another tree node for this node? */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (list)
    {
        for ( ; list; list = list->next)
        {
            GtkTreeIter *i = list->data;
            enum tree_expand es;

            /* skip ourself */
            if (itereq (iter, i))
                continue;

            gtk_tree_model_get (model, i,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
            if (es == DONNA_TREE_EXPAND_MAXI)
            {
                GtkTreeIter child;
                GtkTreePath *path;

                /* let's import the children */

                if (!donna_tree_store_iter_children (priv->store,
                            &child, i))
                {
                    g_critical ("Treeview '%s': Inconsistency detected",
                            priv->name);
                    continue;
                }

                do
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &child,
                            DONNA_TREE_COL_NODE,    &node,
                            -1);
                    add_node_to_tree (tree, iter, node, NULL);
                    g_object_unref (node);
                } while (donna_tree_store_iter_next (priv->store, &child));

                /* update expand state */
                set_es (priv, iter, DONNA_TREE_EXPAND_MAXI);

                /* expand node */
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
                gtk_tree_path_free (path);

                if (scroll_current)
                    scroll_to_current (tree);

                if (extra_callback)
                    extra_callback (tree, iter);

                return TRUE;
            }
        }
    }

    /* can we get them from our sync_with list ? (typical case of expansion of
     * the current location, when it was set from sync_with, i.e. we ignored the
     * node_children signal then because it wasn't yet our current location) */
    if (node == priv->location && priv->sync_with)
    {
        GPtrArray *arr;

        arr = donna_tree_view_get_children (priv->sync_with, node, priv->node_types);
        if (arr)
        {
            guint i;
            GtkTreePath *path;

            /* quite unlikely, but still */
            if (arr->len == 0)
            {
                GtkTreeIter child;

                /* update expand state */
                set_es (priv, iter, DONNA_TREE_EXPAND_NONE);

                if (donna_tree_store_iter_children (priv->store, &child, iter))
                    while (remove_row_from_tree (tree, &child, TRUE))
                        ;

                if (scroll_current)
                    scroll_to_current (tree);

                return TRUE;
            }

            for (i = 0; i < arr->len; ++i)
                add_node_to_tree (tree, iter, arr->pdata[i], NULL);
            g_ptr_array_unref (arr);

            /* update expand state */
            set_es (priv, iter, DONNA_TREE_EXPAND_MAXI);

            /* expand node */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
            gtk_tree_path_free (path);

            if (scroll_current)
                scroll_to_current (tree);

            if (extra_callback)
                extra_callback (tree, iter);

            return TRUE;
        }
    }

    task = donna_node_get_children_task (node, priv->node_types, NULL);

    data = g_slice_new0 (struct node_children_data);
    data->tree  = tree;
    data->scroll_to_current = scroll_current;
    data->iter = *iter;
    watch_iter (tree, &data->iter);
    data->extra_callback = extra_callback;

    /* FIXME: timeout_delay must be an option */
    donna_task_set_timeout (task, 800,
            (task_timeout_fn) node_get_children_tree_timeout,
            data,
            NULL);
    donna_task_set_callback (task,
            (task_callback_fn) node_get_children_tree_cb,
            data,
            (GDestroyNotify) free_node_children_data);

    set_es (priv, &data->iter, DONNA_TREE_EXPAND_WIP);

    donna_app_run_task (priv->app, task);
    g_object_unref (node);
    return FALSE;
}

/* mini-tree only */
static gboolean
maxi_expand_row (DonnaTreeView  *tree,
                 GtkTreeIter    *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    if (es != DONNA_TREE_EXPAND_PARTIAL)
    {
        GtkTreePath *path;
        gboolean ret;

        path = gtk_tree_model_get_path (model, iter);
        ret = !gtk_tree_view_row_expanded ((GtkTreeView *) tree, path);
        if (ret)
            gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
        gtk_tree_path_free (path);
        return ret;
    }

    expand_row (tree, iter, FALSE,
            /* if we're not "in sync" with our list (i.e. there's no row
             * for it) we attach the extra callback to check for it once
             * children will have been added.
             * We also have the check run on every row-expanded, but
             * this is still needed because the row could be expanded to
             * only show the "fake/please wait" node... */
            (!priv->location && priv->sync_with)
            ? (node_children_extra_cb) check_children_post_expand
            : NULL);
    return TRUE;
}

/* tree only */
static gboolean
maxi_collapse_row (DonnaTreeView    *tree,
                   GtkTreeIter      *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;
    GtkTreePath *path;
    gboolean ret;

    path = gtk_tree_model_get_path (model, iter);
    ret = gtk_tree_view_row_expanded ((GtkTreeView *) tree, path);
    if (ret)
        gtk_tree_view_collapse_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    if (es == DONNA_TREE_EXPAND_PARTIAL || es == DONNA_TREE_EXPAND_MAXI)
    {
        GtkTreeIter it;

        /* remove all children */
        if (donna_tree_store_iter_children (priv->store, &it, iter))
            while (remove_row_from_tree (tree, &it, FALSE))
                ;
        /* remove_row_from_tree will add a fake node & set expand_state to
         * UNKNOWN if it was partial. However, if not it won't add the fake node
         * and set expand_state to NONE so we need to adjust things */
        if (es == DONNA_TREE_EXPAND_MAXI)
        {
            /* add fake node */
            donna_tree_store_insert_with_values (priv->store, NULL, iter, 0,
                    DONNA_TREE_COL_NODE,    NULL,
                    -1);
            /* update expand state */
            set_es (priv, iter, DONNA_TREE_EXPAND_UNKNOWN);
        }
    }

    return ret;
}

static gboolean
donna_tree_view_test_expand_row (GtkTreeView    *treev,
                                 GtkTreeIter    *iter,
                                 GtkTreePath    *path)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv = tree->priv;
    enum tree_expand expand_state;

    if (!is_tree (tree))
        /* no expansion */
        return TRUE;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), iter,
            DONNA_TREE_COL_EXPAND_STATE,    &expand_state,
            -1);
    switch (expand_state)
    {
        /* allow expansion */
        case DONNA_TREE_EXPAND_WIP:
        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_MAXI:
            return FALSE;

        /* refuse expansion, import_children or get_children */
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
            /* this will add an idle source import_children, or start a new task
             * get_children */
            expand_row (tree, iter, FALSE,
                    /* if we're not "in sync" with our list (i.e. there's no row
                     * for it) we attach the extra callback to check for it once
                     * children will have been added.
                     * We also have the check run on every row-expanded, but
                     * this is still needed because the row could be expanded to
                     * only show the "fake/please wait" node... */
                    (!priv->location && priv->sync_with)
                    ? (node_children_extra_cb) check_children_post_expand
                    : NULL);
            return TRUE;

        /* refuse expansion. This case should never happen */
        case DONNA_TREE_EXPAND_NONE:
            g_critical ("Treeview '%s' wanted to expand a node without children",
                    priv->name);
            return TRUE;
    }
    /* should never be reached */
    g_critical ("Treeview '%s': invalid expand state: %d",
            priv->name,
            expand_state);
    return FALSE;
}

/* mode tree only -- assumes that list don't have expander */
static void
donna_tree_view_row_collapsed (GtkTreeView   *treev,
                               GtkTreeIter   *iter,
                               GtkTreePath   *path)
{
    /* this node was collapsed, update the flag */
    donna_tree_store_set (((DonnaTreeView *) treev)->priv->store, iter,
            DONNA_TREE_COL_EXPAND_FLAG, FALSE,
            -1);
    /* After row is collapsed, there might still be an horizontal scrollbar,
     * because the column has been enlarged due to a long-ass children, and
     * it hasn't been resized since. So even though there's no need for the
     * scrollbar anymore, it remains there.
     * Since we only have one column, we trigger an autosize to get rid of the
     * horizontal scrollbar (or adjust its size) */
    if (is_tree ((DonnaTreeView *) treev))
        gtk_tree_view_columns_autosize (treev);
}

static void
donna_tree_view_row_expanded (GtkTreeView   *treev,
                              GtkTreeIter   *iter,
                              GtkTreePath   *path)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaTreeViewPrivate * priv = tree->priv;
    GtkTreeIter child;

    /* this node was expanded, update the flag */
    donna_tree_store_set (((DonnaTreeView *) treev)->priv->store, iter,
            DONNA_TREE_COL_EXPAND_FLAG, TRUE,
            -1);
    /* also go through all its children and expand them if the flag is set, tjus
     * restoring the previous expand state. This expansion will trigger a new
     * call to this very function, thus taking care of the recursion */
    if (gtk_tree_model_iter_children ((GtkTreeModel *) priv->store, &child, iter))
    {
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
        do
        {
            gboolean expand_flag;

            gtk_tree_model_get (model, &child,
                    DONNA_TREE_COL_EXPAND_FLAG, &expand_flag,
                    -1);
            if (expand_flag)
            {
                GtkTreePath *p = gtk_tree_model_get_path (model, &child);
                gtk_tree_view_expand_row (treev, p, FALSE);
                gtk_tree_path_free (p);
            }
        } while (gtk_tree_model_iter_next (model, &child));
    }

    if (is_tree (tree) && !priv->location && priv->sync_with)
        check_children_post_expand (tree, iter);
}

static gboolean
visible_func (GtkTreeModel  *model,
              GtkTreeIter   *iter,
              DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter *it_cur;
    DonnaNode *node;
    gchar *name;
    gboolean ret;

    if (tree->priv->show_hidden)
        return TRUE;

    /* special case for (future) current location: we want to always show it,
     * and its parents. future_location_iter will be set during the sync/change
     * of location, and then has precedence over the actual current location. */
    if (priv->future_location_iter.stamp != 0)
        it_cur = &priv->future_location_iter;
    else if (priv->location_iter.stamp != 0)
        it_cur = &priv->location_iter;
    else
        it_cur = NULL;
    if (it_cur && (itereq (iter, it_cur)
            || donna_tree_store_is_ancestor (priv->store, iter, it_cur)))
        return TRUE;

    /* tree: if parent isn't visible, it isn't either. That's the rule our store
     * expect & therefore we must always enforce. This deals with both adding
     * children of an hidden folder (could happen while going there, first we
     * add children, then we'll make them visible) or when adding "fake node"
     * for non-visible iters. */
    if (is_tree (tree))
    {
        GtkTreeIter parent;

        donna_tree_store_iter_parent (priv->store, &parent, iter);
        if (parent.stamp != 0
                && !donna_tree_store_iter_is_visible (priv->store, &parent))
            return FALSE;
    }

    gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return TRUE;

    name = donna_node_get_name (node);
    ret = name[0] != '.';
    g_free (name);
    g_object_unref (node);

    return ret;
}

struct refresh_node_props_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
    GPtrArray     *props;
};

static void
free_refresh_node_props_data (struct refresh_node_props_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    g_mutex_lock (&priv->refresh_node_props_mutex);
    priv->refresh_node_props = g_slist_remove (priv->refresh_node_props, data);
    g_mutex_unlock (&priv->refresh_node_props_mutex);

    g_object_unref (data->node);
    g_ptr_array_unref (data->props);
    g_free (data);
}

/* Usually, upon a provider's node-updated signal, we check if the node is in
 * the tree, and if the property is one that our columns use; If so, we trigger
 * a refresh of that row (i.e. trigger a row-updated on store)
 * However, there's an exception: a columntype can, on render, give a list of
 * properties to be refreshed. We then store those properties on
 * priv->refresh_node_props as we run a task to refresh them. During that time,
 * those properties (on that node) will *not* trigger a refresh, as they usually
 * would. Instead, it's only when this callback is triggered that, if *all*
 * properties were refreshed, the refresh will be triggered (on the tree) */
static void
refresh_node_prop_cb (DonnaTask                      *task,
                      gboolean                        timeout_called,
                      struct refresh_node_props_data *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_DONE)
    {
        /* no return value means all props were refreshed, i.e. full success */
        if (!donna_task_get_return_value (task))
        {
            DonnaTreeViewPrivate *priv = data->tree->priv;
            GtkTreeModel *model;
            GSList *list;

            list = g_hash_table_lookup (priv->hashtable, data->node);
            if (!list)
            {
                g_critical ("Treeview '%s': refresh_node_prop_cb for missing node",
                        priv->name);
                goto bail;
            }

            model = GTK_TREE_MODEL (data->tree->priv->store);
            for ( ; list; list = list->next)
            {
                GtkTreeIter *iter = list->data;
                GtkTreePath *path;

                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }
    }
bail:
    free_refresh_node_props_data (data);
}

static gboolean
spinner_fn (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    guint i;
    gboolean active = FALSE;

    if (!priv->active_spinners_id)
        return FALSE;

    if (!priv->active_spinners->len)
    {
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    model = GTK_TREE_MODEL (priv->store);
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        struct active_spinners *as = priv->active_spinners->pdata[i];
        GSList *list;
        guint j;
        gboolean refresh;

        refresh = FALSE;
        for (j = 0; j < as->as_cols->len; ++j)
        {
            struct as_col *as_col;

            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->nb > 0)
            {
                active = refresh = TRUE;
                break;
            }
        }
        if (!refresh)
            continue;

        list = g_hash_table_lookup (priv->hashtable, as->node);
        for ( ; list; list = list->next)
        {
            GtkTreeIter *iter = list->data;
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }

    if (!active)
    {
        /* there are active spinners only for error messages */
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    ++priv->active_spinners_pulse;
    return TRUE;
}

static gpointer
get_ct_data (const gchar *col_name, DonnaTreeView *tree)
{
    struct column *_col;

    /* since the col_name comes from user input, we could fail to find the
     * column in this case */
    _col = get_column_by_name (tree, col_name);
    return (G_LIKELY (_col)) ? _col->ct_data : NULL;
}

static void
apply_color_filters (DonnaTreeView      *tree,
                     GtkTreeViewColumn  *column,
                     GtkCellRenderer    *renderer,
                     DonnaNode          *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GError *err = NULL;
    gboolean visible;
    GSList *l;

    if (!g_type_is_a (G_TYPE_FROM_INSTANCE (renderer), GTK_TYPE_CELL_RENDERER_TEXT))
        return;

    g_object_get (renderer, "visible", &visible, NULL);
    if (!visible)
        return;

    if (!(priv->arrangement->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
        return;

    for (l = priv->arrangement->color_filters; l; )
    {
        DonnaColorFilter *cf = l->data;
        gboolean keep_going;

        if (donna_color_filter_apply_if_match (cf, (GObject *) renderer,
                    get_column_by_column (tree, column)->name,
                    node, (get_ct_data_fn) get_ct_data, tree, &keep_going, &err))
        {
            if (!keep_going)
                break;
        }
        else if (err)
        {
            GSList *ll;
            gchar *filter;

            /* remove color filter */
            g_object_get (cf, "filter", &filter, NULL);
            donna_app_show_error (priv->app, err,
                    "Ignoring color filter '%s'", filter);
            g_free (filter);
            g_clear_error (&err);

            ll = l;
            l = l->next;
            priv->arrangement->color_filters = g_slist_delete_link (
                    priv->arrangement->color_filters, ll);
            continue;
        }

        l = l->next;
    }
}

/* Because the same renderers are used on all columns, we need to reset their
 * properties so they don't "leak" to other columns. If we used a model, every
 * row would have a foobar-set to TRUE or FALSE accordingly.
 * But we don't, and not all columntypes will set the same properties, also we
 * have things like color filters that also may set some.
 * So we need to reset whatever what set last time a renderer was used. An easy
 * way would be to connect to notify beforehand, have the ct & color filters do
 * their things, w/ our handler keep track of what needs to be reset next time.
 * Unfortunately, this can't be done because by the time we're done in rend_func
 * and therfore disconnect, no signal has been emitted yet. And since we
 * disconnect, we won't get to process anything.
 * The way we deal with all this is, we ask anything that sets a property
 * xalign, highlight and *-set on a renderer to also call this function, with
 * names of properties that shall be reset before next use. */
void
donna_renderer_set (GtkCellRenderer *renderer,
                    const gchar     *first_prop,
                    ...)
{
    GPtrArray *arr = NULL;
    va_list va_args;
    const gchar *prop;

    arr = g_object_get_data ((GObject *) renderer, "renderer-props");
    prop = first_prop;
    va_start (va_args, first_prop);
    while (prop)
    {
        g_ptr_array_add (arr, g_strdup (prop));
        prop = va_arg (va_args, const gchar *);
    }
    va_end (va_args);
}

static void
rend_func (GtkTreeViewColumn  *column,
           GtkCellRenderer    *renderer,
           GtkTreeModel       *model,
           GtkTreeIter        *iter,
           gpointer            data)
{
    DonnaTreeView *tree;
    DonnaTreeViewPrivate *priv;
    struct column *_col;
    DonnaNode *node;
    guint index = GPOINTER_TO_UINT (data);
    GPtrArray *arr;
    guint i;

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;

    /* spinner */
    if (index == INTERNAL_RENDERER_SPINNER || index == INTERNAL_RENDERER_PIXBUF)
    {
        struct active_spinners *as;
        DonnaNode *node;

        if (!priv->active_spinners->len)
        {
            g_object_set (renderer,
                    "visible",  FALSE,
                    NULL);
            return;
        }

        gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
        if (!node)
            return;

        as = get_as_for_node (tree, node, NULL, FALSE);
        g_object_unref (node);
        if (as)
        {
            guint i;

            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                if (index == INTERNAL_RENDERER_SPINNER)
                {
                    if (as_col->nb > 0)
                    {
                        g_object_set (renderer,
                                "visible",  TRUE,
                                "active",   TRUE,
                                "pulse",    priv->active_spinners_pulse,
                                NULL);
                        return;
                    }
                }
                else /* INTERNAL_RENDERER_PIXBUF */
                {
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            g_object_set (renderer,
                                    "visible",      TRUE,
                                    "stock-id",     GTK_STOCK_DIALOG_WARNING,
                                    "follow-state", TRUE,
                                    NULL);
                            return;
                        }
                    }
                }
                break;
            }
        }

        g_object_set (renderer,
                "visible",  FALSE,
                NULL);
        return;
    }

    /* reset any properties that was used last time on this renderer. See
     * donna_renderer_set() for more */
    arr = g_object_get_data ((GObject *) renderer, "renderer-props");
    for (i = 0; i < arr->len; )
    {
        if (streq (arr->pdata[i], "xalign"))
            g_object_set ((GObject *) renderer, "xalign", 0.0, NULL);
        else if (streq (arr->pdata[i], "highlight"))
            g_object_set ((GObject *) renderer, "highlight", NULL, NULL);
        else
            g_object_set ((GObject *) renderer, arr->pdata[i], FALSE, NULL);
        /* brings the last item to index i, hence no need to increment i */
        g_ptr_array_remove_index_fast (arr, i);
    }

    index -= NB_INTERNAL_RENDERERS - 1; /* -1 to start with index 1 */

    _col = get_column_by_column (tree, column);
    gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);

    if (is_tree (tree))
    {
        if (!node)
        {
            /* this is a "fake" node, shown as a "Please Wait..." */
            /* we can only do that for a column of type "name" */
            if (G_TYPE_FROM_INSTANCE (_col->ct) != DONNA_TYPE_COLUMNTYPE_NAME)
                return;

            if (index == 1)
                /* GtkRendererPixbuf */
                g_object_set (renderer, "visible", FALSE, NULL);
            else /* index == 2 */
                /* GtkRendererText */
                g_object_set (renderer,
                        "visible",  TRUE,
                        "text",     "Please Wait...",
                        NULL);

            return;
        }
    }
    else if (!node)
        return;

    arr = donna_columntype_render (_col->ct, _col->ct_data, index, node, renderer);

    /* visuals */
    if (is_tree (tree)
            && G_TYPE_FROM_INSTANCE (_col->ct) == DONNA_TYPE_COLUMNTYPE_NAME)
    {
        if (index == 1)
        {
            /* GtkRendererPixbuf */
            GdkPixbuf *pixbuf;

            gtk_tree_model_get (model, iter,
                    DONNA_TREE_COL_ICON,    &pixbuf,
                    -1);
            if (pixbuf)
            {
                g_object_set (renderer, "pixbuf", pixbuf, NULL);
                g_object_unref (pixbuf);
            }
        }
        else /* index == 2 */
        {
            /* DonnaRendererText */
            gchar *name, *highlight;

            gtk_tree_model_get (model, iter,
                    DONNA_TREE_COL_NAME,        &name,
                    DONNA_TREE_COL_HIGHLIGHT,   &highlight,
                    -1);
            if (name)
            {
                g_object_set (renderer, "text", name, NULL);
                g_free (name);
            }
            if (highlight)
            {
                g_object_set (renderer, "highlight", highlight, NULL);
                donna_renderer_set (renderer, "highlight", NULL);
                g_free (highlight);
            }
        }
    }

    if (arr)
    {
        DonnaTask *task;
        struct refresh_node_props_data *data;

        /* ct wants some properties refreshed on node. See refresh_node_prop_cb */
        data = g_new0 (struct refresh_node_props_data, 1);
        data->tree  = tree;
        data->node  = g_object_ref (node);
        data->props = g_ptr_array_ref (arr);

        g_mutex_lock (&priv->refresh_node_props_mutex);
        priv->refresh_node_props = g_slist_append (priv->refresh_node_props, data);
        g_mutex_unlock (&priv->refresh_node_props_mutex);

        task = donna_node_refresh_arr_task (node, arr, NULL);
        donna_task_set_callback (task,
                (task_callback_fn) refresh_node_prop_cb,
                data,
                (GDestroyNotify) free_refresh_node_props_data);
        donna_app_run_task (priv->app, task);
    }
    else
        apply_color_filters (tree, column, renderer, node);
    g_object_unref (node);
}

static gint
sort_func (GtkTreeModel      *model,
           GtkTreeIter       *iter1,
           GtkTreeIter       *iter2,
           GtkTreeViewColumn *column)
{
    DonnaTreeView *tree;
    DonnaTreeViewPrivate *priv;
    GtkSortType sort_order;
    struct column *_col;
    DonnaNode *node1;
    DonnaNode *node2;
    gint ret;

    gtk_tree_model_get (model, iter1, DONNA_TREE_COL_NODE, &node1, -1);
    /* one node could be a "fake" one, i.e. node is a NULL pointer */
    if (!node1)
        return -1;

    gtk_tree_model_get (model, iter2, DONNA_TREE_COL_NODE, &node2, -1);
    if (!node2)
    {
        g_object_unref (node1);
        return 1;
    }

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;

    /* are iters roots? */
    if (is_tree (tree) && donna_tree_store_iter_depth (priv->store, iter1) == 0)
    {
        GSList *l;

        /* so we decide the order. First one on our (ordered) list is first */
        for (l = priv->roots; l; l = l->next)
            if (itereq (iter1, (GtkTreeIter *) l->data))
                return -1;
            else if (itereq (iter2, (GtkTreeIter *) l->data))
                return 1;
        g_warning ("Treeview '%s': Failed to find order of roots", priv->name);
    }

    sort_order = gtk_tree_view_column_get_sort_order (column);

    if (priv->sort_groups != SORT_CONTAINER_MIXED)
    {
        DonnaNodeType type1, type2;

        type1 = donna_node_get_node_type (node1);
        type2 = donna_node_get_node_type (node2);

        if (type1 == DONNA_NODE_CONTAINER)
        {
            if (type2 != DONNA_NODE_CONTAINER)
            {
                if (priv->sort_groups == SORT_CONTAINER_FIRST)
                    ret = -1;
                else /* SORT_CONTAINER_FIRST_ALWAYS */
                    ret = (sort_order == GTK_SORT_ASCENDING) ? -1 : 1;
                goto done;
            }
        }
        else if (type2 == DONNA_NODE_CONTAINER)
        {
            if (priv->sort_groups == SORT_CONTAINER_FIRST)
                ret = 1;
            else /* SORT_CONTAINER_FIRST_ALWAYS */
                ret = (sort_order == GTK_SORT_ASCENDING) ? 1 : -1;
            goto done;
        }
    }

    _col = get_column_by_column (tree, column);
    ret = donna_columntype_node_cmp (_col->ct, _col->ct_data, node1, node2);

    /* second sort order */
    if (ret == 0 && priv->second_sort_column
            /* could be the same column with second_sort_sticky */
            && priv->second_sort_column != column)
    {
        column = priv->second_sort_column;
        _col = get_column_by_column (tree, column);

        ret = donna_columntype_node_cmp (_col->ct, _col->ct_data, node1, node2);
        if (ret != 0)
        {
            /* if second order is DESC, we should invert ret. But, if the
             * main order is DESC, the store will already invert the return
             * value of this function. */
            if (priv->second_sort_order == GTK_SORT_DESCENDING)
                ret *= -1;
            if (sort_order == GTK_SORT_DESCENDING)
                ret *= -1;
        }
    }

done:
    g_object_unref (node1);
    g_object_unref (node2);
    return ret;
}

static void
node_has_children_cb (DonnaTask                 *task,
                      gboolean                   timeout_called,
                      struct node_children_data *data)
{
    DonnaTreeStore *store = data->tree->priv->store;
    GtkTreeModel *model = GTK_TREE_MODEL (store);
    DonnaTaskState state;
    const GValue *value;
    gboolean has_children;
    enum tree_expand es;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        goto free;

    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
        /* we don't know if the node has children, so we'll keep the fake node
         * in, with expand state to UNKNOWN as it is. That way the user can ask
         * for expansion, which could simply have the expander removed if it
         * wasn't needed after all... */
        goto free;

    value = donna_task_get_return_value (task);
    has_children = g_value_get_boolean (value);

    gtk_tree_model_get (model, &data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    switch (es)
    {
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
        case DONNA_TREE_EXPAND_WIP:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* remove fake node */
                if (donna_tree_store_iter_children (store, &iter, &data->iter))
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &iter,
                            DONNA_TREE_VIEW_COL_NODE,   &node,
                            -1);
                    if (!node)
                        donna_tree_store_remove (store, &iter);
                }
                /* update expand state */
                set_es (data->tree->priv, &data->iter, DONNA_TREE_EXPAND_NONE);
            }
            else
            {
                /* fake node already there, we just update the expand state,
                 * unless we're WIP then we'll let get_children set it right
                 * once the children have been added */
                if (es == DONNA_TREE_EXPAND_UNKNOWN)
                    set_es (data->tree->priv, &data->iter, DONNA_TREE_EXPAND_NEVER);
            }
            break;

        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_MAXI:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* update expand state */
                set_es (data->tree->priv, &data->iter, DONNA_TREE_EXPAND_NONE);
                /* remove all children */
                if (donna_tree_store_iter_children (store, &iter, &data->iter))
                    while (remove_row_from_tree (data->tree, &iter, TRUE))
                        ;
            }
            /* else: children and expand state obviously already good */
            break;

        case DONNA_TREE_EXPAND_NONE:
            if (has_children)
            {
                /* add fake node */
                donna_tree_store_insert_with_values (store,
                        NULL, &data->iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);
                /* update expand state */
                set_es (data->tree->priv, &data->iter, DONNA_TREE_EXPAND_NEVER);
            }
            /* else: already no fake node */
            break;
    }

free:
    free_node_children_data (data);
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 DonnaTreeView  *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GSList *list, *l;
    guint i;

    /* do we have this node on tree? */
    l = g_hash_table_lookup (priv->hashtable, node);
    if (!l)
        return;

    /* should that property cause a refresh? */
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (name, cp->prop))
            break;
    }
    if (i >= priv->col_props->len)
        return;

    /* should we ignore this prop/node combo ? See refresh_node_prop_cb */
    g_mutex_lock (&priv->refresh_node_props_mutex);
    for (list = priv->refresh_node_props; list; list = list->next)
    {
        struct refresh_node_props_data *data = list->data;

        if (data->node == node)
        {
            for (i = 0; i < data->props->len; ++i)
            {
                if (streq (name, data->props->pdata[i]))
                    break;
            }
            if (i < data->props->len)
                break;
        }
    }
    g_mutex_unlock (&priv->refresh_node_props_mutex);
    if (list)
        return;

    /* trigger refresh on all rows for that node */
    model = GTK_TREE_MODEL (priv->store);
    for ( ; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
}

struct node_removed_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *location;
};

static void
free_node_removed_data (struct node_removed_data *data)
{
    g_object_unref (data->node);
    g_free (data->location);
    g_free (data);
}

static void
list_go_up_cb (DonnaTask                *task,
               gboolean                  timeout_called,
               struct node_removed_data *data)
{
    GError *err = NULL;
    DonnaTask *t;
    gchar *s;

    if (!task)
        data->location = donna_node_get_location (data->node);
    else if (donna_task_get_state (task) == DONNA_TASK_DONE)
    {
        DonnaNode *node;

        node = g_value_get_object (donna_task_get_return_value (task));
        if (!donna_tree_view_set_location (data->tree, node, &err))
        {
            gchar *fl = donna_node_get_full_location (data->node);
            donna_app_show_error (data->tree->priv->app, err,
                    "Treeview '%s': Failed to go to '%s' (as parent of '%s')",
                    data->tree->priv->name, data->location, fl);
            g_free (fl);
        }
        free_node_removed_data (data);
        return;
    }

    if (streq (data->location, "/"))
    {
        gchar *fl = donna_node_get_full_location (data->node);
        donna_app_show_error (data->tree->priv->app, err,
                "Treeview '%s': Failed to go to any parent of '%s'",
                data->tree->priv->name, fl);
        g_free (fl);
        free_node_removed_data (data);
        return;
    }

    /* location can't be "/" since root can't be removed */
    s = strrchr (data->location, '/');
    if (s == data->location)
        ++s;
    *s = '\0';

    t = donna_provider_get_node_task (donna_node_peek_provider (data->node),
            data->location, &err);
    if (!t)
    {
        gchar *fl = donna_node_get_full_location (data->node);
        donna_app_show_error (data->tree->priv->app, err,
                "Treeview '%s': Failed to go to a parent of '%s'",
                data->tree->priv->name, fl);
        g_free (fl);
        free_node_removed_data (data);
        return;
    }
    donna_task_set_callback (t, (task_callback_fn) list_go_up_cb, data,
            (GDestroyNotify) free_node_removed_data);
    donna_app_run_task (data->tree->priv->app, t);
}

static gboolean
real_node_removed_cb (struct node_removed_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;
    GSList *next;

    if (!is_tree (data->tree) && priv->location == data->node)
    {
        if (donna_provider_get_flags (donna_node_peek_provider (data->node))
                & DONNA_PROVIDER_FLAG_FLAT)
        {
            gchar *fl = donna_node_get_full_location (data->node);
            donna_app_show_error (priv->app, NULL,
                    "Treeview '%s': Current location (%s) has been removed",
                    priv->name, fl);
            g_free (fl);
            goto free;
        }
        list_go_up_cb (NULL, FALSE, data);
        return FALSE;
    }

    list = g_hash_table_lookup (priv->hashtable, data->node);
    if (!list)
        goto free;

    for ( ; list; list = next)
    {
        next = list->next;
        /* this will remove the row from the list in hashtable. IOW, it will
         * remove the current list element (list); which is why we took the next
         * element ahead of time */
        remove_row_from_tree (data->tree, list->data, TRUE);
    }

free:
    free_node_removed_data (data);
    /* don't repeat */
    return FALSE;
}

static void
node_removed_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 DonnaTreeView  *tree)
{
    struct node_removed_data *data;

    /* we might not be in the main thread, but we need to be */
    data = g_new0 (struct node_removed_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_removed_cb, data);
}

struct node_children_cb_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNodeType    node_types;
    GPtrArray       *children;
};

/* mode tree only */
static gboolean
real_node_children_cb (struct node_children_cb_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    enum tree_expand es;

    if (priv->location != data->node)
        goto free;

    if (!(data->node_types & priv->node_types))
        goto free;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &priv->location_iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    if (es == DONNA_TREE_EXPAND_MAXI)
    {
        g_debug ("treeview '%s': updating children for current location",
                priv->name);
        set_children (data->tree, &priv->location_iter, data->children, FALSE);
    }

free:
    g_object_unref (data->node);
    g_ptr_array_unref (data->children);
    g_free (data);

    /* don't repeat */
    return FALSE;
}

static void
node_children_cb (DonnaProvider  *provider,
                  DonnaNode      *node,
                  DonnaNodeType   node_types,
                  GPtrArray      *children,
                  DonnaTreeView  *tree)
{
    struct node_children_cb_data *data;

    if (!is_tree (tree))
        return;

    /* we might not be in the main thread, but we need to be */
    data = g_new (struct node_children_cb_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->children   = g_ptr_array_ref (children);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_children_cb, data);
}

struct new_child_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNode       *child;
};

static gboolean
real_new_child_cb (struct new_child_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;

    if (!is_tree (data->tree))
    {
        gboolean was_empty = g_hash_table_size (priv->hashtable) == 0;
        if (add_node_to_tree (data->tree, NULL, data->child, NULL) && was_empty)
        {
            GtkWidget *w;

            /* remove the "location empty" message */
            priv->draw_state = DRAW_NOTHING;

            /* we give the treeview the focus, to ensure the focused row is set,
             * hence the class focused-row applied */
            w = gtk_widget_get_toplevel ((GtkWidget *) data->tree);
            w = gtk_window_get_focus ((GtkWindow *) w);
            gtk_widget_grab_focus ((GtkWidget *) data->tree);
            gtk_widget_grab_focus ((w) ? w : (GtkWidget *) data->tree);
        }
        goto free;
    }

    list = g_hash_table_lookup (priv->hashtable, data->node);
    if (!list)
        goto free;

    for ( ; list; list = list->next)
        add_node_to_tree (data->tree, list->data, data->child, NULL);

free:
    g_object_unref (data->node);
    g_object_unref (data->child);
    g_free (data);
    /* no repeat */
    return FALSE;
}

static void
node_new_child_cb (DonnaProvider *provider,
                   DonnaNode     *node,
                   DonnaNode     *child,
                   DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct new_child_data *data;

    /* list: unless we're in node, we don't care */
    if (!is_tree (tree) && priv->location != node)
        return;

    /* if we don't care for this type of nodes, nothing to do */
    if (!(donna_node_get_node_type (child) & priv->node_types))
        return;

    /* we can't check if node is in the tree though, because there's no lock,
     * and we might not be in the main thread, and so we need to be */
    data = g_new (struct new_child_data, 1);
    data->tree  = tree;
    data->node  = g_object_ref (node);
    data->child = g_object_ref (child);
    g_main_context_invoke (NULL, (GSourceFunc) real_new_child_cb, data);
}

/* mode tree only */
static inline GtkTreeIter *
get_child_iter_for_node (DonnaTreeView  *tree,
                         GtkTreeIter    *parent,
                         DonnaNode      *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *list;

    list = g_hash_table_lookup (priv->hashtable, node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *i = list->data;
        GtkTreeIter  p;

        /* get the parent and compare with our parent iter */
        if (donna_tree_store_iter_parent (priv->store, &p, i)
                && itereq (&p, parent))
            return i;
    }
    return NULL;
}

struct node_visuals_data
{
    DonnaTreeView   *tree;
    GtkTreeIter      iter;
    DonnaNode       *node;
};

static void
free_node_visuals_data (struct node_visuals_data *data)
{
    g_slice_free (struct node_visuals_data, data);
}

static void
node_refresh_visuals_cb (DonnaTask                  *task,
                         gboolean                    timeout_called,
                         struct node_visuals_data   *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
    {
        free_node_visuals_data (data);
        return;
    }

    load_node_visuals (data->tree, &data->iter, data->node, FALSE);

    free_node_visuals_data (data);
}

#define add_prop(arr, prop) do {    \
    if (!arr)                       \
        arr = g_ptr_array_new ();   \
    g_ptr_array_add (arr, prop);    \
} while (0)
#define load_node_visual(UPPER, lower, GTYPE, get_fn, COLUMN)   do {                \
    if ((priv->node_visuals & DONNA_TREE_VISUAL_##UPPER)                            \
            && !(visuals & DONNA_TREE_VISUAL_##UPPER))                              \
    {                                                                               \
        donna_node_get (node, FALSE, "visual-" lower, &has, &value, NULL);          \
        switch (has)                                                                \
        {                                                                           \
            case DONNA_NODE_VALUE_NONE:                                             \
            case DONNA_NODE_VALUE_ERROR: /* not possible, avoids warning */         \
                break;                                                              \
            case DONNA_NODE_VALUE_NEED_REFRESH:                                     \
                if (allow_refresh)                                                  \
                    add_prop (arr, "visual-" lower);                                \
                break;                                                              \
            case DONNA_NODE_VALUE_SET:                                              \
                if (G_VALUE_TYPE (&value) != GTYPE)                                 \
                {                                                                   \
                    gchar *location = donna_node_get_location (node);               \
                    g_warning ("Treeview '%s': "                                    \
                            "Unable to load visual-" lower " from node '%s:%s', "   \
                            "property isn't of expected type (%s instead of %s)",   \
                            priv->name,                                             \
                            donna_node_get_domain (node),                           \
                            location,                                               \
                            G_VALUE_TYPE_NAME (&value),                             \
                            g_type_name (GTYPE));                                   \
                    g_free (location);                                              \
                }                                                                   \
                else                                                                \
                    donna_tree_store_set (priv->store, iter,                        \
                            DONNA_TREE_COL_##COLUMN,    g_value_##get_fn (&value),  \
                            -1);                                                    \
                g_value_unset (&value);                                             \
                break;                                                              \
        }                                                                           \
    }                                                                               \
} while (0)
/* mode tree only */
static inline void
load_node_visuals (DonnaTreeView    *tree,
                   GtkTreeIter      *iter,
                   DonnaNode        *node,
                   gboolean          allow_refresh)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaTreeVisual visuals;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GPtrArray *arr = NULL;

    gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
            DONNA_TREE_COL_VISUALS, &visuals,
            -1);

    load_node_visual (NAME,      "name",      G_TYPE_STRING,   get_string, NAME);
    load_node_visual (ICON,      "icon",      GDK_TYPE_PIXBUF, get_object, ICON);
    load_node_visual (BOX,       "box",       G_TYPE_STRING,   get_string, BOX);
    load_node_visual (HIGHLIGHT, "highlight", G_TYPE_STRING,   get_string, HIGHLIGHT);

    if (arr)
    {
        GError *err = NULL;
        DonnaTask *task;

        task = donna_node_refresh_arr_task (node, arr, &err);
        if (!task)
        {
            gchar *location = donna_node_get_location (node);
            donna_app_show_error (priv->app, err,
                    "Unable to refresh visuals on node '%s:%s'",
                    donna_node_get_domain (node),
                    location);
            g_free (location);
            g_clear_error (&err);
        }
        else
        {
            struct node_visuals_data *data;

            data = g_slice_new (struct node_visuals_data);
            data->tree = tree;
            data->iter = *iter;
            data->node = node;

            donna_task_set_callback (task,
                    (task_callback_fn) node_refresh_visuals_cb,
                    data,
                    (GDestroyNotify) free_node_visuals_data);
            donna_app_run_task (priv->app, task);
        }

        g_ptr_array_unref (arr);
    }
}
#undef load_node_visual
#undef add_prop

/* mode tree only */
static inline void
load_tree_visuals (DonnaTreeView    *tree,
                   GtkTreeIter      *iter,
                   DonnaNode        *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *fl;
    GSList *list, *l;
    GtkTreeIter *root;

    if (!priv->tree_visuals)
        return;

    fl = donna_node_get_full_location (node);
    list = g_hash_table_lookup (priv->tree_visuals, fl);
    if (!list)
    {
        g_free (fl);
        return;
    }

    root = get_root_iter (tree, iter);

    for (l = list; l; l = l->next)
    {
        struct visuals *visuals = l->data;

        if (visuals->root.stamp == 0)
        {
            GtkTreeIter it;

            /* invalid iter means user_data holds the "path" element, i.e.
             * number of root to use (starting at 0) */
            if (!gtk_tree_model_iter_nth_child ((GtkTreeModel *) priv->store,
                        &it, NULL, GPOINTER_TO_INT (visuals->root.user_data)))
                /* we don't (yet) have that root */
                continue;

            /* make it a valid iter pointing to the row */
            visuals->root = it;
        }

        if (itereq (root, &visuals->root))
        {
            DonnaTreeVisual v = 0;

            if (visuals->name)
            {
                v |= DONNA_TREE_VISUAL_NAME;
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_NAME,        visuals->name,
                        -1);
            }
            if (visuals->icon)
            {
                v |= DONNA_TREE_VISUAL_ICON;
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_ICON,        visuals->icon,
                        -1);
            }
            if (visuals->box)
            {
                v |= DONNA_TREE_VISUAL_BOX;
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_BOX,         visuals->box,
                        -1);
            }
            if (visuals->highlight)
            {
                v |= DONNA_TREE_VISUAL_HIGHLIGHT;
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_HIGHLIGHT,   visuals->highlight,
                        -1);
            }
            /* not a visual, but treated the same */
            if (visuals->clicks)
            {
                v |= DONNA_TREE_VISUAL_CLICKS;
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_CLICKS,  visuals->clicks,
                        -1);
            }
            donna_tree_store_set (priv->store, iter,
                    DONNA_TREE_COL_VISUALS,         v,
                    -1);

            /* now that it's loaded, remove from the list */
            free_visuals (visuals);
            list = g_slist_delete_link (list, l);
            if (list)
                /* will free fl */
                g_hash_table_insert (priv->tree_visuals, fl, list);
            else
            {
                g_hash_table_remove (priv->tree_visuals, fl);
                g_free (fl);
                if (g_hash_table_size (priv->tree_visuals) == 0)
                {
                    g_hash_table_unref (priv->tree_visuals);
                    priv->tree_visuals = NULL;
                }
            }

            return;
        }
    }
    g_free (fl);
}

static gboolean
add_node_to_tree (DonnaTreeView *tree,
                  GtkTreeIter   *parent,
                  DonnaNode     *node,
                  GtkTreeIter   *iter_row)
{
    const gchar             *domain;
    DonnaTreeViewPrivate    *priv;
    GtkTreeModel            *model;
    GtkTreeIter              iter;
    GtkTreeIter             *it;
    GSList                  *list;
    GSList                  *l;
    DonnaProvider           *provider;
    DonnaNodeType            node_type;
    DonnaTask               *task;
    gboolean                 added;
    guint                    i;
    GError                  *err = NULL;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv  = tree->priv;
    model = GTK_TREE_MODEL (priv->store);

    /* is there already a row for this node at that level? */
    if (parent)
    {
        GtkTreeIter *i;

        i = get_child_iter_for_node (tree, parent, node);
        if (i)
        {
            /* already exists under the same parent, nothing to do */
            if (iter_row)
                *iter_row = *i;
            return TRUE;
        }
    }

    gchar *location = donna_node_get_location (node);
    g_debug ("treeview '%s': adding new node %p for '%s:%s'",
            priv->name,
            node,
            donna_node_get_domain (node),
            location);
    g_free (location);

    if (!is_tree (tree))
    {
        /* mode list only */
        donna_tree_store_insert_with_values (priv->store, &iter, parent, -1,
                DONNA_LIST_COL_NODE,    node,
                -1);
        if (iter_row)
            *iter_row = iter;
        /* add it to our hashtable */
        list = g_hash_table_lookup (priv->hashtable, node);
        list = g_slist_prepend (list, gtk_tree_iter_copy (&iter));
        g_hash_table_insert (priv->hashtable, node, list);

        /* get provider to get task to know if it has children */
        provider = donna_node_peek_provider (node);
        for (i = 0; i < priv->providers->len; ++i)
        {
            struct provider_signals *ps = priv->providers->pdata[i];

            if (ps->provider == provider)
            {
                ps->nb_nodes++;
                break;
            }
        }
        if (i >= priv->providers->len)
        {
            struct provider_signals *ps;

            ps = g_new0 (struct provider_signals, 1);
            ps->provider = g_object_ref (provider);
            ps->nb_nodes = 1;
            ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                    G_CALLBACK (node_updated_cb), tree);
            ps->sid_node_removed = g_signal_connect (provider, "node-removed",
                    G_CALLBACK (node_removed_cb), tree);

            g_ptr_array_add (priv->providers, ps);
        }

        return TRUE;
    }

    /* mode tree only */

    /* check if the parent has a "fake" node as child, in which case we'll
     * re-use it instead of adding a new node */
    added = FALSE;
    if (parent && donna_tree_store_iter_children (priv->store, &iter, parent))
    {
        DonnaNode *n;

        gtk_tree_model_get (model, &iter,
                DONNA_TREE_COL_NODE,    &n,
                -1);
        if (!n)
        {
            donna_tree_store_set (priv->store, &iter,
                    DONNA_TREE_COL_NODE,            node,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                    -1);
            set_es (priv, &iter, DONNA_TREE_EXPAND_UNKNOWN);
            donna_tree_store_refresh_visibility (priv->store, &iter, NULL);
            added = TRUE;
        }
        else
            g_object_unref (n);
    }
    if (!added)
    {
        donna_tree_store_insert_with_values (priv->store, &iter, parent, -1,
                DONNA_TREE_COL_NODE,            node,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                -1);
        set_es (priv, &iter, DONNA_TREE_EXPAND_UNKNOWN);
    }
    if (iter_row)
    {
        *iter_row = iter;
        if (iter_row == &priv->future_location_iter)
            /* this means this is (a parent of) the future current location, and
             * we shall now ensure its visibility */
            donna_tree_store_refresh_visibility (priv->store, &iter, NULL);
    }
    /* add it to our hashtable */
    it   = gtk_tree_iter_copy (&iter);
    list = g_hash_table_lookup (priv->hashtable, node);
    list = g_slist_prepend (list, it);
    g_hash_table_insert (priv->hashtable, node, list);
    /* new root? */
    if (!parent)
        priv->roots = g_slist_append (priv->roots, it);
    /* visuals */
    load_tree_visuals (tree, &iter, node);
    load_node_visuals (tree, &iter, node, TRUE);
    /* check the list in case we have another tree node for that node, in which
     * case we might get the has_children info from there */
    added = FALSE;
    for (l = list; l; l = l->next)
    {
        GtkTreeIter *i = l->data;
        enum tree_expand es;

        if (itereq (&iter, i))
            continue;

        gtk_tree_model_get (model, i,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        switch (es)
        {
            /* node has children */
            case DONNA_TREE_EXPAND_NEVER:
            case DONNA_TREE_EXPAND_PARTIAL:
            case DONNA_TREE_EXPAND_MAXI:
                es = DONNA_TREE_EXPAND_NEVER;
                break;

            /* node doesn't have children */
            case DONNA_TREE_EXPAND_NONE:
                break;

            /* anything else is inconclusive */
            default:
                es = DONNA_TREE_EXPAND_UNKNOWN; /* == 0 */
        }

        if (es)
        {
            set_es (priv, &iter, es);
            if (es == DONNA_TREE_EXPAND_NEVER)
                /* insert a fake node so the user can ask for expansion */
                donna_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);
            added = TRUE;
            break;
        }
    }
    /* get provider to get task to know if it has children */
    provider = donna_node_peek_provider (node);
    node_type = donna_node_get_node_type (node);
    for (i = 0; i < priv->providers->len; ++i)
    {
        struct provider_signals *ps = priv->providers->pdata[i];

        if (ps->provider == provider)
        {
            ps->nb_nodes++;
            break;
        }
    }
    if (i >= priv->providers->len)
    {
        struct provider_signals *ps;

        ps = g_new0 (struct provider_signals, 1);
        ps->provider = g_object_ref (provider);
        ps->nb_nodes = 1;
        ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                G_CALLBACK (node_updated_cb), tree);
        ps->sid_node_removed = g_signal_connect (provider, "node-removed",
                G_CALLBACK (node_removed_cb), tree);
        if (node_type != DONNA_NODE_ITEM)
        {
            ps->sid_node_children = g_signal_connect (provider, "node-children",
                    G_CALLBACK (node_children_cb), tree);
            ps->sid_node_new_child = g_signal_connect (provider, "node-new-child",
                    G_CALLBACK (node_new_child_cb), tree);
        }

        g_ptr_array_add (priv->providers, ps);
    }

    if (added || node_type == DONNA_NODE_ITEM)
    {
        if (node_type == DONNA_NODE_ITEM)
            set_es (priv, &iter, DONNA_TREE_EXPAND_NONE);
        /* fix some weird glitch sometimes, when adding row/root on top and
         * scrollbar is updated */
        gtk_widget_queue_draw (GTK_WIDGET (tree));
        return TRUE;
    }

    task = donna_provider_has_node_children_task (provider, node,
            priv->node_types, &err);
    if (task)
    {
        struct node_children_data *data;

        data = g_slice_new0 (struct node_children_data);
        data->tree  = tree;
        data->iter  = iter;
        watch_iter (tree, &data->iter);

        /* insert a fake node so the user can ask for expansion right away (the
         * node will disappear if needed asap) */
        donna_tree_store_insert_with_values (priv->store,
                NULL, &data->iter, 0,
                DONNA_TREE_COL_NODE,    NULL,
                -1);

        donna_task_set_callback (task,
                (task_callback_fn) node_has_children_cb,
                data,
                (GDestroyNotify) free_node_children_data);
        donna_app_run_task (priv->app, task);
    }
    else
    {
        gchar *location;

        /* insert a fake node, so user can try again by asking to expand it */
        donna_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
            DONNA_TREE_COL_NODE,    NULL,
            -1);

        location = donna_node_get_location (node);
        g_warning ("Treeview '%s': Unable to create a task to determine if the node '%s:%s' has children: %s",
                priv->name, domain, location, err->message);
        g_free (location);
        g_clear_error (&err);
    }

    /* fix some weird glitch sometimes, when adding row/root on top and
     * scrollbar is updated */
    gtk_widget_queue_draw (GTK_WIDGET (tree));
    return TRUE;
}

gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node)
{
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (is_tree (tree), FALSE);

    ret = add_node_to_tree (tree, NULL, node, NULL);
    if (!tree->priv->arrangement)
        donna_tree_view_build_arrangement (tree, FALSE);
    return ret;
}

/* mode list only -- this is used to disallow dropping a column to the right of
 * the empty column (to make blank space there) */
static gboolean
col_drag_func (GtkTreeView          *treev,
               GtkTreeViewColumn    *col,
               GtkTreeViewColumn    *prev_col,
               GtkTreeViewColumn    *next_col,
               gpointer              data)
{
    if (!next_col && !get_column_by_column ((DonnaTreeView *) treev, prev_col))
        return FALSE;
    else
        return TRUE;
}

static gboolean
column_button_press_event_cb (GtkWidget         *btn,
                              GdkEventButton    *event,
                              struct column     *column)
{
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
        column->pressed = TRUE;
        column->ctrl_held = (event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK;
    }
    return FALSE;
}

static inline void
set_second_arrow (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;
    gboolean alt;
    GtkArrowType arrow_type;

    /* GTK settings whether to use sane/alternative arrows or not */
    g_object_get (gtk_widget_get_settings (GTK_WIDGET (tree)),
            "gtk-alternative-sort-arrows", &alt, NULL);

    if (priv->second_sort_order == GTK_SORT_ASCENDING)
        arrow_type = (alt) ? GTK_ARROW_UP : GTK_ARROW_DOWN;
    else
        arrow_type = (alt) ? GTK_ARROW_DOWN : GTK_ARROW_UP;

    /* show/update the second arrow */
    _col = get_column_by_column (tree, priv->second_sort_column);
    gtk_arrow_set (GTK_ARROW (_col->second_arrow), arrow_type, GTK_SHADOW_IN);
    /* visible unless main & second sort are the same */
    gtk_widget_set_visible (_col->second_arrow,
            priv->second_sort_column != priv->sort_column);
}

static void
set_sort_column (DonnaTreeView      *tree,
                 GtkTreeViewColumn  *column,
                 DonnaSortOrder      order,
                 gboolean            preserve_order)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;
    GtkTreeSortable *sortable;
    gint cur_sort_id;
    GtkSortType cur_sort_order;
    GtkSortType sort_order;

    _col = get_column_by_column (tree, column);
    sortable = GTK_TREE_SORTABLE (priv->store);
    gtk_tree_sortable_get_sort_column_id (sortable, &cur_sort_id, &cur_sort_order);

    if (priv->sort_column != column)
    {
        gboolean refresh_second_arrow = FALSE;

        /* new main sort on second sort column, remove the arrow */
        if (priv->second_sort_column == column)
            gtk_widget_set_visible (_col->second_arrow, FALSE);
        /* if not sticky, also remove the second sort */
        if (!priv->second_sort_sticky)
        {
            if (priv->second_sort_column && priv->second_sort_column != column)
                gtk_widget_set_visible (get_column_by_column (tree,
                            priv->second_sort_column)->second_arrow,
                        FALSE);
            priv->second_sort_column = NULL;
        }
        /* if sticky, and the old main sort is the second sort, bring back
         * the arrow (second sort is automatic, i.e. done when the second
         * sort column is set and isn't the main sort column, of course) */
        else if (priv->second_sort_column == priv->sort_column && priv->sort_column)
        {
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    TRUE);
            /* we need to call set_second_arrow() after we've updated
             * priv->sort_colmun, else since it's the same as second_sort_column
             * it won't make the arrow visible */
            refresh_second_arrow = TRUE;
        }

        /* handle the change of main sort column */
        if (priv->sort_column)
            gtk_tree_view_column_set_sort_indicator (priv->sort_column, FALSE);
        priv->sort_column = column;
        if (order != DONNA_SORT_UNKNOWN)
            sort_order = (order == DONNA_SORT_ASC)
                ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        else
            sort_order = donna_columntype_get_default_sort_order (
                    _col->ct, priv->name, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    _col->ct_data);
        if (refresh_second_arrow)
            set_second_arrow (tree);
    }
    else if (order != DONNA_SORT_UNKNOWN)
    {
        sort_order = (order == DONNA_SORT_ASC)
            ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        if (sort_order == cur_sort_order)
            return;
    }
    else if (preserve_order)
        return;
    else
        /* revert order */
        sort_order = (cur_sort_order == GTK_SORT_ASCENDING)
            ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    /* important to set the sort order on column before the sort_id on sortable,
     * since sort_func might use the column's sort_order (when putting container
     * always first) */
    gtk_tree_view_column_set_sort_indicator (column, TRUE);
    gtk_tree_view_column_set_sort_order (column, sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable, _col->sort_id, sort_order);
}


static void
set_second_sort_column (DonnaTreeView       *tree,
                        GtkTreeViewColumn   *column,
                        DonnaSortOrder       order,
                        gboolean             preserve_order)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;
    GtkTreeSortable *sortable;
    gint cur_sort_id;
    GtkSortType cur_sort_order;

    _col = get_column_by_column (tree, column);
    if (!column || priv->sort_column == column)
    {
        if (priv->second_sort_column)
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    FALSE);
        priv->second_sort_column = (priv->second_sort_sticky) ? column : NULL;
        return;
    }

    if (priv->second_sort_column != column)
    {
        if (priv->second_sort_column)
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    FALSE);
        priv->second_sort_column = column;
        if (order != DONNA_SORT_UNKNOWN)
            priv->second_sort_order = (order == DONNA_SORT_ASC)
                ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        else
            priv->second_sort_order = donna_columntype_get_default_sort_order (
                    _col->ct, priv->name, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    _col->ct_data);
    }
    else if (order != DONNA_SORT_UNKNOWN)
    {
        GtkSortType sort_order;

        sort_order = (order == DONNA_SORT_ASC)
            ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        if (sort_order == priv->second_sort_order)
            return;
        priv->second_sort_order = sort_order;
    }
    else if (preserve_order)
        return;
    else
        /* revert order */
        priv->second_sort_order =
            (priv->second_sort_order == GTK_SORT_ASCENDING)
            ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    /* show/update the second arrow */
    set_second_arrow (tree);

    /* trigger a resort */
    sortable = GTK_TREE_SORTABLE (priv->store);
    gtk_tree_sortable_get_sort_column_id (sortable, &cur_sort_id, &cur_sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable,
            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, cur_sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable, cur_sort_id, cur_sort_order);
}

/* we have a "special" handling of clicks on column headers. First off, we
 * don't use gtk_tree_view_column_set_sort_column_id() to handle the sorting
 * because we want control to do things like have a default order (ASC/DESC)
 * based on the type, etc
 * Then, we also don't use the signal clicked because we want to provider
 * support for a second sort order, which is why instead we're connecting to
 * signals of the button making the column header:
 * - in button-press-event (above) we set a flag stating that a click was done.
 *   We also set whether Ctrl was held or not
 * - in button-release-event (below) we check that flag. If there was a click,
 *   we then check that there's no DND class that was added (which would signal
 *   a dragging of the column (header) is taking place, in which case we shall
 *   ignore the click). If good, we can then process the click.
 * This should allow us to deal with a regular click as well as a Ctrl+click for
 * second order, while preserving normal drawing as well as dragging. */
static gboolean
column_button_release_event_cb (GtkWidget       *btn,
                                GdkEventButton  *event,
                                struct column   *column)
{
    GtkStyleContext *context;

    if (event->button != 1 || event->type != GDK_BUTTON_RELEASE || !column->pressed)
        return FALSE;

    column->pressed = FALSE;

    context = gtk_widget_get_style_context (btn);
    if (gtk_style_context_has_class (context, GTK_STYLE_CLASS_DND))
        return FALSE;

    /* ctrl+click on column for second sort; on current sort_column to turn off
     * the second_sort */
    if (column->ctrl_held)
        set_second_sort_column (column->tree, column->column, DONNA_SORT_UNKNOWN, FALSE);
    else
        set_sort_column (column->tree, column->column, DONNA_SORT_UNKNOWN, FALSE);

    return FALSE;
}

static inline void
free_arrangement (DonnaArrangement *arr)
{
    if (!arr)
        return;
    g_free (arr->columns);
    g_free (arr->sort_column);
    g_free (arr->second_sort_column);
    g_free (arr->columns_options);
    g_free (arr);
}

static gint
no_sort (GtkTreeModel *model, GtkTreeIter *i1, GtkTreeIter *i2, gpointer data)
{
    g_critical ("Treeview '%s': Invalid sorting function called",
            ((DonnaTreeView *) data)->priv->name);
    return 0;
}

/* those must only be used on arrangement from select_arrangement(), i.e. they
 * always have all elements (except maybe second_sort). Hence why we don't check
 * for that (again, except second_sort) */
#define must_load_columns(arr, cur_arr, force)                              \
     (force || !cur_arr || arr->flags & DONNA_ARRANGEMENT_COLUMNS_ALWAYS    \
         || !streq (cur_arr->columns, arr->columns))
#define must_load_sort(arr, cur_arr, force)                                 \
     (force || !cur_arr || arr->flags & DONNA_ARRANGEMENT_SORT_ALWAYS       \
         || !(cur_arr->sort_order == arr->sort_order                        \
             && streq (cur_arr->sort_column, arr->sort_column)))
#define must_load_second_sort(arr, cur_arr, force)                          \
    (arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT                         \
     && (force || !cur_arr                                                  \
         || arr->flags & DONNA_ARRANGEMENT_SECOND_SORT_ALWAYS               \
         || !(cur_arr->second_sort_order == arr->second_sort_order          \
             && cur_arr->second_sort_sticky == arr->second_sort_sticky      \
             && streq (cur_arr->second_sort_column,                         \
                 arr->second_sort_column))))
#define must_load_columns_options(arr, cur_arr, force)                      \
     (force || !cur_arr                                                     \
         || arr->flags & DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS           \
         || !streq (cur_arr->columns_options, arr->columns_options))

static void
load_arrangement (DonnaTreeView     *tree,
                  DonnaArrangement  *arrangement,
                  gboolean           force)
{
    DonnaTreeViewPrivate *priv  = tree->priv;
    DonnaConfig          *config;
    GtkTreeView          *treev = GTK_TREE_VIEW (tree);
    GtkTreeSortable      *sortable;
    GSList               *list;
    gboolean              free_sort_column = FALSE;
    gchar                *sort_column = NULL;
    DonnaSortOrder        sort_order = DONNA_SORT_UNKNOWN;
    gboolean              free_second_sort_column = FALSE;
    gchar                *second_sort_column = NULL;
    DonnaSortOrder        second_sort_order = DONNA_SORT_UNKNOWN;
    DonnaSecondSortSticky second_sort_sticky = DONNA_SECOND_SORT_STICKY_UNKNOWN;
    gchar                *col;
    GtkTreeViewColumn    *first_column = NULL;
    GtkTreeViewColumn    *last_column = NULL;
    GtkTreeViewColumn    *expander_column = NULL;
    DonnaColumnType      *ctname;
    gint                  sort_id = 0;

    config = donna_app_peek_config (priv->app);
    sortable = GTK_TREE_SORTABLE (priv->store);

    /* clear list of props we're watching to refresh tree */
    if (priv->col_props->len > 0)
        g_array_set_size (priv->col_props, 0);

    if (!is_tree (tree))
    {
        /* because setting it to NULL means the first visible column will be
         * used. If we don't want an expander to show (and just eat space), we
         * need to add an invisible column and set it as expander column */
        expander_column = gtk_tree_view_get_expander_column (treev);
        if (!expander_column)
        {
            expander_column = gtk_tree_view_column_new ();
            gtk_tree_view_column_set_sizing (expander_column,
                    GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_insert_column (treev, expander_column, 0);
        }
        last_column = expander_column;
    }
    else
        /* so we can make the first column to use it the expander column */
        ctname = donna_app_get_columntype (priv->app, "name");

    col = arrangement->columns;
    /* just to be safe, but this function should only be called with arrangement
     * having (at least) columns */
    if (G_UNLIKELY (!col))
    {
        g_critical ("Treeview '%s': load_arrangement() called on an arrangement without columns",
                priv->name);
        col = (gchar *) "name";
    }

    if (must_load_sort (arrangement, priv->arrangement, force))
    {
        sort_column = arrangement->sort_column;
        sort_order  = arrangement->sort_order;
    }
    else if (priv->sort_column)
    {
        sort_column = g_strdup (get_column_by_column (tree,
                    priv->sort_column)->name);
        free_sort_column = TRUE;
        /* also preserve sort order */
        sort_order  = (gtk_tree_view_column_get_sort_order (priv->sort_column)
                == GTK_SORT_ASCENDING) ? DONNA_SORT_ASC : DONNA_SORT_DESC;
    }

    if (must_load_second_sort (arrangement, priv->arrangement, force))
    {
        second_sort_column = arrangement->second_sort_column;
        second_sort_order  = arrangement->second_sort_order;
        second_sort_sticky = arrangement->second_sort_sticky;
    }
    else if (priv->second_sort_column)
    {
        second_sort_column = g_strdup (get_column_by_column (tree,
                    priv->second_sort_column)->name);
        free_second_sort_column = TRUE;
        /* also preserve sort order */
        second_sort_order  = (gtk_tree_view_column_get_sort_order (priv->second_sort_column)
                == GTK_SORT_ASCENDING) ? DONNA_SORT_ASC : DONNA_SORT_DESC;
    }

    /* because we'll "re-fill" priv->columns, we can't keep the sort columns set
     * up, as calling set_sort_column() or set_second_sort_column() would risk
     * segfaulting, when get_column_by_column() would return NULL (because the
     * old/current columns aren't in priv->columns anymore).
     * So, we unset them both, so they can be set properly */
    if (priv->second_sort_column)
    {
        gtk_widget_set_visible (get_column_by_column (tree,
                    priv->second_sort_column)->second_arrow,
                FALSE);
        priv->second_sort_column = NULL;
    }
    if (priv->sort_column)
    {
        gtk_tree_view_column_set_sort_indicator (priv->sort_column, FALSE);
        priv->sort_column = NULL;
    }

    list = priv->columns;
    priv->columns = NULL;
    priv->main_column = NULL;

    for (;;)
    {
        struct column     *_col;
        gchar             *col_type;
        gchar             *e;
        gboolean           is_last_col;
        DonnaColumnType   *ct;
        DonnaColumnType   *col_ct;
        guint              width;
        gchar             *title;
        GSList            *l;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        const gchar       *rend;
        gint               index;
        GPtrArray         *props;
        gchar              buf[64];
        gchar             *b = buf;

        e = strchrnul (col, ',');
        is_last_col = (*e == '\0');
        if (!is_last_col)
            *e = '\0';

        if (!donna_config_get_string (config, &col_type, "columns/%s/type", col))
        {
            g_warning ("Treeview '%s': No type defined for column '%s', fallback to its name",
                    priv->name, col);
            col_type = NULL;
        }

        ct = donna_app_get_columntype (priv->app, (col_type) ? col_type : col);
        if (!ct)
        {
            g_critical ("Treeview '%s': Unable to load column-type '%s' for column '%s'",
                    priv->name, (col_type) ? col_type : col, col);
            goto next;
        }

        /* look for an existing column of that type */
        column = NULL;
        for (l = list; l; l = l->next)
        {
            _col = l->data;

            if (_col->ct == ct)
            {
                col_ct = _col->ct;
                column = _col->column;

                /* column has a ref already, we can release ours */
                g_object_unref (ct);
                /* update the name if needed */
                if (!streq (_col->name, col))
                {
                    g_free (_col->name);
                    _col->name = g_strdup (col);
                    donna_columntype_free_data (ct, _col->ct_data);
                    _col->ct_data = NULL;
                    donna_columntype_refresh_data (ct, priv->name, col,
                            arrangement->columns_options, &_col->ct_data);
                }
                else if (must_load_columns_options (arrangement, priv->arrangement, force))
                    /* refresh data to load new options */
                    donna_columntype_refresh_data (ct, priv->name, col,
                            arrangement->columns_options, &_col->ct_data);
                /* move column */
                gtk_tree_view_move_column_after (treev, column, last_column);

                list = g_slist_delete_link (list, l);
                priv->columns = g_slist_prepend (priv->columns, _col);
                break;
            }
        }

        if (!column)
        {
            GtkWidget *btn;
            GtkWidget *hbox;
            GtkWidget *label;
            GtkWidget *arrow;

            _col = g_slice_new0 (struct column);
            _col->tree = tree;
            /* create renderer(s) & column */
            _col->column = column = gtk_tree_view_column_new ();
            _col->name = g_strdup (col);
            /* data for use in render, node_cmp, etc */
            donna_columntype_refresh_data (ct, priv->name, col,
                    arrangement->columns_options, &_col->ct_data);
            /* give our ref on the ct to the column */
            _col->ct = ct;
            /* add to our list of columns (order doesn't matter) */
            priv->columns = g_slist_prepend (priv->columns, _col);
            /* to test for expander column */
            col_ct = ct;
            /* sizing stuff */
            gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
            if (!is_tree (tree))
            {
                gtk_tree_view_column_set_resizable (column, TRUE);
                gtk_tree_view_column_set_reorderable (column, TRUE);
            }
            /* put our internal renderers */
            for (index = 0; index < NB_INTERNAL_RENDERERS; ++index)
            {
                gtk_tree_view_column_set_cell_data_func (column,
                        int_renderers[index],
                        rend_func,
                        GINT_TO_POINTER (index),
                        NULL);
                gtk_tree_view_column_pack_start (column, int_renderers[index],
                        FALSE);
            }
            /* load renderers */
            rend = donna_columntype_get_renderers (ct);
            _col->renderers = g_ptr_array_sized_new (strlen (rend));
            for ( ; *rend; ++index, ++rend)
            {
                GtkCellRenderer **r;
                GtkCellRenderer * (*load_renderer) (void);
                /* TODO: use an external (app-global) renderer loader? */
                switch (*rend)
                {
                    case DONNA_COLUMNTYPE_RENDERER_TEXT:
                        r = &priv->renderers[RENDERER_TEXT];
                        load_renderer = donna_cell_renderer_text_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PIXBUF:
                        r = &priv->renderers[RENDERER_PIXBUF];
                        load_renderer = gtk_cell_renderer_pixbuf_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PROGRESS:
                        r = &priv->renderers[RENDERER_PROGRESS];
                        load_renderer = gtk_cell_renderer_progress_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_COMBO:
                        r = &priv->renderers[RENDERER_COMBO];
                        load_renderer = gtk_cell_renderer_combo_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_TOGGLE:
                        r = &priv->renderers[RENDERER_TOGGLE];
                        load_renderer = gtk_cell_renderer_toggle_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_SPINNER:
                        r = &priv->renderers[RENDERER_SPINNER];
                        load_renderer = gtk_cell_renderer_spinner_new;
                        break;
                    default:
                        g_critical ("Treeview '%s': Unknown renderer type '%c' for column '%s'",
                                priv->name, *rend, col);
                        continue;
                }
                if (!*r)
                {
                    /* FIXME use a weakref instead? */
                    renderer = *r = g_object_ref (load_renderer ());
                    g_object_set_data ((GObject * ) renderer, "renderer-type",
                            GINT_TO_POINTER (*rend));
                    /* an array where we'll store properties that have been set
                     * by the ct, so we can reset them before next use.
                     * See donna_renderer_set() for more */
                    g_object_set_data_full ((GObject *) renderer, "renderer-props",
                            /* 4: random. There probably won't be more than 4
                             * properties per renderer, is a guess */
                            g_ptr_array_new_full (4, g_free),
                            (GDestroyNotify) g_ptr_array_unref);
                }
                else
                    renderer = *r;
                g_ptr_array_add (_col->renderers, renderer);
                gtk_tree_view_column_set_cell_data_func (column, renderer,
                        rend_func, GINT_TO_POINTER (index), NULL);
                gtk_tree_view_column_pack_start (column, renderer, FALSE);
            }
            /* add it (we add now because we can't get the button (to connect)
             * until it's been added to the treev) */
            gtk_tree_view_append_column (treev, column);
            gtk_tree_view_move_column_after (treev, column, last_column);
            /* click on column header stuff -- see
             * column_button_release_event_cb() for more about this */
            btn = gtk_tree_view_column_get_button (column);
            g_signal_connect (btn, "button-press-event",
                    G_CALLBACK (column_button_press_event_cb), _col);
            g_signal_connect (btn, "button-release-event",
                    G_CALLBACK (column_button_release_event_cb), _col);
            /* we handle the header stuff so we can add our own arrow (for
             * second sort) */
            hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            label = gtk_label_new (NULL);
            arrow = gtk_arrow_new (GTK_ARROW_NONE, GTK_SHADOW_IN);
            gtk_style_context_add_class (gtk_widget_get_style_context (arrow),
                    "second-arrow");
            gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
            gtk_box_pack_end (GTK_BOX (hbox), arrow, FALSE, FALSE, 0);
            gtk_tree_view_column_set_widget (column, hbox);
            gtk_widget_show (hbox);
            gtk_widget_show (label);
            /* so we can access/update things */
            _col->label = label;
            _col->second_arrow = arrow;
            /* lastly */
            gtk_tree_view_column_set_clickable (column, TRUE);
        }

        if (!first_column)
            first_column = column;

        if (!expander_column && col_ct == ctname)
            expander_column = column;

        if (!priv->main_column && col_ct == ctname)
            priv->main_column = column;

        /* size */
        if (snprintf (buf, 64, "columntypes/%s", col_type) >= 64)
            b = g_strdup_printf ("columntypes/%s", col_type);
        width = donna_config_get_int_column (config, priv->name, col,
                arrangement->columns_options, b, "width", 230);
        gtk_tree_view_column_set_fixed_width (column, width);
        if (b != buf)
        {
            g_free (b);
            b = buf;
        }

        /* title */
        title = donna_config_get_string_column (config, priv->name, col,
                arrangement->columns_options, NULL, "title", col);
        gtk_tree_view_column_set_title (column, title);
        gtk_label_set_text (GTK_LABEL (_col->label), title);
        g_free (title);

        /* props to watch for refresh */
        props = donna_columntype_get_props (ct, _col->ct_data);
        if (props)
        {
            guint i;

            for (i = 0; i < props->len; ++i)
            {
                struct col_prop cp;

                cp.prop = g_strdup (props->pdata[i]);
                cp.column = column;
                g_array_append_val (priv->col_props, cp);
            }
            g_ptr_array_unref (props);
        }
        else
            g_critical ("Treeview '%s': column '%s' reports no properties to watch for refresh",
                    priv->name, col);

        /* sort -- (see column_button_release_event_cb() for more) */
        _col->sort_id = sort_id;
        gtk_tree_sortable_set_sort_func (sortable, sort_id,
                (GtkTreeIterCompareFunc) sort_func, column, NULL);
        if (sort_column && streq (sort_column, col))
        {
            if (free_sort_column)
            {
                g_free (sort_column);
                free_sort_column = FALSE;
            }
            sort_column = NULL;
            set_sort_column (tree, column, sort_order, TRUE);
        }
        ++sort_id;

        /* second sort order */
        if (second_sort_column && streq (second_sort_column, col))
        {
            if (free_second_sort_column)
            {
                g_free (second_sort_column);
                free_second_sort_column = FALSE;
            }
            second_sort_column = NULL;

            if (second_sort_sticky != DONNA_SECOND_SORT_STICKY_UNKNOWN)
                priv->second_sort_sticky =
                    second_sort_sticky == DONNA_SECOND_SORT_STICKY_ENABLED;

            set_second_sort_column (tree, column, second_sort_order, TRUE);
        }

        last_column = column;

next:
        g_free (col_type);
        if (is_last_col)
            break;
        *e = ',';
        col = e + 1;
    }

    if (!is_tree (tree) && !priv->blank_column)
    {
        /* we add an extra (empty) column, so we can have some
         * free/blank space on the right, instead of having the last
         * column to be used to fill the space and whatnot */
        priv->blank_column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_sizing (priv->blank_column, GTK_TREE_VIEW_COLUMN_FIXED);
        g_object_set (priv->blank_column, "expand", TRUE, NULL);
        gtk_tree_view_insert_column (treev, priv->blank_column, -1);
    }

    /* set expander column */
    gtk_tree_view_set_expander_column (treev,
            (expander_column) ? expander_column : first_column);

    /* ensure we have a main column */
    if (!priv->main_column)
        priv->main_column = first_column;

#ifdef GTK_IS_JJK
    if (priv->select_highlight == SELECT_HIGHLIGHT_COLUMN
            || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE)
        gtk_tree_view_set_select_highlight_column (treev, priv->main_column);
    else if (priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE)
    {
        /* since we only want an underline, we must set the select highlight
         * column to a non-visible one */
        if (is_tree (tree))
        {
            /* tree never uses an empty column on the right, so we store the
             * extra non-visible column used for this */
            if (!priv->blank_column)
            {
                priv->blank_column = gtk_tree_view_column_new ();
                gtk_tree_view_column_set_sizing (priv->blank_column,
                        GTK_TREE_VIEW_COLUMN_FIXED);
                gtk_tree_view_insert_column (treev, priv->blank_column, -1);
            }
            gtk_tree_view_set_select_highlight_column (treev, priv->blank_column);
        }
        else
            /* list: expander_column is always set to a non-visible one */
            gtk_tree_view_set_select_highlight_column (treev, expander_column);
    }
    gtk_tree_view_set_select_row_underline (treev,
            priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE
            || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE);
#endif

    /* failed to set sort order */
    if (free_sort_column)
        g_free (sort_column);
    if (sort_column || !priv->sort_column)
        set_sort_column (tree, first_column, DONNA_SORT_UNKNOWN, TRUE);

    /* failed to set second sort order */
    if (second_sort_column)
    {
        if (free_second_sort_column)
            g_free (second_sort_column);
        set_second_sort_column (tree, first_column, DONNA_SORT_UNKNOWN, TRUE);
    }

    /* remove all columns left unused */
    while (list)
    {
        struct column *_col = list->data;

        /* though we should never try to sort by a sort_id not used by a column,
         * let's make sure if that happens, we just get a warning (instead of
         * dereferencing a pointer pointing nowhere) */
        gtk_tree_sortable_set_sort_func (sortable, sort_id++, no_sort, tree, NULL);
        /* free associated data */
        g_free (_col->name);
        donna_columntype_free_data (_col->ct, _col->ct_data);
        g_object_unref (_col->ct);
#ifndef GTK_IS_JJK
        /* "Fix" a bug in GTK that doesn't take care of the button properly.
         * This is a memory leak (button doesn't get unref-d/finalized), but
         * could also cause a few issues for us:
         * - it could lead to a column header (the button) not having its hover
         *   effect done on the right area, still using info from an old
         *   button. Could look bad, could also make it impossible to click a
         *   column!
         * - On button-release-event the wrong signal handler would get called,
         *   leading to use of a free-d memory, and segfault.
         */
        GtkWidget *btn;
        btn = gtk_tree_view_column_get_button (_col->column);
#endif
        gtk_tree_view_remove_column (treev, _col->column);
#ifndef GTK_IS_JJK
        gtk_widget_unparent (btn);
#endif
        g_slice_free (struct column, _col);
        list = g_slist_delete_link (list, list);
    }
}

static gboolean
select_arrangement_accumulator (GSignalInvocationHint  *hint,
                                GValue                 *return_accu,
                                const GValue           *return_handler,
                                gpointer                data)
{
    DonnaArrangement *arr_accu;
    DonnaArrangement *arr_handler;
    gboolean keep_emission = TRUE;

    arr_accu    = g_value_get_pointer (return_accu);
    arr_handler = g_value_get_pointer (return_handler);

    /* nothing in accu but something in handler, probably the first handler */
    if (!arr_accu && arr_handler)
    {
        g_value_set_pointer (return_accu, arr_handler);
        if (arr_handler->priority == DONNA_ARRANGEMENT_PRIORITY_OVERRIDE)
            keep_emission = FALSE;
    }
    /* something in accu & in handler */
    else if (arr_handler)
    {
        if (arr_handler->priority > arr_accu->priority)
        {
            free_arrangement (arr_accu);
            g_value_set_pointer (return_accu, arr_handler);
            if (arr_handler->priority == DONNA_ARRANGEMENT_PRIORITY_OVERRIDE)
                keep_emission = FALSE;
        }
        else
            free_arrangement (arr_handler);
    }

    return keep_emission;
}

static inline DonnaArrangement *
select_arrangement (DonnaTreeView *tree, DonnaNode *location)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    DonnaArrangement *arr = NULL;
    gchar *s;

    g_debug ("treeview '%s': select arrangement", priv->name);

    /* list only: emit select-arrangement */
    if (!is_tree (tree))
        g_signal_emit (tree, donna_tree_view_signals[SIGNAL_SELECT_ARRANGEMENT], 0,
                priv->name, location, &arr);

    if (!arr)
        arr = g_new0 (DonnaArrangement, 1);

    config = donna_app_peek_config (priv->app);

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS))
        /* try loading our from our own arrangement */
        if (!donna_config_arr_load_columns (config, arr,
                    "treeviews/%s/arrangement", priv->name))
            /* fallback on default for our mode */
            if (!donna_config_arr_load_columns (config, arr,
                        "defaults/arrangements/%s",
                        (is_tree (tree)) ? "tree" : "list"))
            {
                /* if all else fails, use a column "name" */
                arr->columns = g_strdup ("name");
                arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS;
            }

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SORT))
        if (!donna_config_arr_load_sort (config, arr,
                    "treeviews/%s/arrangement", priv->name)
                && !donna_config_arr_load_sort (config, arr,
                    "defaults/arrangements/%s",
                    (is_tree (tree)) ? "tree" : "list"))
        {
            /* we can't find anything, default to first column */
            s = strchr (arr->columns, ',');
            if (s)
                arr->sort_column = g_strndup (arr->columns, s - arr->columns);
            else
                arr->sort_column = g_strdup (arr->columns);
            arr->flags |= DONNA_ARRANGEMENT_HAS_SORT;
        }

    /* Note: even here, this one is optional */
    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT))
        if (!donna_config_arr_load_second_sort (config, arr,
                    "treeviews/%s/arrangement",
                    priv->name))
            donna_config_arr_load_second_sort (config, arr,
                    "defaults/arrangements/%s",
                    (is_tree (tree)) ? "tree" : "list");

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS))
    {
        if (!donna_config_arr_load_columns_options (config, arr,
                    "treeviews/%s/arrangement",
                    priv->name)
                && !donna_config_arr_load_columns_options (config, arr,
                    "defaults/arrangements/%s",
                    (is_tree (tree)) ? "tree" : "list"))
            /* else: we say we have something, it is NULL. This will force
             * updating the columntype-data without using an arr_name */
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS;
    }

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
    {
        if (!donna_config_arr_load_color_filters (config, priv->app, arr,
                    "treeviews/%s/arrangement", priv->name))
            donna_config_arr_load_color_filters (config, priv->app, arr,
                    "defaults/arrangements/%s",
                    (is_tree (tree)) ? "tree" : "list");

        /* special: color filters might have been loaded with a type COMBINE,
         * which resulted in them loaded but no flag set (in order to keep
         * loading others from other arrangements). In such a case, we now need
         * to set the flag */
        if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS)
                && arr->color_filters)
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLOR_FILTERS;
    }

    return arr;
}

void
donna_tree_view_build_arrangement (DonnaTreeView *tree, gboolean force)
{
    DonnaTreeViewPrivate *priv;
    DonnaArrangement *arr = NULL;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));

    priv = tree->priv;
    g_debug ("treeview '%s': build arrangement (force=%d)",
            priv->name, force);

    arr = select_arrangement (tree, priv->location);

    if (must_load_columns (arr, priv->arrangement, force))
        load_arrangement (tree, arr, force);
    else
    {
        DonnaConfig *config;
        GSList *l;
        gboolean need_sort;
        gboolean need_second_sort;
        gboolean need_columns_options;

        config = donna_app_peek_config (priv->app);
        need_sort = must_load_sort (arr, priv->arrangement, force);
        need_second_sort = must_load_second_sort (arr, priv->arrangement, force);
        need_columns_options = must_load_columns_options (arr, priv->arrangement, force);

        for (l = priv->columns; l; l = l->next)
        {
            struct column *_col = l->data;

            if (need_sort && streq (_col->name, arr->sort_column))
            {
                set_sort_column (tree, _col->column, arr->sort_order, TRUE);
                need_sort = FALSE;
            }
            if (need_second_sort && streq (_col->name, arr->second_sort_column))
            {
                set_second_sort_column (tree, _col->column,
                        arr->second_sort_order, TRUE);
                if (arr->second_sort_sticky != DONNA_SECOND_SORT_STICKY_UNKNOWN)
                    priv->second_sort_sticky =
                        arr->second_sort_sticky == DONNA_SECOND_SORT_STICKY_ENABLED;
                need_second_sort = FALSE;
            }
            if (!need_sort && !need_second_sort && !need_columns_options)
                break;

            if (need_columns_options)
            {
                gchar buf[64], *b = buf;
                guint width;
                gchar *title;

                /* ctdata */
                donna_columntype_refresh_data (_col->ct, priv->name, _col->name,
                        arr->columns_options, &_col->ct_data);

                /* size */
                if (snprintf (buf, 64, "columntypes/%s",
                            donna_columntype_get_name (_col->ct)) >= 64)
                    b = g_strdup_printf ("columntypes/%s",
                            donna_columntype_get_renderers (_col->ct));
                width = donna_config_get_int_column (config,
                        priv->name, _col->name, arr->columns_options, b,
                        "width", 230);
                gtk_tree_view_column_set_fixed_width (_col->column, width);
                if (b != buf)
                {
                    g_free (b);
                    b = buf;
                }

                /* title */
                title = donna_config_get_string_column (config,
                        priv->name, _col->name, arr->columns_options, NULL,
                        "title", _col->name);
                gtk_tree_view_column_set_title (_col->column, title);
                gtk_label_set_text (GTK_LABEL (_col->label), title);
                g_free (title);
            }
        }
    }

    free_arrangement (priv->arrangement);
    priv->arrangement = arr;
}

struct set_node_prop_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *prop;
};

static void
free_set_node_prop_data (struct set_node_prop_data *data)
{
    g_free (data->prop);
    g_free (data);
}

static struct active_spinners *
get_as_for_node (DonnaTreeView  *tree,
                 DonnaNode      *node,
                 guint          *index,
                 gboolean        create)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct active_spinners *as;
    guint i;

    /* is there already an as for this node? */
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        as = priv->active_spinners->pdata[i];
        if (as->node == node)
            break;
    }

    if (i >= priv->active_spinners->len)
    {
        if (create)
        {
            as = g_new0 (struct active_spinners, 1);
            as->node = g_object_ref (node);
            as->as_cols = g_array_new (FALSE, FALSE, sizeof (struct as_col));

            g_ptr_array_add (priv->active_spinners, as);
        }
        else
            as = NULL;
    }

    if (index)
        *index = i;

    return as;
}

static void
set_node_prop_callbak (DonnaTask                 *task,
                       gboolean                   timeout_called,
                       struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean task_failed;
    guint i;
    GPtrArray *arr;

    task_failed = donna_task_get_state (task) == DONNA_TASK_FAILED;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (data->prop, cp->prop))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        if (task_failed)
        {
            const GError *error;
            gchar *location;

            error = donna_task_get_error (task);
            location = donna_node_get_location (data->node);
            donna_app_show_error (priv->app, error,
                    "Setting property %s on '%s:%s' failed",
                    data->prop,
                    donna_node_get_domain (data->node),
                    location);
            g_free (location);
        }

        g_ptr_array_free (arr, TRUE);
        free_set_node_prop_data (data);
        return;
    }

    /* timeout called == spinners set; task failed == error message */
    if (timeout_called || task_failed)
    {
        struct active_spinners *as;
        guint as_idx; /* in case we need to remove this as */
        gboolean refresh = FALSE;

        as = get_as_for_node (data->tree, data->node, &as_idx, task_failed);
        if (!as)
            goto free;

        /* for every column using that property */
        for (i = 0; i < arr->len; ++i)
        {
            GtkTreeViewColumn *column = arr->pdata[i];
            struct as_col *as_col;
            guint j;

            /* does this as have a spinner for this column? */
            for (j = 0; j < as->as_cols->len; ++j)
            {
                as_col = &g_array_index (as->as_cols, struct as_col, j);
                if (as_col->column == column)
                    break;
            }
            if (j >= as->as_cols->len)
            {
                if (task_failed)
                {
                    struct as_col as_col_new;

                    as_col_new.column = column;
                    /* no as_col means no timeout called, so we can safely set
                     * nb to 0 */
                    as_col_new.nb = 0;
                    as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
                    g_ptr_array_add (as_col_new.tasks, g_object_ref (task));
                    g_array_append_val (as->as_cols, as_col_new);
                    as_col = &g_array_index (as->as_cols, struct as_col, j);
                }
                else
                    continue;
            }
            else if (!timeout_called) /* implies task_failed */
                g_ptr_array_add (as_col->tasks, g_object_ref (task));

            if (!task_failed)
                g_ptr_array_remove_fast (as_col->tasks, task);

            if (timeout_called)
                --as_col->nb;

            if (as_col->nb == 0)
            {
                refresh = TRUE;
#ifndef GTK_IS_JJK
                if (task_failed)
                    /* a bug in GTK means that because when the size of renderer
                     * is first computed and renderer if not visible, it has a
                     * natural size of 0 and therefore even when it becomes
                     * visible it isn't actually drawn.
                     * This is a hack to workaround this, by enforcing the
                     * column to re-compute its size now that we'll have the
                     * renderer visible, so it should have a natural size and
                     * actually be drawn */
                    gtk_tree_view_column_queue_resize (column);
#endif
                /* can we remove this as_col? */
                if (as_col->tasks->len == 0)
                {
                    /* can we remove the whole as? */
                    if (as->as_cols->len == 1)
                        g_ptr_array_remove_index_fast (priv->active_spinners,
                                as_idx);
                    else
                        g_array_remove_index_fast (as->as_cols, j);
                }
            }
        }

        if (refresh)
        {
            GtkTreeModel *model;
            GSList *list;

            model = GTK_TREE_MODEL (priv->store);
            /* for every row of this node */
            list = g_hash_table_lookup (priv->hashtable, data->node);
            for ( ; list; list = list->next)
            {
                GtkTreeIter *iter = list->data;
                GtkTreePath *path;

                /* make sure a redraw will be done for this row, else the
                 * last spinner frame stays there until a redraw happens */

                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }

        /* no more as == we can stop spinner_fn. If there's still one (or more)
         * but only for error messages, on its next call spinner_fn will see it
         * and stop itself */
        if (!priv->active_spinners->len && priv->active_spinners_id)
        {
            g_source_remove (priv->active_spinners_id);
            priv->active_spinners_id = 0;
            priv->active_spinners_pulse = 0;
        }
    }

free:
    g_ptr_array_free (arr, TRUE);
    free_set_node_prop_data (data);
}

static void
set_node_prop_timeout (DonnaTask *task, struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GtkTreeModel *model;
    GSList *list;
    guint i;
    GPtrArray *arr;
    struct active_spinners *as;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (data->prop, cp->prop))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        g_ptr_array_free (arr, TRUE);
        return;
    }

    as = get_as_for_node (data->tree, data->node, NULL, TRUE);
    /* for every column using that property */
    for (i = 0; i < arr->len; ++i)
    {
        GtkTreeViewColumn *column = arr->pdata[i];
        struct as_col *as_col;
        guint j;

        /* does this as already have a spinner for this column? */
        for (j = 0; j < as->as_cols->len; ++j)
        {
            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->column == column)
                break;
        }
        if (j >= as->as_cols->len)
        {
            struct as_col as_col_new;

            as_col_new.column = column;
            as_col_new.nb = 1;
            as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
            g_array_append_val (as->as_cols, as_col_new);
            as_col = &g_array_index (as->as_cols, struct as_col, j);

#ifndef GTK_IS_JJK
            /* a bug in GTK means that because when the size of renderer is
             * first computed and spinner if not visible, it has a natural
             * size of 0 and therefore even when it becomes visible it isn't
             * actually drawn.
             * This is a hack to workaround this, by enforcing the column to
             * re-compute its size now that we'll have the spinner visible,
             * so it should have a natural size and actually be drawn */
            gtk_tree_view_column_queue_resize (column);
#endif
        }
        else
            ++as_col->nb;

        g_ptr_array_add (as_col->tasks, g_object_ref (task));
    }

#ifdef GTK_IS_JJK
    model = GTK_TREE_MODEL (priv->store);
    /* for every row of this node */
    list = g_hash_table_lookup (priv->hashtable, data->node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *iter = list->data;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
#endif

    if (!priv->active_spinners_id)
        priv->active_spinners_id = g_timeout_add (42,
                (GSourceFunc) spinner_fn, data->tree);

    g_ptr_array_free (arr, TRUE);
}

gboolean
donna_tree_view_set_node_property (DonnaTreeView      *tree,
                                   DonnaNode          *node,
                                   const gchar        *prop,
                                   const GValue       *value,
                                   GError           **error)
{
    DonnaTreeViewPrivate *priv;
    GError *err = NULL;
    GSList *list;
    DonnaTask *task;
    struct set_node_prop_data *data;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (prop != NULL, FALSE);
    g_return_val_if_fail (G_IS_VALUE (value), FALSE);

    priv = tree->priv;

    /* make sure the node is on the tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
    {
        gchar *location = donna_node_get_location (node);
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "Treeview '%s': Cannot set property '%s' on node '%s:%s', "
                "the node is not represented in the treeview",
                priv->name,
                prop,
                donna_node_get_domain (node),
                location);
        g_free (location);
        return FALSE;
    }

    task = donna_node_set_property_task (node, prop, value, &err);
    if (!task)
    {
        gchar *fl = donna_node_get_full_location (node);
        if (err)
        g_propagate_prefixed_error (error, err,
                "Treeview '%s': Cannot set property '%s' on node '%s': ",
                priv->name, prop, fl);
        else
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "Treeview '%s': Failed to create task to set property '%s' on node '%s'",
                    priv->name, prop, fl);
        g_free (fl);
        return FALSE;
    }

    data = g_new0 (struct set_node_prop_data, 1);
    data->tree = tree;
    /* don't need to take a ref on node for timeout or cb, since task has one */
    data->node = node;
    data->prop = g_strdup (prop);

    donna_task_set_timeout (task, 800 /* FIXME an option */,
            (task_timeout_fn) set_node_prop_timeout,
            data,
            NULL);
    donna_task_set_callback (task,
            (task_callback_fn) set_node_prop_callbak,
            data,
            (GDestroyNotify) free_set_node_prop_data);
    donna_app_run_task (priv->app, task);
    return TRUE;
}

static DonnaTreeRow *
get_row_for_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    GSList *l;

    gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);
    g_object_unref (node);
    for (l = g_hash_table_lookup (priv->hashtable, node); l; l = l->next)
        if (itereq (iter, (GtkTreeIter *) l->data))
        {
            DonnaTreeRow *row;

            row = g_new (DonnaTreeRow, 1);
            row->node = node;
            row->iter = l->data;
            return row;
        }
    g_return_val_if_reached (NULL);
}

/* mode tree only */
static GtkTreeIter *
get_root_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter root;
    DonnaNode *node;
    GSList *list;

    if (iter->stamp == 0)
        return NULL;

    if (donna_tree_store_iter_depth (priv->store, iter) > 0)
    {
        gchar *str;

        str = gtk_tree_model_get_string_from_iter (model, iter);
        /* there is at least one ':' since it's not a root */
        *strchr (str, ':') = '\0';
        gtk_tree_model_get_iter_from_string (model, &root, str);
        g_free (str);
    }
    else
        /* current location is a root */
        root = *iter;

    /* get the iter from the hashtable */
    gtk_tree_model_get (model, &root, DONNA_TREE_COL_NODE, &node, -1);
    list = g_hash_table_lookup (priv->hashtable, node);
    g_object_unref (node);
    for ( ; list; list = list->next)
        if (itereq (&root, (GtkTreeIter *) list->data))
            return list->data;
    return NULL;
}

/* mode tree only */
static inline GtkTreeIter *
get_current_root_iter (DonnaTreeView *tree)
{
    return get_root_iter (tree, &tree->priv->location_iter);
}

/* mode tree only */
static gboolean
is_row_accessible (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GtkTreeIter parent;
    GtkTreeIter child;
    GtkTreePath *path;

    child = *iter;
    while (gtk_tree_model_iter_parent (model, &parent, &child))
    {
        gboolean is_expanded;

        path = gtk_tree_model_get_path (model, &parent);
        is_expanded = gtk_tree_view_row_expanded (treev, path);
        gtk_tree_path_free (path);
        if (!is_expanded)
            return FALSE;
        /* go up */
        child = parent;
    }
    return TRUE;
}

/* mode tree only */
/* return the best iter for the given node. Iter must exists on tree, and must
 * be expanded unless even_collapsed is TRUE.
 * This is how we get the new current location in DONNA_TREE_SYNC_NODES and
 * DONNA_TREE_SYNC_NODES_KNOWN_CHILDREN */
static GtkTreeIter *
get_best_existing_iter_for_node (DonnaTreeView  *tree,
                                 DonnaNode      *node,
                                 gboolean        even_collapsed)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev;
    GSList *list;
    GtkTreeModel *model;
    GtkTreeIter *iter_cur_root;
    GtkTreeIter *iter_vis = NULL;
    GtkTreeIter *iter_non_vis = NULL;
    GdkRectangle rect_visible;

    /* we only want iters on tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
        return NULL;

    treev = GTK_TREE_VIEW (tree);
    model = GTK_TREE_MODEL (priv->store);

    /* just the one? */
    if (!list->next)
    {
        if (even_collapsed || is_row_accessible (tree, list->data))
            return list->data;
        return NULL;
    }

    iter_cur_root = get_current_root_iter (tree);
    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev, 0, rect_visible.y,
            &rect_visible.x, &rect_visible.y);

    for ( ; list; list = list->next)
    {
        GtkTreeIter *iter = list->data;
        GdkRectangle rect;

        /* not "accessible" w/out expanding, we skip */
        if (!even_collapsed && !is_row_accessible (tree, iter))
            continue;

        /* if in the current location's root branch, it's the one */
        if (iter_cur_root && (itereq (iter_cur_root, iter)
                    || donna_tree_store_is_ancestor (priv->store,
                        iter_cur_root, iter)))
            return iter;

        /* if we haven't found a visible match yet... */
        if (!iter_vis)
        {
            GtkTreePath *path;

            /* determine if it is visible or not */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_get_background_area (treev, path, NULL, &rect);
            gtk_tree_path_free (path);
            if (rect.y >= rect_visible.y
                    && rect.y + rect.height <= rect_visible.y + rect_visible.height)
                iter_vis = iter;
            else if (!iter_non_vis)
                iter_non_vis = iter;
        }
    }

    return (iter_vis) ? iter_vis : iter_non_vis;
}

/* mode tree only -- node must be in a non-flat domain */
static gboolean
is_node_ancestor (DonnaNode         *node,
                  DonnaNode         *descendant,
                  DonnaProvider     *descendant_provider,
                  const gchar       *descendant_location)
{
    gchar *location;
    size_t len;
    gboolean ret;

    if (descendant_provider != donna_node_peek_provider (node))
        return FALSE;

    /* descandant is in the same domain as node, and we know node's domain isn't
     * flat, so we can assume that if descendant is a child, its location starts
     * with its parent's location and a slash */
    location = donna_node_get_location (node);
    len = strlen (location);
    ret = streq (location, "/") /* node is the root */
        || (streqn (location, descendant_location, len)
                && descendant_location[len] == '/');
    g_free (location);
    return ret;
}

/* mode tree only -- node must have its iter ending up under iter_root, and must
 * be in a non-flat domain */
/* get an iter (under iter_root) for the node. If only_accessible we don't want
 * any collapsed row, but the first accessible one. We can then provider the
 * address of a gboolean that will indicate of the iter is for the node asked,
 * or just the closest accessible ancestor. */
static GtkTreeIter *
get_iter_expanding_if_needed (DonnaTreeView *tree,
                              GtkTreeIter   *iter_root,
                              DonnaNode     *node,
                              gboolean       only_accessible,
                              gboolean      *was_match)
{
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GtkTreeIter *last_iter = NULL;
    GtkTreeIter *iter;
    DonnaProvider *provider;
    DonnaNode *n;
    gchar *location;
    size_t len;
    gchar *s;
    gchar *ss;
    DonnaTask *task;
    const GValue *value;

    model = GTK_TREE_MODEL (priv->store);
    iter = iter_root;
    provider = donna_node_peek_provider (node);
    location = donna_node_get_location (node);
    if (was_match)
        *was_match = FALSE;

    /* get the node for the given iter_root, our starting point */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_NODE,    &n,
            -1);
    for (;;)
    {
        GtkTreeIter *prev_iter;
        GtkTreePath *path;
        enum tree_expand es;

        if (n == node)
        {
            /* this _is_ the iter we're looking for */
            g_free (location);
            g_object_unref (n);
            if (was_match)
                *was_match = TRUE;
            return iter;
        }

        /* get the node's location, and obtain the location of the next child */
        ss = donna_node_get_location (n);
        len = strlen (ss);
        g_free (ss);
        g_object_unref (n);
        s = strchr (location + len + 1, '/');
        if (s)
            s = g_strndup (location, s - location);
        else
            s = (gchar *) location;

        /* get the corresponding node */
        task = donna_provider_get_node_task (provider, (const gchar *) s, NULL);
        if (s != location)
            g_free (s);
        g_object_ref_sink (task);
        /* FIXME? should this be in a separate thread, and continue in a
         * callback and all that? might not be worth the trouble... */
        donna_task_run (task);
        if (donna_task_get_state (task) != DONNA_TASK_DONE)
        {
            /* TODO */
            g_object_unref (task);
            g_free (location);
            return NULL;
        }
        value = donna_task_get_return_value (task);
        n = g_value_dup_object (value);
        g_object_unref (task);

        if (only_accessible)
        {
            /* we only remember this iter as last possible match if expanded */
            if (is_row_accessible (tree, iter))
                last_iter = iter;
        }
        else
            last_iter = iter;

        /* now get the child iter for that node */
        prev_iter = iter;
        iter = get_child_iter_for_node (tree, prev_iter, n);
        if (!iter)
        {
            if (!only_accessible)
            {
                GSList *list;

                /* we need to add a new row */
                if (!add_node_to_tree (tree, prev_iter, n,
                            &priv->future_location_iter))
                {
                    /* TODO */
                    g_object_unref (n);
                    g_free (location);
                    return NULL;
                }

                /* get the iter from the hashtable for the row we added (we
                 * cannot end up return the pointer to a local iter) */
                list = g_hash_table_lookup (priv->hashtable, n);
                for ( ; list; list = list->next)
                    if (itereq (&priv->future_location_iter,
                                (GtkTreeIter *) list->data))
                    {
                        iter = list->data;
                        break;
                    }
            }
            else
            {
                g_object_unref (n);
                g_free (location);
                return last_iter;
            }
        }
        else if (only_accessible &&
                (!donna_tree_store_iter_is_visible (priv->store, iter)
                 || !is_row_accessible (tree, iter)))
        {
            g_object_unref (n);
            g_free (location);
            return last_iter;
        }
        else if (!donna_tree_store_iter_is_visible (priv->store, iter))
        {
            /* update our future location, and ensure row's visibility */
            priv->future_location_iter = *iter;
            donna_tree_store_refresh_visibility (priv->store, iter, NULL);
        }

        /* check if the parent (prev_iter) is expanded */
        path = gtk_tree_model_get_path (model, prev_iter);
        if (!gtk_tree_view_row_expanded (treev, path))
        {
            gtk_tree_model_get (model, prev_iter,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
            if (es == DONNA_TREE_EXPAND_MAXI
                    || es == DONNA_TREE_EXPAND_PARTIAL)
                gtk_tree_view_expand_row (treev, path, FALSE);
            else
            {
                es = (priv->is_minitree)
                    ? DONNA_TREE_EXPAND_PARTIAL : DONNA_TREE_EXPAND_UNKNOWN;
                set_es (priv, prev_iter, es);

                if (priv->is_minitree)
                    gtk_tree_view_expand_row (treev, path, FALSE);
                else
                {
                    /* this will take care of the import/get-children, we'll
                     * scroll (if sync_scroll) to make sure to scroll to current
                     * once children are added */
                    expand_row (tree, prev_iter, priv->sync_scroll, NULL);
                    /* now that the thread is started, we need to trigger it
                     * again, so the row actually gets expanded this time, which
                     * we require to be able to continue adding children &
                     * expanding them */
                    gtk_tree_view_expand_row (treev, path, FALSE);
                }
            }
        }
        gtk_tree_path_free (path);
    }
}

static GtkTreeIter *
get_closest_iter_for_node (DonnaTreeView *tree,
                           DonnaNode     *node,
                           DonnaProvider *provider,
                           const gchar   *location,
                           GtkTreeIter   *skip_root,
                           gboolean      *is_match)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;
    GtkTreeIter iter;
    DonnaNode *n;
    GdkRectangle rect_visible;
    GdkRectangle rect;
    GtkTreeIter *last_iter = NULL;
    guint last_match = 0;
#define LM_MATCH    (1 << 0)
#define LM_VISIBLE  (1 << 1)

    model  = (GtkTreeModel *) priv->store;

    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev,
            0, rect_visible.y, &rect_visible.x, &rect_visible.y);

    /* try all existing tree roots (if any) */
    if (gtk_tree_model_iter_children (model, &iter, NULL))
        do
        {
            /* we might have a root to skip (likely the current one, already
             * processed before calling this */
            if (skip_root && itereq (&iter, skip_root))
                continue;

            gtk_tree_model_get (model, &iter, DONNA_TREE_COL_NODE, &n, -1);
            if (n == node || is_node_ancestor (n, node, provider, location))
            {
                GSList *list;
                GtkTreeIter *i;
                gboolean match;

                /* get the iter from the hashtable (we cannot end up return the
                 * pointer to a local iter) */
                list = g_hash_table_lookup (priv->hashtable, n);
                for ( ; list; list = list->next)
                    if (itereq (&iter, (GtkTreeIter *) list->data))
                    {
                        i = list->data;
                        break;
                    }

                /* find the closest "accessible" iter */
                i = get_iter_expanding_if_needed (tree, i, node, TRUE, &match);
                if (i)
                {
                    GtkTreePath *path;

                    /* determine if it is visible or not */
                    path = gtk_tree_model_get_path (model, i);
                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    gtk_tree_path_free (path);
                    if (rect.y >= rect_visible.y
                            && rect.y + rect.height <= rect_visible.y +
                            rect_visible.height)
                    {
                        if (match)
                        {
                            if (is_match)
                                *is_match = match;
                            return get_iter_expanding_if_needed (tree, i, node,
                                    FALSE, NULL);
                        }
                        else if ((last_match & (LM_MATCH | LM_VISIBLE)) == 0)
                        {
                            last_match = LM_VISIBLE;
                            last_iter = i;
                        }
                    }
                    else
                    {
                        if (match)
                        {
                            if (!(last_match & LM_MATCH))
                            {
                                last_match = LM_MATCH;
                                last_iter = i;
                            }
                        }
                        else if (!last_iter)
                        {
                            last_match = 0;
                            last_iter = i;
                        }
                    }
                }
            }
        }
        while (gtk_tree_model_iter_next (model, &iter));

    if (is_match)
        *is_match = (last_match & LM_MATCH) ? TRUE : FALSE;
    return last_iter;
}

/* mode tree only */
/* this will get the best iter for new location in
 * DONNA_TREE_SYNC_NODES_CHILDREN, as well as DONNA_TREE_SYNC_FULL with
 * add_root_if_needed set to TRUE */
static GtkTreeIter *
get_best_iter_for_node (DonnaTreeView   *tree,
                        DonnaNode       *node,
                        gboolean         add_root_if_needed,
                        GError         **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    DonnaProvider *provider;
    DonnaProviderFlags flags;
    gchar *location;
    GtkTreeIter *iter_cur_root;
    DonnaNode *n;
    GtkTreeIter *last_iter = NULL;
    gboolean match;

    provider = donna_node_peek_provider (node);
    flags = donna_provider_get_flags (provider);
    if (G_UNLIKELY (flags & DONNA_PROVIDER_FLAG_INVALID))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Unable to get flags for provider '%s'",
                priv->name,
                donna_provider_get_domain (provider));
        return NULL;
    }
    /* w/ flat provider we can't do anything else but rely on existing rows */
    else if (flags & DONNA_PROVIDER_FLAG_FLAT)
        /* TRUE not to ignore non-"accesible" (collapsed) ones */
        return get_best_existing_iter_for_node (tree, node, TRUE);

    model  = GTK_TREE_MODEL (priv->store);
    location = donna_node_get_location (node);

    /* try inside the current branch first */
    iter_cur_root = get_current_root_iter (tree);
    if (iter_cur_root)
    {
        gtk_tree_model_get (model, iter_cur_root, DONNA_TREE_COL_NODE, &n, -1);
        if (n == node || is_node_ancestor (n, node, provider, location))
        {
            g_free (location);
            g_object_unref (n);
            return get_iter_expanding_if_needed (tree, iter_cur_root, node,
                    FALSE, NULL);
        }
    }

    last_iter = get_closest_iter_for_node (tree, node,
            provider, location, iter_cur_root, &match);
    if (last_iter)
    {
        g_free (location);
        if (match)
            return last_iter;
        else
            return get_iter_expanding_if_needed (tree, last_iter, node,
                    FALSE, NULL);
    }
    else if (add_root_if_needed)
    {
        DonnaTask *task;
        const GValue *value;
        gchar *s;
        GSList *list;
        GtkTreeIter *i;

        /* the tree is empty, we need to add the first root */
        s = strchr (location, '/');
        if (s)
            *++s = '\0';

        task = donna_provider_get_node_task (provider, location, NULL);
        g_object_ref_sink (task);
        donna_task_run (task);
        if (donna_task_get_state (task) != DONNA_TASK_DONE)
        {
            g_object_unref (task);
            g_free (location);
            /* FIXME set error */
            return NULL;
        }
        value = donna_task_get_return_value (task);
        n = g_value_dup_object (value);
        g_object_unref (task);
        g_free (location);

        add_node_to_tree (tree, NULL, n, &priv->future_location_iter);
        /* first root added, so we might need to load an arrangement */
        if (!priv->arrangement)
            donna_tree_view_build_arrangement (tree, FALSE);
        /* get the iter from the hashtable for the row we added (we
         * cannot end up return the pointer to a local iter) */
        list = g_hash_table_lookup (priv->hashtable, n);
        for ( ; list; list = list->next)
            if (itereq (&priv->future_location_iter, (GtkTreeIter *) list->data))
            {
                i = list->data;
                break;
            }

        g_object_unref (n);
        return get_iter_expanding_if_needed (tree, i, node,
                FALSE, NULL);
    }
    else
    {
        g_free (location);
        return NULL;
    }
}

static inline void
scroll_to_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    GtkTreeView *treev = (GtkTreeView *) tree;
    GdkRectangle rect_visible, rect;
    GtkTreePath *path;

    /* get visible area, so we can determine if it is already visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);

    path = gtk_tree_model_get_path ((GtkTreeModel *) tree->priv->store, iter);
    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
    if (rect.y < 0 || rect.y > rect_visible.height - rect.height)
        /* only scroll if not visible */
        gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.5, 0.0);

    gtk_tree_path_free (path);
}

/* mode tree only */
static gboolean
scroll_to_current (DonnaTreeView *tree)
{
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected (
                gtk_tree_view_get_selection (GTK_TREE_VIEW (tree)),
                NULL,
                &iter))
        return FALSE;

    scroll_to_iter (tree, &iter);
    return FALSE;
}

struct node_get_children_list_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
    DonnaNode     *child; /* item to scroll to & select */
};

static inline void
free_node_get_children_list_data (struct node_get_children_list_data *data)
{
    g_object_unref (data->node);
    if (data->child)
        g_object_unref (data->child);
    g_slice_free (struct node_get_children_list_data, data);
}

/* mode list only */
static void
node_get_children_list_timeout (DonnaTask                           *task,
                                struct node_get_children_list_data  *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    /* is this still valid (or did the user click away already) ? */
    if (data->node)
    {
        if (priv->future_location != data->node)
            return;
    }
    else if (priv->future_location != data->child)
        return;

    /* clear the list */
    donna_tree_store_clear (priv->store);
    /* also the hashtable (we don't need to unref nodes (keys), as our ref was
     * handled by the store) */
    g_hash_table_remove_all (priv->hashtable);
    /* and show the "please wait" message */
    priv->draw_state = DRAW_WAIT;
    gtk_widget_queue_draw (GTK_WIDGET (data->tree));
}

/* mode list only */
static void
node_get_children_list_cb (DonnaTask                            *task,
                           gboolean                              timeout_called,
                           struct node_get_children_list_data   *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GtkStyleContext *context;
    gchar buf[64];
    const gchar *domain;
    gboolean changed_location;
    GtkTreeIter iter, *it = &iter;
    const GValue *value;
    GPtrArray *arr;
    guint i;
    DonnaProvider *provider;
    DonnaProvider *provider_location;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        gchar             *location;
        const GError      *error;

        error = donna_task_get_error (task);
        location = donna_node_get_location (data->node);
        donna_app_show_error (priv->app, error,
                "Treeview '%s': Failed to get children for node '%s:%s'",
                priv->name,
                donna_node_get_domain (data->node),
                location);
        g_free (location);

        if (priv->future_location == data->node)
        {
            priv->future_location = NULL;

            /* since we couldn't go there, make sure our real current location
             * is known (e.g. when user clicked on tree, it should go back to
             * where we are, since we failed to change location) */
            g_object_notify_by_pspec ((GObject *) data->tree,
                    donna_tree_view_props[PROP_LOCATION]);
        }

        goto free;
    }

    /* is this still valid (or did the user click away already) ? */
    if (priv->future_location != data->node)
        goto free;

    changed_location = priv->location != priv->future_location;
    /* clear the list */
    donna_tree_store_clear (priv->store);
    /* also the hashtable (we don't need to unref nodes (keys), as our ref was
     * handled by the store) */
    g_hash_table_remove_all (priv->hashtable);

    context = gtk_widget_get_style_context ((GtkWidget *) data->tree);
    /* update current location (now because we need this done to build
     * arrangement) */
    if (priv->location)
    {
        provider_location = donna_node_get_provider (priv->location);
        domain = donna_node_get_domain (priv->location);
        /* 56 == 64 (buf) - 8 (strlen ("domain-") + NUL) */
        if (strlen (domain) <= 56)
        {
            strcpy (buf, "domain-");
            strcpy (buf + 7, domain);
            gtk_style_context_remove_class (context, buf);
        }
        else
        {
            gchar *b = g_strdup_printf ("domain-%s", domain);
            gtk_style_context_remove_class (context, buf);
            g_free (b);
        }
        g_object_unref (priv->location);
    }
    else
        provider_location = NULL;
    priv->location = g_object_ref (data->node);
    domain = donna_node_get_domain (priv->location);
    /* 56 == 64 (buf) - 8 (strlen ("domain-") + NUL) */
    if (strlen (domain) <= 56)
    {
        strcpy (buf, "domain-");
        strcpy (buf + 7, domain);
        gtk_style_context_add_class (context, buf);
    }
    else
    {
        gchar *b = g_strdup_printf ("domain-%s", domain);
        gtk_style_context_add_class (context, buf);
        g_free (b);
    }
    /* we're there */
    priv->future_location = NULL;
    /* update arrangement for new location if needed */
    donna_tree_view_build_arrangement (data->tree, FALSE);

    value = donna_task_get_return_value (task);
    arr = g_value_get_boxed (value);
    if (arr->len > 0)
    {
        GtkTreeSortable *sortable = (GtkTreeSortable *) data->tree->priv->store;
        gint sort_col_id;
        GtkSortType order;
        GtkWidget *w;

        priv->draw_state = DRAW_NOTHING;
        iter.stamp = 0;

        /* adding items to a sorted store is quite slow; we get much better
         * performance by adding all items to an unsorted store, and then
         * sorting it */
        gtk_tree_sortable_get_sort_column_id (sortable, &sort_col_id, &order);
        gtk_tree_sortable_set_sort_column_id (sortable,
                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);

        for (i = 0; i < arr->len; ++i)
        {
            add_node_to_tree (data->tree, NULL, arr->pdata[i], it);
            if (data->child == arr->pdata[i])
                /* don't change iter no more */
                it = NULL;
        }

        gtk_tree_sortable_set_sort_column_id (sortable, sort_col_id, order);

        /* in order to scroll properly, we need to have the tree sorted &
         * everything done; i.e. we need to have all pending events processed.
         * Note: here this should be fine, as there shouldn't be any pending
         * events updating the list. See sync_with_location_changed_cb for more
         * about this. */
        while (gtk_events_pending ())
            gtk_main_iteration ();

        /* do we have a child to focus/scroll to? */
        if (!it && iter.stamp != 0)
        {
            GtkTreePath *path;

            /* we bring the row into view as needed, then focus it only. If not
             * patched, we can just set_cursor() & then unselect, since we know
             * there are no selection */

            path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
            if (changed_location)
                scroll_to_iter (data->tree, &iter);
            else
                gtk_tree_view_scroll_to_cell ((GtkTreeView *) data->tree, path,
                        NULL, FALSE, 0.0, 0.0);
#ifdef GTK_IS_JJK
            gtk_tree_view_set_focused_row ((GtkTreeView *) data->tree, path);
#else
            gtk_tree_view_set_cursor ((GtkTreeView *) data->tree, path, NULL, FALSE);
            gtk_tree_selection_unselect_all (
                    gtk_tree_view_get_selection ((GtkTreeView *) data->tree));
#endif
            gtk_tree_path_free (path);
        }
        else
            /* scroll to top-left */
            gtk_tree_view_scroll_to_point ((GtkTreeView *) data->tree, 0, 0);

        /* we give the treeview the focus, to ensure the focused row is set,
         * hence the class focused-row applied */
        w = gtk_widget_get_toplevel ((GtkWidget *) data->tree);
        w = gtk_window_get_focus ((GtkWindow *) w);
        gtk_widget_grab_focus ((GtkWidget *) data->tree);
        gtk_widget_grab_focus ((w) ? w : (GtkWidget *) data->tree);
    }
    else
    {
        /* show the "location empty" message */
        priv->draw_state = DRAW_EMPTY;
        gtk_widget_queue_draw ((GtkWidget *) data->tree);
    }

    /* connect to provider's signals of current location (if needed) */
    provider = donna_node_peek_provider (priv->location);
    if (provider != provider_location)
    {
        struct provider_signals *ps;
        gint done = (provider_location) ? 0 : 1;
        gint found = -1;

        for (i = 0; i < priv->providers->len; ++i)
        {
            ps = priv->providers->pdata[i];

            if (ps->provider == provider)
            {
                found = i;
                ps->nb_nodes++;
                ++done;
            }
            else if (ps->provider == provider_location)
            {
                if (--ps->nb_nodes == 0)
                    /* this will disconnect handlers from provider & free memory */
                    g_ptr_array_remove_index_fast (priv->providers, i--);
                else
                {
                    /* still connected for children listed on list, but not the
                     * current location. So, we can disconnect from new_child */
                    g_signal_handler_disconnect (ps->provider,
                            ps->sid_node_new_child);
                    ps->sid_node_new_child = 0;
                }
                ++done;
            }

            if (done == 2)
                break;
        }
        if (found < 0)
        {
            ps = g_new0 (struct provider_signals, 1);
            ps->provider = g_object_ref (provider);
            ps->nb_nodes = 1;
            ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                    G_CALLBACK (node_updated_cb), data->tree);
            ps->sid_node_removed = g_signal_connect (provider, "node-removed",
                    G_CALLBACK (node_removed_cb), data->tree);

            g_ptr_array_add (priv->providers, ps);
        }
        else
            ps = priv->providers->pdata[found];
        /* whether or not we created ps, we need to connect to new_child, since
         * it's only useful for current location */
        ps->sid_node_new_child = g_signal_connect (provider, "node-new-child",
                G_CALLBACK (node_new_child_cb), data->tree);
        if (provider_location)
            g_object_unref (provider_location);
    }

    /* emit signal */
    g_object_notify_by_pspec ((GObject *) data->tree,
            donna_tree_view_props[PROP_LOCATION]);

free:
    free_node_get_children_list_data (data);
}

/* mode list only */
static void
node_get_parent_list_cb (DonnaTask                            *task,
                         gboolean                              timeout_called,
                         struct node_get_children_list_data   *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    const GValue *value;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        gchar             *location;
        const GError      *error;

        error = donna_task_get_error (task);
        location = donna_node_get_location (data->child);
        donna_app_show_error (priv->app, error,
                "Treeview '%s': Failed to get parent for node '%s:%s'",
                priv->name,
                donna_node_get_domain (data->child),
                location);
        g_free (location);

        if (priv->future_location == data->child)
        {
            priv->future_location = NULL;

            /* since we couldn't go there, make sure our real current location
             * is known (e.g. when user clicked on tree, it should go back to
             * where we are, since we failed to change location) */
            g_object_notify_by_pspec (G_OBJECT (data->tree),
                    donna_tree_view_props[PROP_LOCATION]);
        }

        free_node_get_children_list_data (data);
        return;
    }

    /* is this still valid (or did the user click away already) ? */
    if (priv->future_location != data->child)
    {
        free_node_get_children_list_data (data);
        return;
    }

    value = donna_task_get_return_value (task);
    /* simply update data, as we'll re-use it for the new task */
    data->node = g_value_dup_object (value);
    /* update future location (no ref needed) */
    priv->future_location = data->node;

    task = donna_node_get_children_task (data->node, priv->node_types, NULL);
    if (!timeout_called)
        donna_task_set_timeout (task, 800, /* FIXME */
                (task_timeout_fn) node_get_children_list_timeout,
                data,
                NULL);
    donna_task_set_callback (task,
            (task_callback_fn) node_get_children_list_cb,
            data,
            (GDestroyNotify) free_node_get_children_list_data);
    donna_app_run_task (priv->app, task);
}

gboolean
donna_tree_view_set_location (DonnaTreeView  *tree,
                              DonnaNode      *node,
                              GError        **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaNodeType node_type;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = tree->priv;
    node_type = donna_node_get_node_type (node);

    if (is_tree (tree))
    {
        GtkTreeIter *iter;

        if (!(priv->node_types & node_type))
        {
            gchar *location = donna_node_get_location (node);
            g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                    "Treeview '%s': Cannot go to '%s:%s', invalid type",
                    priv->name, donna_node_get_domain (node), location);
            g_free (location);
            return FALSE;
        }

        iter = get_best_iter_for_node (tree, node, TRUE, error);
        if (iter)
        {
            GtkTreePath *path;

            /* we select the new row and put the cursor on it (required to get
             * things working when collapsing the parent) */
            path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->store), iter);
            gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree), path, NULL, FALSE);
            gtk_tree_path_free (path);

            /* we want to scroll to this current row, but we do it in an idle
             * source to make sure any pending drawing has been processed;
             * specifically any expanding that might have been requested */
            g_idle_add ((GSourceFunc) scroll_to_current, tree);
        }

        priv->future_location_iter.stamp = 0;
        return !!iter;
    }
    else
    {
        DonnaProvider *provider;
        struct node_get_children_list_data *data;

        provider = donna_node_peek_provider (node);

        if (node_type == DONNA_NODE_CONTAINER)
        {
            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->node = g_object_ref (node);

            task = donna_provider_get_node_children_task (provider, node,
                    priv->node_types, NULL);
            donna_task_set_timeout (task, 800, /* FIXME */
                    (task_timeout_fn) node_get_children_list_timeout,
                    data,
                    NULL);
            donna_task_set_callback (task,
                    (task_callback_fn) node_get_children_list_cb,
                    data,
                    (GDestroyNotify) free_node_get_children_list_data);
        }
        else /* DONNA_NODE_ITEM */
        {
            if (donna_provider_get_flags (provider) == DONNA_PROVIDER_FLAG_FLAT)
            {
                gchar *fl;

                fl = donna_node_get_full_location (node);
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot set node '%s' as current location, provider is flat (i.e. no parent to go to)",
                        priv->name, fl);
                g_free (fl);
                return FALSE;
            }

            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->child = g_object_ref (node);

            task = donna_provider_get_node_parent_task (provider, node, NULL);
            donna_task_set_timeout (task, 800, /* FIXME */
                    (task_timeout_fn) node_get_children_list_timeout,
                    data,
                    NULL);
            donna_task_set_callback (task,
                    (task_callback_fn) node_get_parent_list_cb,
                    data,
                    (GDestroyNotify) free_node_get_children_list_data);
        }

        /* we don't ref this node, since we should only have it for a short
         * period of time, and will onyl use it to compare (the pointer) in the
         * task's timeout/cb, to make sure the new location is still valid */
        priv->future_location = node;

        donna_app_run_task (priv->app, task);
        return TRUE;
    }
}

DonnaNode *
donna_tree_view_get_location (DonnaTreeView      *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);

    if (tree->priv->location)
        return g_object_ref (tree->priv->location);
    else
        return NULL;
}

GPtrArray *
donna_tree_view_get_selected_nodes (DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GList *list, *l;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    sel  = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    list = gtk_tree_selection_get_selected_rows (sel, NULL);
    if (!list)
        return NULL;

    arr = g_ptr_array_new_full (gtk_tree_selection_count_selected_rows (sel),
            g_object_unref);
    for (l = list; l; l = l->next)
    {
        GtkTreeIter iter;
        DonnaNode *node;

        gtk_tree_model_get_iter (model, &iter, l->data);
        gtk_tree_model_get (model, &iter,
                DONNA_TREE_VIEW_COL_NODE,   &node,
                -1);
        g_ptr_array_add (arr, node);
    }
    g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
    return arr;
}

typedef enum
{
    ROW_ID_INVALID = 0,
    ROW_ID_ROW,
    ROW_ID_SELECTION,
    ROW_ID_ALL,
} row_id_type;

static row_id_type
convert_row_id_to_iter (DonnaTreeView   *tree,
                        DonnaTreeRowId  *rowid,
                        GtkTreeIter     *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *list;

    if (rowid->type == DONNA_ARG_TYPE_ROW)
    {
        DonnaTreeRow *row = rowid->ptr;

        list = g_hash_table_lookup (priv->hashtable, row->node);
        if (!list)
            return ROW_ID_INVALID;
        for ( ; list; list = list->next)
            if ((GtkTreeIter *) list->data == row->iter)
            {
                *iter = *row->iter;
                return ROW_ID_ROW;
            }
        return ROW_ID_INVALID;
    }
    else if (rowid->type == DONNA_ARG_TYPE_NODE)
    {
        list = g_hash_table_lookup (priv->hashtable, rowid->ptr);
        if (!list)
            return ROW_ID_INVALID;
        *iter = * (GtkTreeIter *) list->data;
        return ROW_ID_ROW;
    }
    else if (rowid->type == DONNA_ARG_TYPE_PATH)
    {
        gchar *s = rowid->ptr;

        if (*s == ':')
        {
            ++s;
            if (streq ("all", s))
                return ROW_ID_ALL;
            else if (streq ("selected", s))
                return ROW_ID_SELECTION;
            else if (streq ("focused", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("prev", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                    return ROW_ID_INVALID;

                for (;;)
                {
                    if (!donna_tree_model_iter_previous ((GtkTreeModel *) priv->store,
                                iter))
                        return ROW_ID_INVALID;
                    if (!is_tree (tree) || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("next", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                    return ROW_ID_INVALID;

                for (;;)
                {
                    if (!donna_tree_model_iter_next ((GtkTreeModel *) priv->store,
                                iter))
                        return ROW_ID_INVALID;
                    if (!is_tree (tree) || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("last", s))
            {
                GtkTreeModel *model = (GtkTreeModel *) priv->store;
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter (model, iter, path))
                    return ROW_ID_INVALID;

                if (!donna_tree_model_iter_last (model, iter))
                    return ROW_ID_INVALID;
                if (is_tree (tree))
                {
                    while (!is_row_accessible (tree, iter))
                        if (!donna_tree_model_iter_previous (model, iter))
                            return ROW_ID_INVALID;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("up", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_up (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("down", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_down (path);

                if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path) && is_row_accessible (tree, iter))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("prev-same-depth", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_prev (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("next-same-depth", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor ((GtkTreeView *) tree,
                        &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_next (path);

                if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else
                return ROW_ID_INVALID;
        }
        else
        {
            GtkTreePath *path = gtk_tree_path_new_from_string (s);
            if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                        iter, path))
            {
                gtk_tree_path_free (path);
                return ROW_ID_ROW;
            }
            gtk_tree_path_free (path);
            return ROW_ID_INVALID;
        }
    }
    else
        return ROW_ID_INVALID;
}

static void
unselect_path (gpointer p, gpointer s)
{
    gtk_tree_selection_unselect_path ((GtkTreeSelection *) s, (GtkTreePath *) p);
}

gboolean
donna_tree_view_selection (DonnaTreeView        *tree,
                           DonnaTreeSelAction    action,
                           DonnaTreeRowId       *rowid,
                           gboolean              to_focused,
                           GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    row_id_type type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (action == DONNA_TREE_SEL_SELECT
            || action == DONNA_TREE_SEL_UNSELECT
            || action == DONNA_TREE_SEL_INVERT, FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);

    priv = tree->priv;
    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot update selection, invalid row-id",
                priv->name);
        return FALSE;
    }

    /* tree is limited in its selection capabilities */
    if (is_tree (tree) && !(type == ROW_ID_ROW && !to_focused
                && action == DONNA_TREE_SEL_SELECT))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot update selection, incompatible with mode tree",
                priv->name);
        return FALSE;
    }

    if (type == ROW_ID_ALL)
    {
        if (action == DONNA_TREE_SEL_SELECT)
            gtk_tree_selection_select_all (sel);
        else if (action == DONNA_TREE_SEL_UNSELECT)
            gtk_tree_selection_unselect_all (sel);
        else /* DONNA_TREE_SEL_INVERT */
        {
            gint nb, count;
            GList *list;

            nb = gtk_tree_selection_count_selected_rows (sel);
            if (nb == 0)
            {
                gtk_tree_selection_select_all (sel);
                return TRUE;
            }

            count = donna_tree_model_get_count ((GtkTreeModel *) priv->store);
            if (nb == count)
            {
                gtk_tree_selection_unselect_all (sel);
                return TRUE;
            }

            list = gtk_tree_selection_get_selected_rows (sel, NULL);
            gtk_tree_selection_select_all (sel);
            g_list_foreach (list, unselect_path, sel);
            g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
        }
        return TRUE;
    }
    else if (type == ROW_ID_SELECTION)
    {
        /* SELECT the selection means do nothing; UNSELECT & INVERT both means
         * unselect (all) */
        if (action == DONNA_TREE_SEL_UNSELECT || action == DONNA_TREE_SEL_INVERT)
            gtk_tree_selection_unselect_all (sel);
        return TRUE;
    }
    else /* ROW_ID_ROW */
    {
        if (to_focused)
        {
            GtkTreePath *path, *path_focus;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (!path_focus)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot update selection, failed to get focused row",
                        priv->name);
                return FALSE;
            }
            path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
            if (!path)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot update selection, failed to get path",
                        priv->name);
                gtk_tree_path_free (path_focus);
                return FALSE;
            }

            if (action == DONNA_TREE_SEL_SELECT)
                gtk_tree_selection_select_range (sel, path, path_focus);
            else if (action == DONNA_TREE_SEL_UNSELECT)
                gtk_tree_selection_unselect_range (sel, path, path_focus);
            else /* DONNA_TREE_SEL_INVERT */
#ifdef GTK_IS_JJK
                gtk_tree_selection_invert_range (sel, path, path_focus);
#else
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot invert selection on a range (Vanilla GTK+ limitation)",
                        priv->name);
                gtk_tree_path_free (path);
                gtk_tree_path_free (path_focus);
                return FALSE;
            }
#endif

            gtk_tree_path_free (path);
            gtk_tree_path_free (path_focus);
            return TRUE;
        }
        else
        {
            if (action == DONNA_TREE_SEL_SELECT)
                gtk_tree_selection_select_iter (sel, &iter);
            else if (action == DONNA_TREE_SEL_UNSELECT)
                gtk_tree_selection_unselect_iter (sel, &iter);
            else /* DONNA_TREE_SEL_INVERT */
            {
                if (gtk_tree_selection_iter_is_selected (sel, &iter))
                    gtk_tree_selection_unselect_iter (sel, &iter);
                else
                    gtk_tree_selection_select_iter (sel, &iter);
            }
            return TRUE;
        }
    }
}

#ifdef GTK_IS_JJK
gboolean
donna_tree_view_set_focus (DonnaTreeView        *tree,
                           DonnaTreeRowId       *rowid,
                           GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    GtkTreePath *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot set focus, invalid row-id",
                priv->name);
        return FALSE;
    }

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);
    return TRUE;
}
#endif

gboolean
donna_tree_view_set_cursor (DonnaTreeView        *tree,
                            DonnaTreeRowId       *rowid,
                            GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    GtkTreePath *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot set cursor, invalid row-id",
                priv->name);
        return FALSE;
    }

    /* more about this can be read in sync_with_location_changed_cb() */

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
#ifdef GTK_IS_JJK
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
    gtk_tree_selection_select_path (
            gtk_tree_view_get_selection ((GtkTreeView *) tree), path);
#else
    gtk_tree_view_set_cursor ((GtkTreeView *) tree, path, NULL, FALSE);
#endif
    gtk_tree_path_free (path);
    if (is_tree (tree) && priv->sync_scroll)
        scroll_to_iter (tree, &iter);
    return TRUE;
}

gboolean
donna_tree_view_activate_row (DonnaTreeView      *tree,
                              DonnaTreeRowId     *rowid,
                              GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    DonnaNode   *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot activate row, invalid row-id",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);
    if (!node)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "Treeview '%s': No node matching the given row-id",
                priv->name);
        return FALSE;
    }

    /* since the node is in the tree, we already have a ref on it */
    g_object_unref (node);

    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
        return donna_tree_view_set_location (tree, node, error);
    else /* DONNA_NODE_ITEM */
    {
        DonnaTask *task;

        task = donna_node_trigger_task (node, error);
        if (!task)
            return FALSE;

        donna_task_set_callback (task,
                (task_callback_fn) show_err_on_task_failed, tree, NULL);
        donna_app_run_task (priv->app, task);
        return TRUE;
    }
}

gboolean
donna_tree_view_toggle_row (DonnaTreeView      *tree,
                            DonnaTreeRowId     *rowid,
                            DonnaTreeToggle     toggle,
                            GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeIter  iter;
    row_id_type  type;
    enum tree_expand es;
    GtkTreePath *path;
    gboolean     ret = TRUE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': toggle_node() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot toggle row, invalid row-id",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    if (es == DONNA_TREE_EXPAND_NONE)
        return TRUE;

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    if (G_UNLIKELY (!path))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Failed to obtain path for iter", priv->name);
        return FALSE;
    }

    if (gtk_tree_view_row_expanded (treev, path))
    {
        if (toggle == DONNA_TREE_TOGGLE_STANDARD)
            gtk_tree_view_collapse_row (treev, path);
        else if (toggle == DONNA_TREE_TOGGLE_FULL)
            ret = donna_tree_view_full_collapse (tree, rowid, error);
        else /* DONNA_TREE_TOGGLE_MAXI */
        {
            /* maxi is a special kind of toggle: if partially expanded, we
             * maxi-expand; Else, we maxi collapse */
            if (es == DONNA_TREE_EXPAND_PARTIAL)
                ret = donna_tree_view_maxi_expand (tree, rowid, error);
            else
                ret = donna_tree_view_maxi_collapse (tree, rowid, error);
        }
    }
    else
    {
        if (toggle == DONNA_TREE_TOGGLE_STANDARD)
            gtk_tree_view_expand_row (treev, path, FALSE);
        else if (toggle == DONNA_TREE_TOGGLE_FULL)
            ret = donna_tree_view_full_expand (tree, rowid, error);
        else /* DONNA_TREE_TOGGLE_MAXI */
        {
            /* maxi is a special kind of toggle: if never expanded, we (maxi)
             * expand; Else we maxi collapse */
            if (es == DONNA_TREE_EXPAND_NEVER || es == DONNA_TREE_EXPAND_UNKNOWN)
                gtk_tree_view_expand_row (treev, path, FALSE);
            else
                ret = donna_tree_view_maxi_collapse (tree, rowid, error);
        }
    }

    gtk_tree_path_free (path);
    return ret;
}

static void full_expand_children (DonnaTreeView *tree, GtkTreeIter *iter);

static inline void
full_expand (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    switch (es)
    {
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
            /* will import/create get_children task. Will also call ourself
             * on iter, or make sure it gets called from the task's cb */
            expand_row (tree, iter, FALSE, full_expand_children);
            break;

        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_MAXI:
            {
                GtkTreePath *path;
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
                gtk_tree_path_free (path);
                /* recursion */
                full_expand_children (tree, iter);
            }
            break;

        case DONNA_TREE_EXPAND_NONE:
        case DONNA_TREE_EXPAND_WIP:
            break;
    }
}

static void
full_expand_children (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter child;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    do
    {
        full_expand (tree, &child);
    } while (gtk_tree_model_iter_next (model, &child));
}

gboolean
donna_tree_view_full_expand (DonnaTreeView      *tree,
                             DonnaTreeRowId     *rowid,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeIter  iter;
    row_id_type  type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': full_expand() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot full-expand row, invalid row-id",
                priv->name);
        return FALSE;
    }

    full_expand (tree, &iter);
    return TRUE;
}

static void
reset_expand_flag (GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkTreeIter child;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    do
    {
        donna_tree_store_set ((DonnaTreeStore *) model, &child,
                DONNA_TREE_COL_EXPAND_FLAG, FALSE,
                -1);
        reset_expand_flag (model, &child);
    } while (gtk_tree_model_iter_next (model, &child));
}

gboolean
donna_tree_view_full_collapse (DonnaTreeView      *tree,
                               DonnaTreeRowId     *rowid,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;
    GtkTreePath  *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': full_collapse() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot full-collapse row, invalid row-id",
                priv->name);
        return FALSE;
    }

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    gtk_tree_view_collapse_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);

    /* we also need to recursively set the EXPAND_FLAG to FALSE */
    reset_expand_flag ((GtkTreeModel *) priv->store, &iter);

    return TRUE;
}

gboolean
donna_tree_view_maxi_expand (DonnaTreeView      *tree,
                             DonnaTreeRowId     *rowid,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': maxi_expand() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }
    if (G_UNLIKELY (!priv->is_minitree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': maxi_expand() only works in mini-tree",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot maxi-expand row, invalid row-id",
                priv->name);
        return FALSE;
    }

    maxi_expand_row (tree, &iter);
    return TRUE;
}

gboolean
donna_tree_view_maxi_collapse (DonnaTreeView      *tree,
                               DonnaTreeRowId     *rowid,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': maxi_collapse() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }
    if (G_UNLIKELY (!priv->is_minitree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': maxi_collapse() only works in mini-tree",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot maxi-collapse row, invalid row-id",
                priv->name);
        return FALSE;
    }

    maxi_collapse_row (tree, &iter);
    return TRUE;
}

gboolean
donna_tree_view_set_visual (DonnaTreeView      *tree,
                            DonnaTreeRowId     *rowid,
                            DonnaTreeVisual     visual,
                            const gchar        *_value,
                            GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    DonnaTreeVisual v;
    gpointer value = (gpointer) _value;
    guint col;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    g_return_val_if_fail (visual != 0, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': set_visual() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot set visual, invalid row-id",
                priv->name);
        return FALSE;
    }

    if (visual == DONNA_TREE_VISUAL_NAME)
        col = DONNA_TREE_COL_NAME;
    else if (visual == DONNA_TREE_VISUAL_ICON)
    {
        col = DONNA_TREE_COL_ICON;
        value = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                    _value, 16 /* FIXME */, 0, NULL);
    }
    else if (visual == DONNA_TREE_VISUAL_BOX)
        col = DONNA_TREE_COL_BOX;
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
        col = DONNA_TREE_COL_HIGHLIGHT;
    else if (visual == DONNA_TREE_VISUAL_CLICKS)
        col = DONNA_TREE_COL_CLICKS;
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot set visual, invalid visual type",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_COL_VISUALS,     &v,
            -1);
    v |= visual;
    donna_tree_store_set (priv->store, &iter,
            DONNA_TREE_COL_VISUALS,     v,
            col,                        value,
            -1);

    if (visual == DONNA_TREE_VISUAL_ICON)
        g_object_unref (value);

    return TRUE;
}

gchar *
donna_tree_view_get_visual (DonnaTreeView           *tree,
                            DonnaTreeRowId          *rowid,
                            DonnaTreeVisual          visual,
                            DonnaTreeVisualSource    source,
                            GError                 **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    DonnaTreeVisual v;
    guint col;
    gchar *value;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    g_return_val_if_fail (visual != 0, NULL);
    g_return_val_if_fail (source == DONNA_TREE_VISUAL_SOURCE_TREE
            || source == DONNA_TREE_VISUAL_SOURCE_NODE
            || source == DONNA_TREE_VISUAL_SOURCE_ANY, NULL);
    priv = tree->priv;

    if (G_UNLIKELY (!is_tree (tree)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': get_visual() doesn't apply in mode list",
                priv->name);
        return NULL;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot set visual, invalid row-id",
                priv->name);
        return NULL;
    }

    if (visual == DONNA_TREE_VISUAL_NAME)
        col = DONNA_TREE_COL_NAME;
    else if (visual == DONNA_TREE_VISUAL_BOX)
        col = DONNA_TREE_COL_BOX;
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
        col = DONNA_TREE_COL_HIGHLIGHT;
    else if (visual == DONNA_TREE_VISUAL_CLICKS)
        col = DONNA_TREE_COL_CLICKS;
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot get visual, invalid visual type",
                priv->name);
        return NULL;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_COL_VISUALS,     &v,
            -1);

    if (source == DONNA_TREE_VISUAL_SOURCE_TREE)
    {
        if (!(v & visual))
            return NULL;
    }
    else if (source == DONNA_TREE_VISUAL_SOURCE_NODE)
    {
        if (v & visual)
            return NULL;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            col,    &value,
            -1);

    return value;
}

struct re_data
{
    DonnaTreeView       *tree;
    GtkTreeViewColumn   *column;
    GtkTreePath         *path;
};

static void
editable_remove_widget_cb (GtkCellEditable *editable, DonnaTreeView *tree)
{
    g_signal_handler_disconnect (editable,
            tree->priv->renderer_editable_remove_widget_sid);
    tree->priv->renderer_editable_remove_widget_sid = 0;
    tree->priv->renderer_editable = NULL;
}

static void
editing_started_cb (GtkCellRenderer *renderer,
                    GtkCellEditable *editable,
                    gchar           *path,
                    DonnaTreeView   *tree)
{
    g_signal_handler_disconnect (renderer, tree->priv->renderer_editing_started_sid);
    tree->priv->renderer_editing_started_sid = 0;

    donna_app_ensure_focused (tree->priv->app);

    /* in case we need to abort the editing */
    tree->priv->renderer_editable = editable;
    /* when the editing will be done */
    tree->priv->renderer_editable_remove_widget_sid = g_signal_connect (
            editable, "remove-widget",
            (GCallback) editable_remove_widget_cb, tree);
}

static gboolean
renderer_edit (GtkCellRenderer *renderer, struct re_data *data)
{
    GdkEventAny event = { .type = GDK_NOTHING };
    GtkTreePath *path;
    GdkRectangle cell_area;
    gint offset;

    /* shouldn't happen, but to be safe */
    if (G_UNLIKELY (data->tree->priv->renderer_editable))
        return FALSE;

    /* get the cell_area (i.e. where editable will be placed */
    gtk_tree_view_get_cell_area ((GtkTreeView *) data->tree,
            data->path, data->column, &cell_area);
    /* in case there are other renderers in that column */
    if (gtk_tree_view_column_cell_get_position (data->column, renderer,
            &offset, &cell_area.width))
        cell_area.x += offset;

    /* so we can get the editable to be able to abort if needed */
    data->tree->priv->renderer_editing_started_sid = g_signal_connect (
            renderer, "editing-started",
            (GCallback) editing_started_cb, data->tree);

    return gtk_cell_area_activate_cell (
            gtk_cell_layout_get_area ((GtkCellLayout *) data->column),
            (GtkWidget *) data->tree,
            renderer,
            (GdkEvent *) &event,
            &cell_area,
            0);
}

gboolean
donna_tree_view_edit_column (DonnaTreeView      *tree,
                             DonnaTreeRowId     *rowid,
                             const gchar        *column,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    struct column *_col;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    g_return_val_if_fail (column != NULL, NULL);
    priv = tree->priv;

    _col = get_column_by_name (tree, column);
    if (!_col)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_UNKNOWN_COLUMN,
                "Treeview '%s': Cannot edit column, unknown column '%s'",
                priv->name, column);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot edit column, invalid row-id",
                priv->name);
        return FALSE;
    }

    struct re_data re_data = {
        .tree   = tree,
        .column = _col->column,
        .path   = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter),
    };

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);

    if (!donna_columntype_edit (_col->ct, _col->ct_data, node,
                (GtkCellRenderer **) _col->renderers->pdata,
                (renderer_edit_fn) renderer_edit, &re_data, tree, error))
    {
        g_object_unref (node);
        return FALSE;
    }

    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, re_data.path);

    g_object_unref (node);
    return TRUE;
}

/* mode list only */
GPtrArray *
donna_tree_view_get_children (DonnaTreeView      *tree,
                              DonnaNode          *node,
                              DonnaNodeType       node_types)
{
    DonnaTreeViewPrivate *priv;
    GList *list, *l;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (!is_tree (tree), NULL);

    priv = tree->priv;

    if (node != priv->location)
        return NULL;

    if (!(node_types & priv->node_types))
        return NULL;

    /* list changing location, already cleared the children */
    if (!is_tree (tree) && tree->priv->draw_state == DRAW_WAIT)
        return NULL;

    /* get list of nodes we have in tree */
    list = g_hash_table_get_keys (priv->hashtable);
    /* create an array that could hold them all */
    arr = g_ptr_array_new_full (g_hash_table_size (priv->hashtable),
            g_object_unref);
    /* fill array based on requested node_types */
    for (l = list ; l; l = l->next)
    {
        if (donna_node_get_node_type (l->data) & node_types)
            g_ptr_array_add (arr, g_object_ref (l->data));
    }
    g_list_free (list);

    return arr;
}

static gboolean
query_tooltip_cb (GtkTreeView   *treev,
                  gint           x,
                  gint           y,
                  gboolean       keyboard_mode,
                  GtkTooltip    *tooltip)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    GtkTreeViewColumn *column;
#ifdef GTK_IS_JJK
    GtkCellRenderer *renderer;
#endif
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean ret = FALSE;

    /* x & y are widget coords, converted to bin_window coords */
    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, keyboard_mode,
                &model, NULL, &iter))
    {
#ifdef GTK_IS_JJK
        if (!gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (!gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
        {
            DonnaNode *node;
            struct column *_col;
            guint index = 0;

            gtk_tree_model_get (model, &iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (!node)
                return FALSE;

#ifdef GTK_IS_JJK
            if (renderer == int_renderers[INTERNAL_RENDERER_SPINNER])
                return FALSE;
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
            {
                struct active_spinners *as;
                guint i;

                as = get_as_for_node (tree, node, NULL, FALSE);
                if (G_UNLIKELY (!as))
                {
                    g_object_unref (node);
                    return FALSE;
                }

                for (i = 0; i < as->as_cols->len; ++i)
                {
                    struct as_col *as_col;
                    GString *str;

                    as_col = &g_array_index (as->as_cols, struct as_col, i);
                    if (as_col->column != column)
                        continue;

                    str = g_string_new (NULL);
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            const GError *err;

                            if (str->len > 0)
                                g_string_append_c (str, '\n');

                            err = donna_task_get_error (task);
                            if (err)
                                g_string_append (str, err->message);
                            else
                                g_string_append (str, "Task failed, no error message");
                        }
                    }

                    i = str->len > 0;
                    if (i)
                        gtk_tooltip_set_text (tooltip, str->str);
                    g_string_free (str, TRUE);
                    return (gboolean) i;
                }
                return FALSE;
            }
#endif
            _col = get_column_by_column (tree, column);

#ifdef GTK_IS_JJK
            if (renderer)
            {
                const gchar *rend;

                rend = donna_columntype_get_renderers (_col->ct);
                if (rend[1] == '\0')
                    /* only one renderer in this column */
                    index = 1;
                else
                {
                    gchar r;

                    r = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (renderer),
                                "renderer-type"));
                    for (index = 1; *rend && *rend != r; ++index, ++rend)
                        ;
                }
            }
#endif
            ret = donna_columntype_set_tooltip (_col->ct, _col->ct_data,
                    index, node, tooltip);

            g_object_unref (node);
        }
    }
    return ret;
}

static void
donna_tree_view_row_activated (GtkTreeView    *treev,
                               GtkTreePath    *path,
                               GtkTreeViewColumn *column)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaTreeRowId rowid;

    /* warning because this shouldn't happen, as we're doing things our own way.
     * If this happens, it's probably an oversight somewhere that should be
     * fixed. So warning, and then we just do our ativating */
    g_warning ("Treeview '%s': row-activated signal was emitted", tree->priv->name);

    rowid.type = DONNA_ARG_TYPE_PATH;
    rowid.ptr  = gtk_tree_path_to_string (path);
    donna_tree_view_activate_row (tree, &rowid, NULL);
    g_free (rowid.ptr);
}

static void
check_children_post_expand (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    DonnaNode *loc_node;
    DonnaProvider *loc_provider;
    gchar *loc_location;
    GtkTreeIter child;

    /* don't do this when we're not sync, otherwise collapsing a row where we
     * put the focus would trigger a selection which we don't want */
    if (priv->sync_mode == DONNA_TREE_SYNC_NONE)
        return;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    loc_node = donna_tree_view_get_location (priv->sync_with);
    loc_provider = donna_node_peek_provider (loc_node);
    loc_location = donna_node_get_location (loc_node);
    do
    {
        DonnaNode *n;

        gtk_tree_model_get (model, &child,
                DONNA_TREE_COL_NODE,    &n,
                -1);
        /* did we just revealed the node or one of its parent? */
        if (n == loc_node || is_node_ancestor (n, loc_node,
                    loc_provider, loc_location))
        {
            GtkTreeView *treev = (GtkTreeView *) tree;
            GtkTreeSelection *sel;
            GtkTreePath *loc_path;

            loc_path = gtk_tree_model_get_path (model, &child);
#ifdef GTK_IS_JJK
            gtk_tree_view_set_focused_row (treev, loc_path);
#endif
            if (n == loc_node)
            {
#ifdef GTK_IS_JJK
                sel = gtk_tree_view_get_selection (treev);
                gtk_tree_selection_select_path (sel, loc_path);
#else
                gtk_tree_view_set_cursor (treev, loc_path, NULL, FALSE);
#endif
            }
#ifndef GTK_IS_JJK
            else
            {
                /* ancestor, so we just want to put the cursor on the node, no
                 * selection */
                sel = gtk_tree_view_get_selection (treev);
                gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
                gtk_tree_view_set_cursor (treev, loc_path, NULL, FALSE);
                gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
            }
#endif
            gtk_tree_path_free (loc_path);

            if (priv->sync_scroll)
                scroll_to_iter (tree, &child);

            g_object_unref (n);
            break;
        }
        g_object_unref (n);
    } while (gtk_tree_model_iter_next (model, &child));

    g_free (loc_location);
    g_object_unref (loc_node);
}

#define ensure_str()  do {                          \
    if (!str)                                       \
        str = g_string_new (NULL);                  \
    g_string_append_len (str, sce, s - sce);        \
} while (0)
gchar *
tree_parse_location (DonnaTreeView  *tree,
                     DonnaTreeRow   *row,
                     struct column  *_col,
                     const gchar    *sce)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GString *str = NULL;
    gchar *s = (gchar *) sce;
    gchar *ss;

    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'o':
                ensure_str ();
                g_string_append (str, priv->name);
                break;

            case 'L':
                ensure_str ();
                if (priv->location)
                {
                    ss = donna_node_get_full_location (priv->location);
                    g_string_append (str, ss);
                    g_free (ss);
                }
                else
                    g_string_append_c (str, '-');
                break;

            case 'l':
                ensure_str ();
                if (priv->location)
                {
                    ss = donna_node_get_location (priv->location);
                    g_string_append (str, ss);
                    g_free (ss);
                }
                else
                    g_string_append_c (str, '-');
                break;

            case 'R':
                ensure_str ();
                g_string_append (str, _col->name);
                break;

            case 'r':
                ensure_str ();
                if (row)
                    g_string_append_printf (str, "[%p;%p]", row->node, row->iter);
                else
                    g_string_append_c (str, '-');
                break;

            case 'N':
                ensure_str ();
                if (row)
                {
                    ss = donna_node_get_full_location (row->node);
                    g_string_append (str, ss);
                    g_free (ss);
                }
                else
                    g_string_append_c (str, '-');
                break;

            case 'n':
                ensure_str ();
                if (row)
                {
                    ss = donna_node_get_location (row->node);
                    g_string_append (str, ss);
                    g_free (ss);
                }
                else
                    g_string_append_c (str, '-');
                break;

            default:
                s += 2;
                continue;
        }
        s += 2;
        sce = (const gchar *) s;
    }

    if (!str)
        return NULL;

    g_string_append (str, sce);
    return g_string_free (str, FALSE);
}
#undef ensure_str

#define is_regular_left_click(click, event)             \
    ((click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))  \
     == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)         \
     && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))

struct rc_data
{
    gboolean         is_heap;
    DonnaTreeView   *tree;
    struct column   *_col;
    DonnaTreeRow    *row;
    gchar           *fl;
    DonnaCommand    *command;
    gchar           *start;
    gchar           *end;
    guint            i;
    GPtrArray       *arr;
};

static void
free_rc_data (struct rc_data *data)
{
    g_free (data->row);
    g_free (data->fl);
    if (data->arr)
        _donna_command_free_args (data->command, data->arr);
    if (data->is_heap)
        g_free (data);
}

static void
command_run_cb (DonnaTask *task, gboolean timeout_called, struct rc_data *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (data->tree->priv->app, donna_task_get_error (task),
                "Treeview '%s': Action triggered failed",
                data->tree->priv->name);
    free_rc_data (data);
}

static DonnaTaskState
run_command (DonnaTask *task, struct rc_data *data)
{
    GError *err = NULL;
    DonnaTask *cmd_task;

    if (!data->command)
    {
        data->command = _donna_command_init_parse (data->fl + 8,
                &data->start, &data->end, &err);
        if (!data->command)
        {
            donna_app_show_error (data->tree->priv->app, err,
                    "Treeview '%s': Cannot trigger action, parsing command failed",
                    data->tree->priv->name);
            g_clear_error (&err);
            free_rc_data (data);
            return DONNA_TASK_FAILED;
        }

        data->arr = g_ptr_array_sized_new (data->command->argc + 3);
        g_ptr_array_add (data->arr, task);
    }
    for ( ; data->i < data->command->argc; ++data->i)
    {
        gchar c;

        c = *data->end;
        *data->end = '\0';
        if (streq (data->start, "%o"))
        {
            if (data->command->arg_type[data->i] == DONNA_ARG_TYPE_TREEVIEW)
                g_ptr_array_add (data->arr, g_object_ref (data->tree));
            else
                goto str_parsing;
        }
        else if (streq (data->start, "%L"))
        {
            if (data->command->arg_type[data->i] == DONNA_ARG_TYPE_NODE)
                g_ptr_array_add (data->arr, g_object_ref (data->tree->priv->location));
            else
                goto str_parsing;
        }
        else if (streq (data->start, "%r"))
        {
            if (data->command->arg_type[data->i] == DONNA_ARG_TYPE_ROW_ID)
            {
                DonnaTreeRowId *rid = g_new (DonnaTreeRowId, 1);
                DonnaTreeRow *r = g_new (DonnaTreeRow, 1);
                rid->type = DONNA_ARG_TYPE_ROW;
                rid->ptr  = r;
                r->node = data->row->node;
                r->iter = data->row->iter;
                g_ptr_array_add (data->arr, rid);
            }
            else if (data->command->arg_type[data->i] == DONNA_ARG_TYPE_ROW)
            {
                DonnaTreeRow *r = g_new (DonnaTreeRow, 1);
                r->node = data->row->node;
                r->iter = data->row->iter;
                g_ptr_array_add (data->arr, r);
            }
            else
                goto str_parsing;
        }
        else if (streq (data->start, "%N"))
        {
            if (data->command->arg_type[data->i] == DONNA_ARG_TYPE_NODE)
                g_ptr_array_add (data->arr, g_object_ref (data->row->node));
            else
                goto str_parsing;
        }
        else
        {
            gchar *ss;
            gpointer ptr;

str_parsing:
            ss = tree_parse_location (data->tree, data->row, data->_col, data->start);
            if (!_donna_command_convert_arg (data->tree->priv->app,
                        data->command->arg_type[data->i], TRUE, task != NULL,
                        (ss) ? ss : data->start, &ptr, &err))
            {
                g_free (ss);
                if (g_error_matches (err, COMMAND_ERROR, COMMAND_ERROR_MIGHT_BLOCK))
                {
                    struct rc_data *d;

                    /* restore */
                    *data->end = c;

                    /* need to put data on heap */
                    d = g_new (struct rc_data, 1);
                    memcpy (d, data, sizeof (struct rc_data));
                    d->is_heap = TRUE;

                    /* and continue parsing in a task/new thread */
                    task = donna_task_new ((task_fn) run_command, d,
                            (GDestroyNotify) free_rc_data);
                    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL);
                    donna_app_run_task (data->tree->priv->app, task);
                    return DONNA_TASK_DONE;
                }
                donna_app_show_error (data->tree->priv->app, err,
                        "Treeview '%s': Cannot trigger action, parsing command failed",
                        data->tree->priv->name);
                g_clear_error (&err);
                free_rc_data (data);
                return DONNA_TASK_FAILED;
            }
            g_free (ss);
            g_ptr_array_add (data->arr, ptr);
        }
        *data->end = c;

        if (!_donna_command_get_next_arg (data->command, data->i,
                    &data->start, &data->end, &err))
        {
            donna_app_show_error (data->tree->priv->app, err,
                    "Treeview '%s': Cannot trigger action, parsing command failed",
                    data->tree->priv->name);
            g_clear_error (&err);
            free_rc_data (data);
            return DONNA_TASK_FAILED;
        }
    }

    if (!_donna_command_checks_post_parsing (data->command, data->i,
                data->start, data->end, &err))
    {
        donna_app_show_error (data->tree->priv->app, err,
                "Treeview '%s': Cannot trigger action, parsing command failed",
                data->tree->priv->name);
        g_clear_error (&err);
        free_rc_data (data);
        return DONNA_TASK_FAILED;
    }

    /* add DonnaApp* as extra arg for command */
    g_ptr_array_add (data->arr, data->tree->priv->app);

    cmd_task = donna_task_new ((task_fn) data->command->cmd_fn, data->arr, NULL);
    donna_task_set_visibility (cmd_task, data->command->visibility);
    donna_task_set_callback (cmd_task, (task_callback_fn) command_run_cb, data,
            (GDestroyNotify) free_rc_data);
    if (task)
        /* avoid starting another thread, since we're already in one */
        donna_task_set_can_block (g_object_ref_sink (cmd_task));
    donna_app_run_task (data->tree->priv->app, cmd_task);
    if (task)
    {
        donna_task_wait_for_it (cmd_task);
        g_object_unref (cmd_task);
    }
}

enum
{
    CLICK_REGULAR = 0,
    CLICK_ON_BLANK,
    CLICK_ON_EXPANDER,
};

static void
handle_click (DonnaTreeView     *tree,
              DonnaClick         click,
              GdkEventButton    *event,
              GtkTreeIter       *iter,
              GtkTreeViewColumn *column,
              GtkCellRenderer   *renderer,
              guint              click_on)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    struct rc_data data;
    gboolean is_tree = is_tree (tree);
    gchar *clicks = NULL;
    /* longest possible is "blankcol_ctrl_shift_middle_double_click" (len=39) */
    gchar buf[40];
    gchar *b = buf + 9; /* leave space for "blankcol_" prefix */
    const gchar *def = NULL;

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
    if (click & DONNA_CLICK_LEFT)
    {
        strcpy (b, "left_");
        b += 5;
    }
    else if (click & DONNA_CLICK_MIDDLE)
    {
        strcpy (b, "middle_");
        b += 7;
    }
    else /* DONNA_CLICK_RIGHT */
    {
        strcpy (b, "right_");
        b += 6;
    }
    if (click & DONNA_CLICK_DOUBLE)
    {
        strcpy (b, "double_");
        b += 7;
    }
    else if (click & DONNA_CLICK_SLOW_DOUBLE)
    {
        strcpy (b, "slow_");
        b += 5;
    }
    /* else DONNA_CLICK_SINGLE; we don't print anything for it */
    strcpy (b, "click");

    config = donna_app_peek_config (priv->app);
    memset (&data, 0, sizeof (data));
    data._col = get_column_by_column (tree, column);

    if (!iter)
    {
        memcpy (buf, "blankrow_", 9 * sizeof (gchar));
        b = buf;
    }
    else if (!data._col)
    {
        memcpy (buf, "blankcol_", 9 * sizeof (gchar));
        b = buf;
    }
    else if (click_on == CLICK_ON_BLANK)
    {
        memcpy (buf + 3, "blank_", 6 * sizeof (gchar));
        b = buf + 3;
    }
    else if (click_on == CLICK_ON_EXPANDER)
    {
        memcpy (buf, "expander_", 9 * sizeof (gchar));
        b = buf;
    }
    else
        b = buf + 9;

    /* a few of those should have valid defaults, just in case */
    if (is_tree)
    {
        if (streq (b, "left_click"))
            def = "command:tree_set_cursor (%o, %r)";
        else if (streq (b, "left_double_click")
                || streq (b, "expander_left_click"))
            def = "command:tree_toggle_row (%o, %r, std)";
    }
    else
    {
        if (streq (b, "left_click"))
            def = "command:tree_set_focus (%o, %r)";
        else if (streq (b, "blank_left_click")
                || streq (b, "blankcol_left_click")
                || streq (b, "blankrow_left_click"))
            def = "command:tree_selection (%o, unselect, :all, -)";
        else if (streq (b, "left_double_click"))
            def = "command:tree_activate_row (%o, %r)";
    }

    if (is_tree && iter)
        gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
                DONNA_TREE_COL_CLICKS,  &clicks,
                -1);

    data.fl = _donna_config_get_string_tree_column (config, priv->name,
            (data._col) ? data._col->name : NULL,
            is_tree,
            (is_tree) ? clicks : priv->arrangement->columns_options,
            (is_tree) ? "clicks/tree" : "clicks/list",
            b, (gchar *) def);

    g_free (clicks);

    if (!data.fl)
        return;

    if (iter)
        data.row = get_row_for_iter (tree, iter);

    /* for commands we'll do the parsing directly, to avoid converting args to
     * string and back, or the need to use get_node tasks */
    if (streqn (data.fl, "command:", 8))
    {
        data.tree = tree;
        /* run_command() will take care of freeing data as/when needed */
        run_command (NULL, &data);
    }
    else
    {
        gchar *ss;

        ss = tree_parse_location (tree, data.row, data._col, data.fl);
        if (ss)
        {
            g_free (data.fl);
            data.fl = ss;
        }
        donna_app_trigger_node (priv->app, data.fl);
        g_free (data.fl);
        g_free (data.row);
    }
}

static gboolean
trigger_click (DonnaTreeView *tree, DonnaClick click, GdkEventButton *event)
{
    GtkTreeView *treev = (GtkTreeView *) tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint x, y;

    if (event->button == 1)
        click |= DONNA_CLICK_LEFT;
    else if (event->button == 2)
        click |= DONNA_CLICK_MIDDLE;
    else if (event->button == 3)
        click |= DONNA_CLICK_RIGHT;

    /* a click will grab the focus if:
     * - tree: it's a regular left click (i.e. no Ctrl/Shift held)
     * - list: it's a left click (event w/ Ctrl/Shift)
     * and, ofc, focus isn't on tree already */
    if (((is_tree (tree) && is_regular_left_click (click, event))
                || (!is_tree (tree)
                    && (click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))
                    == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)))
            && !gtk_widget_is_focus ((GtkWidget *) tree))
    {
        GtkWidget *w = NULL;

        if (!is_tree (tree) && priv->focusing_click)
            /* get the widget that currently has the focus */
            w = gtk_window_get_focus ((GtkWindow *) gtk_widget_get_toplevel (
                        (GtkWidget *) tree));

        gtk_widget_grab_focus ((GtkWidget *) tree);

        /* we "skip" the click if list w/ focusing_click, unless the widget that
         * had the focus was a children of ours, i.e. a column header */
        if (!is_tree (tree) && priv->focusing_click && w
                && gtk_widget_get_ancestor (w,
                        DONNA_TYPE_TREE_VIEW) != (GtkWidget *) tree)
            return FALSE;
    }

    x = (gint) event->x;
    y = (gint) event->y;

    /* event->window == bin_window, so ready for use with the is_blank()
     * functions. For get_context() however we need widget coords */
    gtk_tree_view_convert_bin_window_to_widget_coords (treev, x, y, &x, &y);

    /* it will also convert x & y (back) to bin_window coords */
    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, 0,
                &model, NULL, &iter))
    {
#ifdef GTK_IS_JJK
        if (gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
            handle_click (tree, click, event, &iter, column, renderer, CLICK_ON_BLANK);
        else
        {
            DonnaNode *node;
            struct active_spinners *as = NULL;
            guint as_idx;
            guint i;

            gtk_tree_model_get (model, &iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (!node)
                /* prevent clicking/selecting a fake node */
                return TRUE;

#ifdef GTK_IS_JJK
            if (!renderer)
            {
                /* i.e. clicked on an expander */
                handle_click (tree, click, event, &iter, column, renderer,
                        CLICK_ON_EXPANDER);
                return TRUE;
            }
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
#endif
                as = get_as_for_node (tree, node, &as_idx, FALSE);

            if (!as)
            {
                handle_click (tree, click, event, &iter, column, renderer,
                        CLICK_REGULAR);
                return TRUE;
            }

            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;
                GSList *list;
                GString *str;
                guint j;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                str = g_string_new (NULL);
                for (j = 0; j < as_col->tasks->len; )
                {
                    DonnaTask *task = as_col->tasks->pdata[j];

                    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                    {
                        const GError *err;

                        if (str->len > 0)
                            g_string_append_c (str, '\n');

                        err = donna_task_get_error (task);
                        if (err)
                            g_string_append (str, err->message);
                        else
                            g_string_append (str, "Task failed, no error message");

                        /* this will get the last task in the array to j,
                         * hence no need to move/increment j */
                        g_ptr_array_remove_index_fast (as_col->tasks, j);

                        /* can we remove the as_col? */
                        if (as_col->nb == 0 && as_col->tasks->len == 0)
                        {
                            /* can we remove the whole as? */
                            if (as->as_cols->len == 1)
                                g_ptr_array_remove_index_fast (priv->active_spinners,
                                        as_idx);
                            else
                                g_array_remove_index_fast (as->as_cols, i);

                            break;
                        }
                    }
                    else
                        /* move to next task */
                        ++j;
                }

                if (str->len > 0)
                {
                    GError *err = NULL;
                    gchar *fl = donna_node_get_full_location (node);

                    g_set_error (&err, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                            "%s", str->str);
                    donna_app_show_error (priv->app, err,
                            "Treeview '%s': Error occured on '%s'",
                            priv->name, fl);
                    g_free (fl);
                    g_error_free (err);
                }
                g_string_free (str, TRUE);

                /* we can safely assume we found/removed a task, so a refresh
                 * is in order for every row of this node */
                list = g_hash_table_lookup (priv->hashtable, node);
                for ( ; list; list = list->next)
                {
                    GtkTreeIter *iter = list->data;
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }

                g_object_unref (node);
                return TRUE;
            }
            g_object_unref (node);
            /* there was no as for this column */
            handle_click (tree, click, event, &iter, column, renderer, CLICK_REGULAR);
        }
    }
    else
        handle_click (tree, click, event, NULL, NULL, NULL, CLICK_ON_BLANK);
    return TRUE;
}

static gboolean
slow_expired_cb (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    g_source_remove (priv->last_event_timeout);
    priv->last_event_timeout = 0;
    gdk_event_free ((GdkEvent *) priv->last_event);
    priv->last_event = NULL;
    priv->last_event_expired = FALSE;

    return FALSE;
}

static gboolean
single_click_cb (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gint delay;

    /* single click it is */

    g_source_remove (priv->last_event_timeout);
    priv->last_event_expired = TRUE;
    /* timeout for slow dbl click. If triggered, we can free last_event */
    g_object_get (gtk_settings_get_default (),
            "gtk-double-click-time",    &delay,
            NULL);
    priv->last_event_timeout = g_timeout_add (delay,
            (GSourceFunc) slow_expired_cb, tree);

    /* see button_press_event below for more about this */
    if (priv->last_event->button != 1
            || (priv->last_event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
        trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);

    return FALSE;
}

static gboolean
donna_tree_view_button_press_event (GtkWidget      *widget,
                                    GdkEventButton *event)
{
    DonnaTreeView *tree = (DonnaTreeView *) widget;
    DonnaTreeViewPrivate *priv = tree->priv;
    gboolean set_up_as_last = FALSE;
    gboolean just_focused;

    /* if app's main window just got focused, we ignore this click */
    g_object_get (priv->app, "just-focused", &just_focused, NULL);
    if (just_focused)
    {
        g_object_set (priv->app, "just-focused", FALSE, NULL);
        return TRUE;
    }

    if (event->window != gtk_tree_view_get_bin_window ((GtkTreeView *) widget)
            || event->type != GDK_BUTTON_PRESS)
        return GTK_WIDGET_CLASS (donna_tree_view_parent_class)
            ->button_press_event (widget, event);

    if (priv->renderer_editable)
    {
        /* we abort the editing -- we just do this, because our signal handlers
         * for remove-widget will take care of removing handlers and whatnot */
        g_object_set (priv->renderer_editable, "editing-canceled", TRUE, NULL);
        gtk_cell_editable_editing_done (priv->renderer_editable);
        gtk_cell_editable_remove_widget (priv->renderer_editable);
        if (priv->focusing_click && event->button == 1
                && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            /* this is a focusing click, don't process it further */
            return TRUE;
    }

    if (!priv->last_event)
        set_up_as_last = TRUE;
    else if (priv->last_event_expired)
    {
        priv->last_event_expired = FALSE;
        /* since it's expired, there is a timeout, and we should remove it no
         * matter if it is a slow-double click or not */
        g_source_remove (priv->last_event_timeout);
        priv->last_event_timeout = 0;

        if (priv->last_event->button == event->button)
        {
            gint distance;

            /* slow-double click? */

            g_object_get (gtk_settings_get_default (),
                    "gtk-double-click-distance",    &distance,
                    NULL);

            if ((ABS (event->x - priv->last_event->x) <= distance)
                    && ABS (event->y - priv->last_event->y) <= distance)
                /* slow-double click it is */
                trigger_click (tree, DONNA_CLICK_SLOW_DOUBLE, event);
            else
                /* just a new click */
                set_up_as_last = TRUE;
        }
        else
            /* new click */
            set_up_as_last = TRUE;

        gdk_event_free ((GdkEvent *) priv->last_event);
        priv->last_event = NULL;
    }
    else
    {
        /* since it's not expired, there is a timeout (for single-click), and we
         * should remove it no matter if it is a double click or not */
        g_source_remove (priv->last_event_timeout);
        priv->last_event_timeout = 0;

        if (priv->last_event->button == event->button)
        {
            gint distance;

            /* double click? */

            g_object_get (gtk_settings_get_default (),
                    "gtk-double-click-distance",    &distance,
                    NULL);

            if ((ABS (event->x - priv->last_event->x) <= distance)
                    && ABS (event->y - priv->last_event->y) <= distance)
                /* trigger event as double click */
                trigger_click (tree, DONNA_CLICK_DOUBLE, event);
            else
            {
                /* trigger last_event as single click */
                trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);
                /* and set up a new click */
                set_up_as_last = TRUE;
            }
        }
        else
        {
            /* trigger last_event as single click */
            trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);
            /* and set up new click */
            set_up_as_last = TRUE;
        }

        gdk_event_free ((GdkEvent *) priv->last_event);
        priv->last_event = NULL;
    }

    if (set_up_as_last)
    {
        gint delay;

        /* left click are processed right away, unless Ctrl and/or Shift was
         * held. This is because:
         * - the delay could give the impression of things being "slow"(er than
         *   expected)
         * - usual behavior when dbl-clicking an item is to have it selected
         *   (from the click) and then dbl-clicked
         * This way we get that, yet other (middle, right) clicks, as well as
         * when Ctrl and/or Shift is held, can have a dbl-click registered w/out
         * a click before, so e.g. one could Ctrl+dbl-click an item without
         * having the selection being affected.
         * We still set up as last event after we triggered the click, so we can
         * still handle (slow) double clicks */
        if (event->button == 1 && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            if (!trigger_click (tree, DONNA_CLICK_SINGLE, event))
                /* click wasn't processed, i.e. focusing click */
                return TRUE;


        /* first timer. store it, and wait to see what happens next */

        g_object_get (gtk_settings_get_default (),
                "gtk-double-click-time",    &delay,
                NULL);

        priv->last_event = (GdkEventButton *) gdk_event_copy ((GdkEvent *) event);
        priv->last_event_timeout = g_timeout_add (delay,
                (GSourceFunc) single_click_cb, tree);
        priv->last_event_expired = FALSE;
    }

    /* handled */
    return TRUE;
}

static gboolean
donna_tree_view_button_release_event (GtkWidget      *widget,
                                      GdkEventButton *event)
{
    gboolean ret;
    GSList *l;

    /* Note: this call will have GTK toggle (expand/collapse) a row when it was
     * double-left-clicked on an expander. It would be a PITA to avoid w/out
     * breaking other things (column resize/drag, rubber band, etc) so we leave
     * it as is.
     * After all, the left click will probably do that already, so no one in
     * their right might would really use expander-dbl-left-click for anything
     * really. (Middle/right dbl-click are fine.) */
    ret = GTK_WIDGET_CLASS (donna_tree_view_parent_class)->button_release_event (
            widget, event);

    /* because after a user resize of a column, GTK might have set the expand
     * property to TRUE which will then cause it to auto-expand on following
     * resize (of other columns or entire window), something we don't want.
     * So, to ensure our columns stay the size they are, and because there's no
     * event "release-post-column-resize" or something, we do the following:
     * After a button release on tree (which handles the column resize) we check
     * all our columns (i.e. skip our non-visible expander or blank space on the
     * right) and, if the property expand is TRUE, we set it back to FALSE. We
     * also set the fixed-width to the current width otherwise the column
     * shrinks unexpectedly. */
    for (l = ((DonnaTreeView *) widget)->priv->columns; l; l = l->next)
    {
        struct column *_col = l->data;
        gboolean expand;

        g_object_get (_col->column, "expand", &expand, NULL);
        if (expand)
            g_object_set (_col->column,
                    "expand",       FALSE,
                    "fixed-width",  gtk_tree_view_column_get_width (_col->column),
                    NULL);
    }

    return ret;
}

static gboolean
set_selection_browse (GtkTreeSelection *selection)
{
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
    return FALSE;
}

static gboolean
check_focus_widget (DonnaTreeView *tree)
{
    if (gtk_widget_is_focus ((GtkWidget *) tree))
        gtk_widget_grab_focus ((GtkWidget *) tree->priv->sync_with);
    return FALSE;
}

static void
selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter iter;

    if (!is_tree (tree))
        return;

    if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
        DonnaNode *node;

        /* might have been to SELECTION_SINGLE if there was no selection, due to
         * unsync with the list (or minitree mode) */
        if (priv->sync_mode != DONNA_TREE_SYNC_NONE
                && gtk_tree_selection_get_mode (selection) != GTK_SELECTION_BROWSE)
            /* trying to change it now causes a segfault in GTK */
            g_idle_add ((GSourceFunc) set_selection_browse, selection);

        priv->location_iter = iter;

        gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        if (priv->location != node)
        {
            if (priv->location)
                g_object_unref (priv->location);
            priv->location = node;

            if (priv->sync_with)
            {
                DonnaNode *n;

                /* should we ask the list to change its location? */
                n = donna_tree_view_get_location (priv->sync_with);
                g_object_unref (n);
                if (n == node)
                    return;

                donna_tree_view_set_location (priv->sync_with, node, NULL);
                if (priv->auto_focus_sync)
                    /* auto_focus_sync means if we have the focus, we send it to
                     * sync_with. We need to do this in a new idle source
                     * because we might be getting the focus with the selection
                     * change (i.e.  user clicked on tree while focus was
                     * elsewhere) and is_focus() is only gonna take this into
                     * account *after* this signal is processed. */
                    g_idle_add ((GSourceFunc) check_focus_widget, tree);
            }
        }
        else if (node)
            g_object_unref (node);
    }
    else if (gtk_tree_selection_get_mode (selection) != GTK_SELECTION_BROWSE)
    {
        /* if we're not in BROWSE mode anymore, it means this is the result of
         * being out of sync with our list, resulting in a temporary switch to
         * SINGLE. So, we just don't have a current location for the moment */
        if (priv->location)
        {
            g_object_unref (priv->location);
            priv->location = NULL;
            priv->location_iter.stamp = 0;
        }
    }
    else
    {
        GtkTreePath *path;

        /* if that happens while in BROWSE, this is most likely a bug or
         * something in GTK, where user could unselect w/out making a new
         * selection.
         * One way to do this is to move the focus up/outside the branch, then
         * collapse the parent of the selected node. No more selection!
         * If that happens, we select something to make it our new current
         * location, and we use the focus for that */

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (!path)
            path = gtk_tree_path_new_from_string ("0");
        gtk_tree_selection_select_path (selection, path);
        gtk_tree_path_free (path);
    }
}

static gboolean
donna_tree_view_draw (GtkWidget *w, cairo_t *cr)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (w);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = GTK_TREE_VIEW (w);
    gint x, y, width;
    GtkStyleContext *context;
    PangoLayout *layout;

    /* chain up, so the drawing actually gets done */
    GTK_WIDGET_CLASS (donna_tree_view_parent_class)->draw (w, cr);

    if (is_tree (tree) || priv->draw_state == DRAW_NOTHING)
        return FALSE;

    gtk_tree_view_convert_tree_to_widget_coords (treev, 0, 0, &x, &y);
    width = gtk_widget_get_allocated_width (w);
    context = gtk_widget_get_style_context (w);

    if (priv->draw_state == DRAW_EMPTY)
    {
        gtk_style_context_save (context);
        gtk_style_context_set_state (context, GTK_STATE_FLAG_INSENSITIVE);
    }

    layout = gtk_widget_create_pango_layout (w,
            (priv->draw_state == DRAW_WAIT)
            ? "Please wait..." : "(Location is empty)");
    pango_layout_set_width (layout, width * PANGO_SCALE);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
    gtk_render_layout (context, cr, x, y, layout);

    if (priv->draw_state == DRAW_EMPTY)
        gtk_style_context_restore (context);
    g_object_unref (layout);
    return FALSE;
}

const gchar *
donna_tree_view_get_name (DonnaTreeView *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    return tree->priv->name;
}

GtkWidget *
donna_tree_view_new (DonnaApp    *app,
                     const gchar *name)
{
    DonnaTreeViewPrivate *priv;
    DonnaTreeView        *tree;
    GtkWidget            *w;
    GtkTreeView          *treev;
    GtkTreeModel         *model;
    GtkTreeSelection     *sel;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    w = g_object_new (DONNA_TYPE_TREE_VIEW, NULL);
    treev = GTK_TREE_VIEW (w);
    gtk_widget_set_name (w, name);
    gtk_tree_view_set_fixed_height_mode (treev, TRUE);

    /* tooltip */
    g_signal_connect (G_OBJECT (w), "query-tooltip",
            G_CALLBACK (query_tooltip_cb), NULL);
    gtk_widget_set_has_tooltip (w, TRUE);

    tree        = DONNA_TREE_VIEW (w);
    priv        = tree->priv;
    priv->app   = app;
    priv->name  = g_strdup (name);

    g_debug ("load_config for new tree '%s'", priv->name);
    load_config (tree);

    if (is_tree (tree))
    {
        g_debug ("treeview '%s': setting up as tree", priv->name);
        /* store */
        priv->store = donna_tree_store_new (DONNA_TREE_NB_COLS,
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_NODE */
                G_TYPE_INT,     /* DONNA_TREE_COL_EXPAND_STATE */
                G_TYPE_BOOLEAN, /* DONNA_TREE_COL_EXPAND_FLAG */
                G_TYPE_STRING,  /* DONNA_TREE_COL_ROW_CLASS */
                G_TYPE_STRING,  /* DONNA_TREE_COL_NAME */
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_ICON */
                G_TYPE_STRING,  /* DONNA_TREE_COL_BOX */
                G_TYPE_STRING,  /* DONNA_TREE_COL_HIGHLIGHT */
                G_TYPE_STRING,  /* DONNA_TREE_COL_CLICKS */
                G_TYPE_UINT);   /* DONNA_TREE_COL_VISUALS */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        gtk_tree_view_set_enable_tree_lines (treev, TRUE);
        gtk_tree_view_set_rules_hint (treev, FALSE);
        gtk_tree_view_set_headers_visible (treev, FALSE);
    }
    else
    {
        g_debug ("treeview '%s': setting up as list", priv->name);
        /* store */
        priv->store = donna_tree_store_new (DONNA_LIST_NB_COLS,
                G_TYPE_OBJECT); /* DONNA_LIST_COL_NODE */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        gtk_tree_view_set_rules_hint (treev, TRUE);
        gtk_tree_view_set_headers_visible (treev, TRUE);
        /* to refuse reordering column past the blank column on the right */
        gtk_tree_view_set_column_drag_function (treev, col_drag_func, NULL, NULL);
    }

    g_debug ("treeview '%s': setting up filter & selection", priv->name);

    /* to show/hide .files, set Visual Filters, etc */
    donna_tree_store_set_visible_func (priv->store,
            (store_visible_fn) visible_func, tree, NULL);
    /* add to tree */
    gtk_tree_view_set_model (treev, model);
#ifdef GTK_IS_JJK
    if (is_tree (tree))
    {
        gtk_tree_view_set_row_class_column (treev, DONNA_TREE_COL_ROW_CLASS);
        gtk_tree_boxable_set_box_column ((GtkTreeBoxable *) priv->store,
                DONNA_TREE_COL_BOX);
    }
#endif

    /* selection mode */
    sel = gtk_tree_view_get_selection (treev);
    gtk_tree_selection_set_mode (sel, (is_tree (tree))
            ? GTK_SELECTION_BROWSE : GTK_SELECTION_MULTIPLE);

    g_signal_connect (G_OBJECT (sel), "changed",
            G_CALLBACK (selection_changed_cb), tree);

    return w;
}
