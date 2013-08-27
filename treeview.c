
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <gtk/gtk.h>
#include <string.h>             /* strchr(), strncmp() */
#include "treeview.h"
#include "treestore.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "statusprovider.h"
#include "macros.h"
#include "renderer.h"
#include "columntype-name.h"    /* DONNA_TYPE_COLUMNTYPE_NAME */
#include "cellrenderertext.h"
#include "colorfilter.h"
#include "size.h"
#include "provider-internal.h"
#include "closures.h"

enum
{
    PROP_0,

    PROP_APP,
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

enum spec_type
{
    SPEC_NONE        = 0,
    /* a-z */
    SPEC_LOWER      = (1 << 0),
    /* A-Z */
    SPEC_UPPER      = (1 << 1),
    /* 0-9 */
    SPEC_DIGITS     = (1 << 2),
    /* anything translating to a character in SPEC_EXTRA_CHARS (below) */
    SPEC_EXTRA      = (1 << 3),

    /* key of type motion (can obviously not be combined w/ anything else) */
    SPEC_MOTION     = (1 << 9),
};
#define SPEC_EXTRA_CHARS "*+=-[](){}<>'\"|&~@$_"

enum key_type
{
    /* key does nothing */
    KEY_DISABLED = 0,
    /* gets an extra spec (can't be MOTION) for following action */
    KEY_COMBINE,
    /* direct trigger */
    KEY_DIRECT,
    /* key takes a spec */
    KEY_SPEC,
    /* key is "aliased" to another one */
    KEY_ALIAS,
};

enum changed_on
{
    STATUS_CHANGED_ON_KEYMODE   = (1 << 0),
    STATUS_CHANGED_ON_KEYS      = (1 << 1),
    STATUS_CHANGED_ON_CONTENT   = (1 << 2),
};

/* because changing location for List is a multi-step process */
enum cl
{
    /* we're not changing location */
    CHANGING_LOCATION_NOT = 0,
    /* the get_children() task has been started */
    CHANGING_LOCATION_ASKED,
    /* the timeout was triggered (DRAW_WAIT) */
    CHANGING_LOCATION_SLOW,
    /* we've received nodes from new-child signal (e.g. search results) */
    CHANGING_LOCATION_GOT_CHILD
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
    gulong           sid_node_deleted;
    gulong           sid_node_removed_from;
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

/* when filters use columns not loaded/used in tree */
struct column_filter
{
    gchar           *name;
    DonnaColumnType *ct;
    gpointer         ct_data;
};

/* status in statusbar */
struct status
{
    guint            id;
    enum changed_on  changed_on;
    gchar           *fmt;
    /* keep the name, so we can load keymode_colors options. We don't "preload"
     * them because we don't know which keymodes exists, so it's simpler that
     * way */
    gchar           *name;
    /* keymode color options */
    gboolean         keymode_colors;
    /* size options */
    gint             digits;
    gboolean         long_unit;
};


struct _DonnaTreeViewPrivate
{
    DonnaApp            *app;
    gulong               option_set_sid;
    gulong               option_deleted_sid;

    /* tree name */
    gchar               *name;

    /* tree store */
    DonnaTreeStore      *store;
    /* tree only -- see row_has_child_toggled_cb() and remove_row_from_tree() */
    gulong               row_has_child_toggled_sid;
    /* list of struct column */
    GSList              *columns;
    /* not in list above
     * list: empty column on the right
     * tree: non-visible column used as select-highlight-column when UNDERLINE */
    GtkTreeViewColumn   *blank_column;
    /* list of struct column_filter */
    GSList              *columns_filter;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* main column is the one where the SELECT_HIGHLIGHT_COLUMN effect is
     * applied to. In mode tree it's also the expander one (in list expander is
     * hidden) */
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

    /* List: last get_children task, if we need to cancel it. This is list-only
     * because in tree we don't abort the last get_children when we start a new
     * one, plus aborting one would require a lot more (remove any already added
     * child, reset expand state, etc) */
    DonnaTask           *get_children_task;
    /* List: future location (task get_children running) */
    DonnaNode           *future_location;
    /* List: extra info if the change_location if a move inside our history */
    DonnaHistoryDirection future_history_direction;
    guint                 future_history_nb;
    /* duplicatable task to get_children -- better than get doing a get_children
     * for e.g. search results, to keep the same workdir, etc */
    DonnaTask           *location_task;
    /* which step are we in the changing of location */
    enum cl              cl;

    /* List: history */
    DonnaHistory        *history;

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
    gulong               sid_treeview_loaded;

    /* info about last event, used to handle single, double & slow-dbl clicks */
    GdkEventButton      *last_event;
    guint                last_event_timeout; /* it was a single-click */
    gboolean             last_event_expired; /* after sgl-clk, could get a slow-dbl */
    /* in case the trigger must happen on button-release instead */
    DonnaClick           on_release_click;
    /* used to make sure the release is within distance of the press */
    gint                 on_release_x;
    gint                 on_release_y;
    /* because middle/right click have a delay, and release could happen before
     * the timeout for the click is triggered */
    gboolean             on_release_triggered;
    /* info to handle the keys */
    gchar               *key_mode;          /* current key mode */
    gchar               *key_combine_name;  /* combine that was used */
    gchar                key_combine;       /* combine that was pressed */
    gchar                key_combine_spec;  /* the spec from the combine */
    enum spec_type       key_spec_type;     /* spec we're waiting for */
    guint                key_m;             /* key modifier */
    guint                key_val;           /* (main) key pressed */
    guint                key_motion_m;      /* motion modifier */
    guint                key_motion;        /* motion's key */
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

    /* statuses for statusbar */
    GArray              *statuses;
    guint                last_status_id;

    /* ignore any & all node-updated signals */
    guint                refresh_on_hold    : 1;
    /* when filling list, some things can be disabled; e.g. check_statuses()
     * will not be triggered when adding nodes, etc */
    guint                filling_list       : 1;
    /* tree is switching selection mode (see selection_changed_cb()) */
    guint                changing_sel_mode  : 1;
    /* "cached" options */
    guint                mode               : 1;
    guint                node_types         : 2;
    guint                show_hidden        : 1;
    guint                sort_groups        : 2; /* containers (always) first/mixed */
    guint                select_highlight   : 2; /* only used if GTK_IS_JJK */
    /* mode Tree */
    guint                is_minitree        : 1;
    guint                sync_mode          : 3;
    guint                sync_scroll        : 1;
    guint                auto_focus_sync    : 1;
    /* mode List */
    guint                draw_state         : 2;
    guint                focusing_click     : 1;
    guint                ln_relative        : 1; /* line column: relative number */
    guint                ln_relative_focused: 1; /* line column: relative when focused */
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
enum
{
    TREE_COL_TREE,          /* mode tree, clicks */
    TREE_COL_LIST,          /* mode list */
    TREE_COL_LIST_SELECTED, /* mode list, selected */
};
gchar *_donna_config_get_string_tree_column (DonnaConfig   *config,
                                             const gchar   *tv_name,
                                             const gchar   *col_name,
                                             guint          tree_col,
                                             const gchar   *arr_name,
                                             const gchar   *def_cat,
                                             const gchar   *opt_name,
                                             gchar         *def_val);
gboolean
_donna_config_get_boolean_tree_column (DonnaConfig   *config,
                                       const gchar   *tv_name,
                                       const gchar   *col_name,
                                       guint          tree_col,
                                       const gchar   *arr_name,
                                       const gchar   *def_cat,
                                       const gchar   *opt_name,
                                       gboolean      *ret);

/* internal from app.c */
gboolean _donna_app_filter_nodes (DonnaApp        *app,
                                  GPtrArray       *nodes,
                                  const gchar     *filter_str,
                                  get_ct_data_fn   get_ct_data,
                                  gpointer         data,
                                  GError         **error);

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
static gboolean is_row_accessible                       (DonnaTreeView   *tree,
                                                         GtkTreeIter     *iter);
static struct active_spinners * get_as_for_node         (DonnaTreeView   *tree,
                                                         DonnaNode       *node,
                                                         guint           *index,
                                                         gboolean         create);
static gboolean change_location                         (DonnaTreeView  *tree,
                                                         enum cl         cl,
                                                         DonnaNode      *node,
                                                         gpointer        data,
                                                         GError        **error);
static inline void scroll_to_iter                       (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean scroll_to_current                       (DonnaTreeView  *tree);
static void check_children_post_expand                  (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean maxi_expand_row                         (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean maxi_collapse_row                       (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static inline void resort_tree                          (DonnaTreeView  *tree);
static gboolean select_arrangement_accumulator      (GSignalInvocationHint  *hint,
                                                     GValue                 *return_accu,
                                                     const GValue           *return_handler,
                                                     gpointer                data);
static void check_statuses (DonnaTreeView *tree, enum changed_on changed);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);
static void free_active_spinners (struct active_spinners *as);

static gboolean donna_tree_view_button_press_event  (GtkWidget      *widget,
                                                     GdkEventButton *event);
static gboolean donna_tree_view_button_release_event(GtkWidget      *widget,
                                                     GdkEventButton *event);
static gboolean donna_tree_view_key_press_event     (GtkWidget      *widget,
                                                     GdkEventKey    *event);
#ifdef GTK_IS_JJK
static void     donna_tree_view_rubber_banding_active (
                                                     GtkTreeView    *treev);
#endif
static void     donna_tree_view_row_activated       (GtkTreeView    *treev,
                                                     GtkTreePath    *path,
                                                     GtkTreeViewColumn *column);
static gboolean donna_tree_view_test_collapse_row   (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
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
static void     donna_tree_view_set_property        (GObject        *object,
                                                     guint           prop_id,
                                                     const GValue   *value,
                                                     GParamSpec     *pspec);
static gboolean donna_tree_view_draw                (GtkWidget      *widget,
                                                     cairo_t        *cr);
static void     donna_tree_view_finalize            (GObject        *object);


/* DonnaStatusProvider */
static guint    status_provider_create_status       (DonnaStatusProvider    *sp,
                                                     gpointer                config,
                                                     GError                **error);
static void     status_provider_free_status         (DonnaStatusProvider    *sp,
                                                     guint                   id);
static const gchar * status_provider_get_renderers  (DonnaStatusProvider    *sp,
                                                     guint                   id);
static void     status_provider_render              (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkCellRenderer        *renderer);
static gboolean status_provider_set_tooltip         (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkTooltip             *tooltip);

/* DonnaColumnType */
static const gchar * columntype_get_name            (DonnaColumnType    *ct);
static const gchar * columntype_get_renderers       (DonnaColumnType    *ct);
static DonnaColumnTypeNeed columntype_refresh_data  (DonnaColumnType  *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void         columntype_free_data            (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *  columntype_get_props            (DonnaColumnType    *ct,
                                                     gpointer            data);


#ifndef GTK_IS_JJK
static void selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree);

/* this isn't really the same at all, because the patched version in GTK allows
 * to set the focus without affecting the selection or scroll. Here we have to
 * use set_cursor() to set the focus, and that can trigger some minimum
 * scrolling.
 * We try to "undo" it, but let's be clear: the patched version is obviously
 * much better. */
void
gtk_tree_view_set_focused_row (GtkTreeView *treev, GtkTreePath *path)
{
    GtkTreeSelection *sel;
    GtkTreePath *p;
    gint y;
    gboolean scroll;

    sel = gtk_tree_view_get_selection (treev);
    scroll = gtk_tree_view_get_path_at_pos (treev, 0, 0, &p, NULL, NULL, &y);

    if (is_tree ((DonnaTreeView *) treev))
    {
        GtkSelectionMode mode;
        GtkTreeIter iter;

        if (gtk_tree_selection_get_selected (sel, NULL, &iter))
            g_signal_handlers_block_by_func (sel, selection_changed_cb, treev);
        else
            iter.stamp == 0;

        mode = gtk_tree_selection_get_mode (sel);
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
        gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
        gtk_tree_selection_set_mode (sel, mode);
        priv->changing_sel_mode = FALSE;
        if (iter.stamp != 0)
        {
            gtk_tree_selection_select_iter (sel, &iter);
            g_signal_handlers_unblock_by_func (sel, selection_changed_cb, treev);
        }
    }
    else
    {
        GList *list, *l;

        list = gtk_tree_selection_get_selected_rows (sel, NULL);
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
        gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
        priv->changing_sel_mode = FALSE;
        for (l = list; l; l = l->next)
            gtk_tree_selection_select_path (sel, l->data);
        g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
    }

    if (scroll)
    {
        gtk_tree_view_scroll_to_cell (treev, p, NULL, TRUE, 0.0, 0.0);
        if (y != 0)
        {
            gint x; /* useless, but we need to send another gint* */
            gint new_y;

            gtk_tree_view_convert_bin_window_to_tree_coords (treev, 0, 0,
                    &x, &new_y);
            gtk_tree_view_scroll_to_point (treev, -1, new_y + y);
        }
        gtk_tree_path_free (p);
    }
}
#endif

static void
donna_tree_view_status_provider_init (DonnaStatusProviderInterface *interface)
{
    interface->create_status    = status_provider_create_status;
    interface->free_status      = status_provider_free_status;
    interface->get_renderers    = status_provider_get_renderers;
    interface->render           = status_provider_render;
    interface->set_tooltip      = status_provider_set_tooltip;
}

static void
donna_tree_view_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name         = columntype_get_name;
    interface->get_renderers    = columntype_get_renderers;
    interface->refresh_data     = columntype_refresh_data;
    interface->free_data        = columntype_free_data;
    interface->get_props        = columntype_get_props;
}


G_DEFINE_TYPE_WITH_CODE (DonnaTreeView, donna_tree_view, GTK_TYPE_TREE_VIEW,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_STATUS_PROVIDER,
            donna_tree_view_status_provider_init)
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE,
            donna_tree_view_column_type_init));

static void
donna_tree_view_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;
    GtkWidgetClass *w_class;
    GObjectClass *o_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
#ifdef GTK_IS_JJK
    tv_class->rubber_banding_active = donna_tree_view_rubber_banding_active;
#endif
    tv_class->row_activated         = donna_tree_view_row_activated;
    tv_class->row_expanded          = donna_tree_view_row_expanded;
    tv_class->row_collapsed         = donna_tree_view_row_collapsed;
    tv_class->test_collapse_row     = donna_tree_view_test_collapse_row;
    tv_class->test_expand_row       = donna_tree_view_test_expand_row;

    w_class = GTK_WIDGET_CLASS (klass);
    w_class->draw = donna_tree_view_draw;
    w_class->button_press_event = donna_tree_view_button_press_event;
    w_class->button_release_event = donna_tree_view_button_release_event;
    w_class->key_press_event = donna_tree_view_key_press_event;

    o_class = G_OBJECT_CLASS (klass);
    o_class->get_property   = donna_tree_view_get_property;
    o_class->set_property   = donna_tree_view_set_property;
    o_class->finalize       = donna_tree_view_finalize;

    donna_tree_view_props[PROP_APP] =
        g_param_spec_object ("app", "app",
                "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

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
free_status (struct status *status)
{
    g_free (status->fmt);
    g_free (status->name);
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
    priv->statuses = g_array_new (FALSE, FALSE, sizeof (struct status));
    g_array_set_clear_func (priv->statuses, (GDestroyNotify) free_status);
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
    if (ps->sid_node_deleted)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_deleted);
    if (ps->sid_node_removed_from)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_removed_from);
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
    else if (prop_id == PROP_APP)
        g_value_set_object (value, priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_tree_view_set_property (GObject        *object,
                              guint           prop_id,
                              const GValue   *value,
                              GParamSpec     *pspec)
{
    DonnaTreeViewPrivate *priv = DONNA_TREE_VIEW (object)->priv;

    if (prop_id == PROP_APP)
        priv->app = g_value_get_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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

static void
free_column_filter (struct column_filter *col)
{
    g_free (col->name);
    donna_columntype_free_data (col->ct, col->ct_data);
    g_object_unref (col->ct);
    g_free (col);
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
    g_slist_free_full (priv->columns_filter, (GDestroyNotify) free_column_filter);
    if (priv->tree_visuals)
        g_hash_table_foreach_remove (priv->tree_visuals,
                (GHRFunc) free_tree_visuals, NULL);
    g_array_free (priv->statuses, TRUE);

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
        if (node)
            g_object_unref (node);
        return;
    }
    else if (!node)
        return;

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

        /* this beauty will put focus & select the row, without doing any
         * scrolling whatsoever. What a wonderful thing! :) */
        /* Note: that's true when GTK_IS_JJK; if not we do provide a replacement
         * for set_focused_row() that should get the same results, though much
         * less efficiently. */
        gtk_tree_view_set_focused_row (treev, path);
        gtk_tree_selection_select_path (sel, path);
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
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
        priv->changing_sel_mode = FALSE;
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
                gtk_tree_view_set_focused_row (treev, path);
                gtk_tree_path_free (path);

                if (priv->sync_scroll)
                    scroll_to_iter (tree, iter);
            }
        }
    }

    priv->future_location_iter.stamp = 0;
    g_object_unref (node);
    /* it might have already happened on selection change, but this might have
     * not changed the selection, only the focus (if anything), so: */
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
}

static void
active_list_changed_cb (GObject         *object,
                        GParamSpec      *pspec,
                        DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (priv->sync_with)
    {
        if (priv->sid_sw_location_changed)
            g_signal_handler_disconnect (priv->sync_with,
                    priv->sid_sw_location_changed);
        g_object_unref (priv->sync_with);
    }
    g_object_get (object, "active-list", &priv->sync_with, NULL);
    priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
            "notify::location",
            G_CALLBACK (sync_with_location_changed_cb), tree);

    sync_with_location_changed_cb ((GObject *) priv->sync_with, NULL, tree);
}

/* mode list only */
static inline void
set_get_children_task (DonnaTreeView *tree, DonnaTask *task)
{
    if (tree->priv->get_children_task)
    {
        if (!(donna_task_get_state (tree->priv->get_children_task) & DONNA_TASK_POST_RUN))
            donna_task_cancel (tree->priv->get_children_task);
        g_object_unref (tree->priv->get_children_task);
    }
    tree->priv->get_children_task = g_object_ref (task);
}

enum
{
    OPT_NONE = 0,
    OPT_DEFAULT,
    OPT_TREEVIEW,
    OPT_TREEVIEW_COLUMN,
    OPT_COLUMN,
};

struct option_data
{
    DonnaTreeView *tree;
    gchar *option;
    guint opt;
    gssize len;
};

static gboolean
reset_node_visuals (GtkTreeModel    *model,
                    GtkTreePath     *path,
                    GtkTreeIter     *iter,
                    DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaTreeVisual visual;
    DonnaNode *node;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_VISUALS, &visual,
            DONNA_TREE_COL_NODE,    &node,
            -1);

    if (!node)
        /* keep iterating */
        return FALSE;

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_NAME)
            && !(visual & DONNA_TREE_VISUAL_NAME))
        donna_tree_store_set (tree->priv->store, iter,
                DONNA_TREE_COL_NAME,        NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_ICON)
            && !(visual & DONNA_TREE_VISUAL_ICON))
        donna_tree_store_set (tree->priv->store, iter,
                DONNA_TREE_COL_ICON,        NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_BOX)
            && !(visual & DONNA_TREE_VISUAL_BOX))
        donna_tree_store_set (tree->priv->store, iter,
                DONNA_TREE_COL_BOX,         NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_HIGHLIGHT)
            && !(visual & DONNA_TREE_VISUAL_HIGHLIGHT))
        donna_tree_store_set (tree->priv->store, iter,
                DONNA_TREE_COL_HIGHLIGHT,   NULL,
                -1);

    load_node_visuals (tree, iter, node, TRUE);
    g_object_unref (node);

    /* keep iterating */
    return FALSE;
}

static gboolean
switch_minitree_off (GtkTreeModel    *model,
                     GtkTreePath     *path,
                     GtkTreeIter     *iter,
                     DonnaTreeView   *tree)
{
    enum tree_expand es;

    gtk_tree_model_get (model, iter, DONNA_TREE_COL_EXPAND_STATE, &es, -1);
    if (es == DONNA_TREE_EXPAND_PARTIAL)
    {
        if (gtk_tree_view_row_expanded ((GtkTreeView *) tree, path))
            maxi_expand_row (tree, iter);
        else
            maxi_collapse_row (tree, iter);
    }

    /* keep iterating */
    return FALSE;
}

static gint
config_get_int (DonnaTreeView   *tree,
                DonnaConfig     *config,
                const gchar     *option,
                gint             def)
{
    gint val;

    if (donna_config_get_int (config, &val, "treeviews/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_int (config, &val, "defaults/treeviews/%s/%s",
                (is_tree (tree)) ? "tree" : "list", option))
        return val;
    g_warning ("Treeview '%s': option 'defaults/treeviews/%s/%s' not found, setting default (%d)",
            tree->priv->name, (is_tree (tree)) ? "tree" : "list", option, def);
    donna_config_set_int (config, def, "defaults/treeviews/%s/%s",
            (is_tree (tree)) ? "tree" : "list", option);
    return def;
}

static gboolean
config_get_boolean (DonnaTreeView   *tree,
                    DonnaConfig     *config,
                    const gchar     *option,
                    gboolean         def)
{
    gboolean val;

    if (donna_config_get_boolean (config, &val, "treeviews/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_boolean (config, &val, "defaults/treeviews/%s/%s",
                (is_tree (tree)) ? "tree" : "list", option))
        return val;
    g_warning ("Treeview '%s': option 'defaults/treeviews/%s/%s' not found, setting default (%d)",
            tree->priv->name, (is_tree (tree)) ? "tree" : "list", option, def);
    donna_config_set_boolean (config, def, "defaults/treeviews/%s/%s",
            (is_tree (tree)) ? "tree" : "list", option);
    return def;
}

static gchar *
config_get_string (DonnaTreeView   *tree,
                   DonnaConfig     *config,
                   const gchar     *option,
                   gchar           *def)
{
    gchar *val;

    if (donna_config_get_string (config, &val, "treeviews/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_string (config, &val, "defaults/treeviews/%s/%s",
                (is_tree (tree)) ? "tree" : "list", option))
        return val;
    if (!def)
        return NULL;
    g_warning ("Treeview '%s': option 'defaults/treeviews/%s/%s' not found, setting default (%s)",
            tree->priv->name, (is_tree (tree)) ? "tree" : "list", option, def);
    donna_config_set_string (config, def, "defaults/treeviews/%s/%s",
            (is_tree (tree)) ? "tree" : "list", option);
    return def;
}

#define cfg_get_mode(t,c) \
    CLAMP (config_get_int (t, c, "mode", DONNA_TREE_VIEW_MODE_LIST), 0, 1)
#define cfg_get_show_hidden(t,c) \
    config_get_boolean (t, c, "show_hidden", TRUE)
#define cfg_get_node_types(t,c) \
    CLAMP (config_get_int (t, c, "node_types", \
                (is_tree (t)) ? DONNA_NODE_CONTAINER \
                : DONNA_NODE_CONTAINER | DONNA_NODE_ITEM), \
                0, 3)
#define cfg_get_sort_groups(t,c) \
    CLAMP (config_get_int (t, c, "sort_groups", SORT_CONTAINER_FIRST), 0, 2)
#ifdef GTK_IS_JJK
#define cfg_get_select_highlight(t,c) \
    CLAMP (config_get_int (t, c, "select_highlight", \
                (is_tree (tree)) ? SELECT_HIGHLIGHT_COLUMN \
                : SELECT_HIGHLIGHT_COLUMN_UNDERLINE), 0, 3)
#else
#define cfg_get_select_highlight(t,c) SELECT_HIGHLIGHT_FULL_ROW
#endif
#define cfg_get_node_visuals(t,c) \
    CLAMP (config_get_int (t, c, "node_visuals", DONNA_TREE_VISUAL_NOTHING), 0, 31)
#define cfg_get_is_minitree(t,c) \
    config_get_boolean (t, c, "is_minitree", FALSE)
#define cfg_get_sync_mode(t,c) \
    CLAMP (config_get_int (t, c, "sync_mode", DONNA_TREE_SYNC_FULL), 0, 4)
#define cfg_get_sync_with(t,c) \
    config_get_string (t, c, "sync_with", NULL)
#define cfg_get_sync_scroll(t,c) \
    config_get_boolean (t, c, "sync_scroll", TRUE)
#define cfg_get_auto_focus_sync(t,c) \
    config_get_boolean (t, c, "auto_focus_sync", TRUE)
#define cfg_get_focusing_click(t,c) \
    config_get_boolean (t, c, "focusing_click", TRUE)
#define cfg_get_history_max(t,c) \
    config_get_int (t, c, "history_max", 100)

static gboolean
real_option_cb (struct option_data *data)
{
    DonnaTreeView *tree = data->tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    gchar *opt;
    gchar *s;

    config = donna_app_peek_config (priv->app);
    opt = data->option + data->len;

    if (data->opt == OPT_TREEVIEW || data->opt == OPT_DEFAULT)
    {
        gint val;

        /* treeview option */

        if (streq (opt, "mode"))
        {
            val = cfg_get_mode (tree, config);
            if (priv->mode != val)
            {
                donna_app_show_error (priv->app, NULL,
                        "Treeview '%s': option 'mode' was changed; Please restart the application to have it applied.",
                        priv->name);
            }
        }
        else if (streq (opt, "show_hidden"))
        {
            val = cfg_get_show_hidden (tree, config);
            if (priv->show_hidden != val)
            {
                priv->show_hidden = val;
                donna_tree_store_refilter (priv->store, NULL);
                check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
            }
        }
        else if (streq (opt, "node_types"))
        {
            val = cfg_get_node_types (tree, config);
            if (priv->node_types != val)
            {
                priv->node_types = val;
                donna_tree_view_refresh (data->tree,
                        DONNA_TREE_REFRESH_RELOAD, NULL);
            }
        }
        else if (streq (opt, "sort_groups"))
        {
            val = cfg_get_sort_groups (tree, config);
            if (priv->sort_groups != val)
            {
                priv->sort_groups = val;
                resort_tree (data->tree);
            }
        }
        else if (streq (opt, "select_highlight"))
        {
            val = cfg_get_select_highlight (tree, config);
            if (priv->select_highlight != val)
            {
                GtkTreeView *treev = (GtkTreeView *) data->tree;

                priv->select_highlight = val;
                if (priv->select_highlight == SELECT_HIGHLIGHT_COLUMN
                        || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE)
                    gtk_tree_view_set_select_highlight_column (treev, priv->main_column);
                else if (priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE)
                {
                    /* since we only want an underline, we must set the select highlight
                     * column to a non-visible one */
                    if (is_tree (data->tree))
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
                        gtk_tree_view_set_select_highlight_column (treev,
                                gtk_tree_view_get_expander_column (treev));
                }
                else
                    gtk_tree_view_set_select_highlight_column (treev, NULL);

                gtk_tree_view_set_select_row_underline (treev,
                        priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE
                        || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE);

                gtk_widget_queue_draw ((GtkWidget *) treev);
            }
        }
        else if (is_tree (data->tree))
        {
            if (streq (opt, "node_visuals"))
            {
                val = cfg_get_node_visuals (tree, config);
                if (priv->node_visuals != (guint) val)
                {
                    priv->node_visuals = (guint) val;
                    gtk_tree_model_foreach ((GtkTreeModel *) priv->store,
                            (GtkTreeModelForeachFunc) reset_node_visuals,
                            data->tree);
                }
            }
            else if (streq (opt, "is_minitree"))
            {
                val = cfg_get_is_minitree (tree, config);
                if (priv->is_minitree != val)
                {
                    priv->is_minitree = val;
                    if (!val)
                    {
                        gtk_tree_model_foreach ((GtkTreeModel *) priv->store,
                                (GtkTreeModelForeachFunc) switch_minitree_off,
                                data->tree);
                        g_idle_add ((GSourceFunc) scroll_to_current, data->tree);
                    }
                }
            }
            else if (streq (opt, "sync_mode"))
            {
                val = cfg_get_sync_mode (tree, config);
                if (priv->sync_mode != val)
                {
                    priv->sync_mode = val;
                    if (priv->sync_with)
                        sync_with_location_changed_cb ((GObject *) priv->sync_with,
                                NULL, data->tree);
                }
            }
            else if (streq (opt, "sync_with"))
            {
                DonnaTreeView *sw;

                s = cfg_get_sync_with (tree, config);
                if (streq (s, ":active"))
                    g_object_get (priv->app, "active-list", &sw, NULL);
                else if (s)
                    sw = donna_app_get_treeview (priv->app, s);
                else
                    sw = NULL;
                g_free (s);
                if (priv->sync_with != sw)
                {
                    if (priv->sync_with)
                    {
                        g_signal_handler_disconnect (priv->sync_with,
                                priv->sid_sw_location_changed);
                        g_object_unref (priv->sync_with);
                    }
                    priv->sync_with = sw;
                    priv->sid_sw_location_changed = (sw)
                        ? g_signal_connect (priv->sync_with,
                                "notify::location",
                                (GCallback) sync_with_location_changed_cb,
                                data->tree)
                        : 0;

                    if (priv->sid_treeview_loaded)
                    {
                        g_signal_handler_disconnect (priv->app,
                                priv->sid_treeview_loaded);
                        priv->sid_treeview_loaded = 0;
                    }
                }
                else
                    g_object_unref (sw);
            }
            else if (streq (opt, "sync_scroll"))
            {
                val = cfg_get_sync_scroll (tree, config);
                if (priv->sync_scroll != val)
                    priv->sync_scroll = val;
            }
            else if (streq (opt, "auto_focus_sync"))
            {
                val = cfg_get_auto_focus_sync (tree, config);
                if (priv->auto_focus_sync != val)
                    priv->auto_focus_sync = val;
            }
        }
        else /* list */
        {
            if (streq (opt, "focusing_click"))
            {
                val = cfg_get_focusing_click (tree, config);
                if (priv->focusing_click != val)
                    priv->focusing_click = val;
            }
            else if (streq (opt, "history_max"))
            {
                val = cfg_get_history_max (tree, config);
                if (donna_history_get_max (priv->history) != (guint) val)
                    donna_history_set_max (priv->history, (guint) val);
            }
        }
    }
    /* columns option */
    else
    {
        s = strchr (opt, '/');
        if (s)
        {
            struct column *_col;

            /* is this change about a column we are using right now? */
            *s = '\0';
            _col = get_column_by_name (tree, opt);
            *s = '/';
            if (_col)
            {
                if (streq (s + 1, "title"))
                {
                    gchar *s = NULL;

                    /* we know we will get a value, but it might not be from the
                     * config changed that occured, since the value might be
                     * overridden by current arrangement, etc */
                    s = donna_config_get_string_column (config,
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
                    w = donna_config_get_int_column (config,
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
                        resort_tree (data->tree);
                    if (need & DONNA_COLUMNTYPE_NEED_REDRAW)
                        gtk_widget_queue_draw ((GtkWidget *) tree);
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
    guint opt = OPT_NONE;

    /* options we care about are ones for the tree (in "/treeviews/<NAME>" or
     * "/defaults/treeviews/<MODE>") or for one of our columns:
     * /treeviews/<NAME>/columns/<NAME>
     * /columns/<NAME>
     * This excludes options in the current arrangement, but that's
     * okay/expected: arrangement are loaded/"created" on location change.
     *
     * Here we can only check if the option starts with "/treeviews/<NAME>",
     * "/defaults/treeviews/<MODE>" or "/columns/" and that's it, to loop
     * through our columns we need the GTK lock, i.e. go in main thread */

    len = snprintf (buf, 255, "/treeviews/%s/", tree->priv->name);
    if (len >= 255)
        b = g_strdup_printf ("/treeviews/%s/", tree->priv->name);

    if (streqn (option, b, len))
    {
        opt = OPT_TREEVIEW;
        if (streqn (option + len, "columns/", 8))
        {
            opt = OPT_TREEVIEW_COLUMN;
            len += 8;
        }
    }
    else if (streqn (option, "/columns/", 9))
    {
        opt = OPT_COLUMN;
        len = 9;
    }

    if (b != buf)
        g_free (b);

    if (opt == OPT_NONE)
    {
        len = snprintf (buf, 255, "/defaults/treeviews/%s/",
                (is_tree (tree)) ? "tree" : "list");
        if (len >= 255)
            b = g_strdup_printf ("/defaults/treeviews/%s/",
                    (is_tree (tree)) ? "tree" : "list");
        else
            b = buf;
        if (streqn (option, b, len))
            opt = OPT_DEFAULT;
        if (b != buf)
            g_free (b);
    }

    if (opt != OPT_NONE)
    {
        struct option_data *data;

        data = g_new (struct option_data, 1);
        data->tree = tree;
        data->option = g_strdup (option);
        data->opt = opt;
        data->len = len;
        g_main_context_invoke (NULL, (GSourceFunc) real_option_cb, data);
    }
}

static void
treeview_loaded_cb (DonnaApp *app, DonnaTreeView *loaded_tree, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *s;

    s = cfg_get_sync_with (tree, donna_app_peek_config (priv->app));
    if (!priv->sync_with && streq (s, loaded_tree->priv->name))
    {
        g_signal_handler_disconnect (priv->app, priv->sid_treeview_loaded);
        priv->sid_treeview_loaded = 0;
        priv->sync_with = g_object_ref (loaded_tree);
        priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                "notify::location",
                (GCallback) sync_with_location_changed_cb, tree);
    }
    g_free (s);
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    gint val;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,deleted} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;
    config = donna_app_peek_config (priv->app);

    /* we don't use cfg_get_mode() so we can set a default if option doesn't
     * exist -- obviously, we can't go to the mode-based default */
    if (donna_config_get_int (config, &val, "treeviews/%s/mode", priv->name))
        priv->mode = CLAMP (val, 0, 1);
    else
    {
        priv->mode = DONNA_TREE_VIEW_MODE_LIST;
        g_warning ("Treeview '%s': no mode specified, defaulting to list",
                priv->name);
        donna_config_set_int (config, 0, "treeviews/%s/mode", priv->name);
    }

    val = cfg_get_show_hidden (tree, config);
    priv->show_hidden = val;

    val = cfg_get_node_types (tree, config);
    priv->node_types = val;

    val = cfg_get_sort_groups (tree, config);
    priv->sort_groups = val;

    val = cfg_get_select_highlight (tree, config);
    priv->select_highlight = val;

    if (is_tree (tree))
    {
        gchar *s;

        val = cfg_get_node_visuals (tree, config);
        priv->node_visuals = val;

        val = cfg_get_is_minitree (tree, config);
        priv->is_minitree = val;

        val = cfg_get_sync_mode (tree, config);
        priv->sync_mode = val;

        s = cfg_get_sync_with (tree, config);
        if (streq (s, ":active"))
        {
            g_object_get (priv->app, "active-list", &priv->sync_with, NULL);
            priv->sid_active_list_changed = g_signal_connect (priv->app,
                    "notify::active-list",
                    (GCallback) active_list_changed_cb, tree);
        }
        else if (s)
            priv->sync_with = donna_app_get_treeview (priv->app, s);
        g_free (s);
        if (priv->sync_with)
            priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                    "notify::location",
                    (GCallback) sync_with_location_changed_cb, tree);
        else if (s)
            priv->sid_treeview_loaded = g_signal_connect (priv->app,
                    "treeview_loaded",
                    (GCallback) treeview_loaded_cb, tree);

        val = cfg_get_sync_scroll (tree, config);
        priv->sync_scroll = val;

        val = cfg_get_auto_focus_sync (tree, config);
        priv->auto_focus_sync = val;
    }
    else
    {
        val = cfg_get_focusing_click (tree, config);
        priv->focusing_click = val;

        val = cfg_get_history_max (tree, config);
        priv->history = donna_history_new (val);
    }

    /* listen to config changes */
    priv->option_set_sid = g_signal_connect (config, "option-set",
            (GCallback) option_cb, tree);
    priv->option_deleted_sid = g_signal_connect (config, "option-deleted",
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

static void
handle_removing_row (DonnaTreeView *tree, GtkTreeIter *iter, gboolean is_focus)
{
    GtkTreeModel *model = (GtkTreeModel *) tree->priv->store;
    GtkTreeIter it = *iter;
    gboolean found = FALSE;

    /* we will move the focus/selection (current row in tree) to the next item
     * (or prev if there's no next).
     * In list, it's a simple next/prev; on tree it's the same (to try to stay
     * on the same level), then we go up. This is obviously the natural choice,
     * especially for the current location. */

    if (gtk_tree_model_iter_next (model, &it))
        found = TRUE;
    else
    {
        it = *iter;
        if (gtk_tree_model_iter_previous (model, &it))
            found= TRUE;
    }

    if (!found && is_tree (tree))
        found = gtk_tree_model_iter_parent (model, &it, iter);

    if (!is_focus)
    {
        if (found)
            gtk_tree_selection_select_iter (
                    gtk_tree_view_get_selection ((GtkTreeView *) tree), &it);
        else
        {
            GtkTreePath *path;

            if (donna_tree_model_get_count (model) == 0)
            {
                /* if there's no more rows on tree, let's make sure we don't
                 * have an old (invalid) current location */
                if (tree->priv->location)
                {
                    g_object_unref (tree->priv->location);
                    tree->priv->location = NULL;
                    tree->priv->location_iter.stamp = 0;
                }
                return;
            }
            path = gtk_tree_path_new_from_string ("0");
            gtk_tree_selection_select_path (
                    gtk_tree_view_get_selection ((GtkTreeView *) tree), path);
            gtk_tree_path_free (path);
        }
    }
    else if (found)
    {
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, &it);
        gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
        gtk_tree_path_free (path);
    }
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
    GtkTreeIter it;
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

    if (is_tree (tree))
    {
        GtkTreeIter child;

        /* this signal is used to possibly "undo" the removal of expander (see
         * row_has_child_toggled_cb() for more), this will not be that case but
         * since we're possibly removing a lot/causing a bunch of those signals,
         * might as well avoid useless processing */
        if (priv->row_has_child_toggled_sid)
            g_signal_handler_block (priv->store, priv->row_has_child_toggled_sid);

        /* get the parent, in case we're removing its last child */
        donna_tree_store_iter_parent (priv->store, &parent, iter);
        /* we need to remove all children before we remove the row, so we can
         * have said children processed correctly (through here) as well */
        if (donna_tree_store_iter_children (priv->store, &child, iter))
            while (remove_row_from_tree (tree, &child, is_removal))
                ;

        if (priv->row_has_child_toggled_sid)
            g_signal_handler_unblock (priv->store, priv->row_has_child_toggled_sid);
    }

    /* removing the row with the focus will have GTK do a set_cursor(), this
     * isn't the best of behaviors, so let's see if we can do "better" */
    if (donna_tree_model_get_count (model) > 1)
    {
        GtkTreePath *path_cursor;

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_cursor, NULL);
        if (path_cursor)
        {
            GtkTreeIter  iter_cursor;

            gtk_tree_model_get_iter (model, &iter_cursor, path_cursor);
            if (itereq (iter, &iter_cursor))
                handle_removing_row (tree, iter, TRUE);
            gtk_tree_path_free (path_cursor);
        }
    }

    /* tree: if removing the current location, let's move it */
    if (is_tree (tree) && gtk_tree_selection_get_selected (
                gtk_tree_view_get_selection ((GtkTreeView *) tree), NULL, &it)
            && itereq (iter, &it))
        handle_removing_row (tree, &it, FALSE);

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
    else if (!is_tree (tree)
            && (donna_tree_model_get_count ((GtkTreeModel *) priv->store) == 0))
        priv->draw_state = DRAW_EMPTY;

    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return ret;
}

struct refresh_data
{
    DonnaTreeView   *tree;
    GMutex           mutex;
    guint            count;
    gboolean         done;
};

/* when doing a refresh, we ask every node on tree (or every visible node for
 * DONNA_TREE_REFRESH_VISIBLE) to refresh its set properties, and we then get
 * flooded by node-updated signals.
 * In a tree w/ 800 rows/nodes, that's 800 * nb_props, so even with only 6
 * properties (name, size, time, uid, gid, mode) that's 4 800 callbacks, which
 * is a lot.
 * And apparently the slow bit that might make the UI a bit unresponsive or make
 * it slow until the refresh "appears on screen" comes from the thousands of
 * calls to gtk_tree_model_get_path() (the path being needed to call
 * gtk_tree_model_row_changed).
 * So to try and make this a bit better/feel faster, we put refresh_on_hold
 * (i.e. all signals node-updated are no-op. We don't actually block them just
 * because I'm lazy, and in tree there can be plenty of providers/handlers to
 * block/unblock. Could be better though...) and simply trigger a redraw when
 * done, to refresh only the visible rows. Much better.
 * This is done using a refresh_data with the number of tasks started, all of
 * which having this callback to decrement the counter (under lock, ofc). After
 * all tasks have been started, this function is called with no task, to set the
 * flag done to TRUE. When done is TRUE & count == 0, it means everything has
 * been processed, we can trigger the refresh & free memory */
static void
refresh_node_cb (DonnaTask              *task,
                 gboolean                timeout_called,
                 struct refresh_data    *data)
{
    g_mutex_lock (&data->mutex);
    if (task)
        --data->count;
    else
        data->done = TRUE;
    if (data->done && data->count == 0)
    {
        g_mutex_unlock (&data->mutex);
        g_mutex_clear (&data->mutex);
        data->tree->priv->refresh_on_hold = FALSE;
        gtk_widget_queue_draw ((GtkWidget *) data->tree);
        /* in case any name or size changed, since it was refresh_on_hold */
        check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
        g_free (data);
    }
    else
        g_mutex_unlock (&data->mutex);
}

static gboolean may_get_children_refresh (DonnaTreeView *tree, GtkTreeIter *iter);

static void
set_children (DonnaTreeView *tree,
              GtkTreeIter   *iter,
              GPtrArray     *children,
              gboolean       expand,
              gboolean       refresh)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    gboolean is_tree = is_tree (tree);

    if (children->len == 0)
    {
        GtkTreeIter child;

        if (is_tree)
        {
            /* set new expand state */
            set_es (priv, iter, DONNA_TREE_EXPAND_NONE);
            if (donna_tree_store_iter_children (priv->store, &child, iter))
                while (remove_row_from_tree (tree, &child, TRUE))
                    ;
        }
        else
        {
            /* clear the list (see selection_changed_cb() for why filling_list) */
            priv->filling_list = TRUE;
            donna_tree_store_clear (priv->store);
            priv->filling_list = FALSE;
            /* also the hashtable (we don't need to unref nodes (keys), as our ref was
             * handled by the store) */
            g_hash_table_remove_all (priv->hashtable);

            /* show the "location empty" message */
            priv->draw_state = DRAW_EMPTY;
            gtk_widget_queue_draw ((GtkWidget *) tree);
        }
    }
    else
    {
        GSList *list = NULL;
        struct refresh_data *data;
        guint nb_real;
        enum tree_expand es;
        guint i;
        gboolean has_children = !is_tree;

        if (is_tree)
            gtk_tree_model_get (model, iter,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
        else
        {
            if (refresh)
            {
                /* see refresh_node_cb() for more about this */
                data = g_new (struct refresh_data, 1);
                data->tree = tree;
                g_mutex_init (&data->mutex);
                data->count = children->len;
                data->done = FALSE;
                priv->refresh_on_hold = TRUE;
                nb_real = 0;
            }

            es = DONNA_TREE_EXPAND_MAXI;
        }

        if (es == DONNA_TREE_EXPAND_MAXI || es == DONNA_TREE_EXPAND_PARTIAL)
        {
            GtkTreeIter i;

            if (donna_tree_store_iter_children (priv->store, &i, iter))
            {
                do
                {
                    list = g_slist_prepend (list, gtk_tree_iter_copy (&i));
                } while (donna_tree_store_iter_next (priv->store, &i));
                list = g_slist_reverse (list);
            }
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

            /* in tree, we want to just call add_node_to_tree(), which will
             * check if there's already a row for the given parent, if so set it
             * in &row else set the newly added one.
             * But, in list (i.e. parent (here iter) == NULL), this check
             * doesn't happen. But since we don't wanna add what's already
             * there, we need to check it first. */
            row.stamp = 0;
            if (!is_tree)
            {
                GSList *l;

                l = g_hash_table_lookup (priv->hashtable, node);
                if (l)
                    row = * (GtkTreeIter *) l->data;
            }

            /* add_node_to_tree() shouldn't be able to fail/return FALSE */
            if (row.stamp == 0 && !add_node_to_tree (tree, iter, node, &row))
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

                if (refresh
                        && donna_tree_store_iter_is_visible (priv->store, &row))
                {
                    if (is_tree)
                        may_get_children_refresh (tree, &row);
                    else
                    {
                        DonnaTask *task;

                        ++nb_real;
                        task = donna_node_refresh_task (node,
                                DONNA_NODE_REFRESH_SET_VALUES, NULL);
                        donna_task_set_callback (task,
                                (task_callback_fn) refresh_node_cb,
                                data, NULL);
                        donna_app_run_task (priv->app, task);
                    }
                }

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

        if (is_tree)
        {
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

                /* and make sure the row gets expanded (since we "blocked" it
                 * when clicked */
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
                gtk_tree_path_free (path);
            }
        }
        else
        {
            if (children->len > 0)
            {
                if (priv->draw_state == DRAW_EMPTY)
                {
                    GtkWidget *w;

                    priv->draw_state = DRAW_NOTHING;
                    /* we give the treeview the focus, to ensure the focused row
                     * is set, hence the class focused-row applied */
                    w = gtk_widget_get_toplevel ((GtkWidget *) tree);
                    w = gtk_window_get_focus ((GtkWindow *) w);
                    gtk_widget_grab_focus ((GtkWidget *) tree);
                    gtk_widget_grab_focus ((w) ? w : (GtkWidget *) tree);
                }
            }
            else
                priv->draw_state = DRAW_EMPTY;

            if (refresh)
            {
                /* we might have to adjust the number we set, because children has
                 * nodes of type we don't care for, because some were not visible,
                 * etc */
                if (nb_real != children->len)
                {
                    g_mutex_lock (&data->mutex);
                    data->count -= children->len - nb_real;
                    g_mutex_unlock (&data->mutex);
                }
                refresh_node_cb (NULL, FALSE, data);
            }
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
            TRUE /* expand row */,
            FALSE /* no refresh */);

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
donna_tree_view_test_collapse_row (GtkTreeView    *treev,
                                   GtkTreeIter    *iter,
                                   GtkTreePath    *path)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreePath *p;
    GtkTreeSelection *sel;
    GtkTreeIter sel_iter;

    if (!is_tree (tree))
        /* no collapse */
        return TRUE;

    /* if the focused row is somewhere down, we need to move it up before the
     * collapse, to avoid GTK's set_cursor() */
    gtk_tree_view_get_cursor (treev, &p, NULL);
    if (gtk_tree_path_is_ancestor (path, p))
        gtk_tree_view_set_focused_row (treev, path);
    gtk_tree_path_free (p);

    /* if the current row (i.e. selected path) is somewhere down, let's change
     * the selection now so we can change the selection, without changing the
     * focus */
    sel = gtk_tree_view_get_selection (treev);
    if (gtk_tree_selection_get_selected (sel, NULL, &sel_iter))
    {
        p = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &sel_iter);
        if (p)
        {
            if (gtk_tree_path_is_ancestor (path, p))
                gtk_tree_selection_select_path (sel, path);
            gtk_tree_path_free (p);
        }
    }

    /* collapse */
    return FALSE;
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
    GSList *l;

    /* since the col_name comes from user input, we could fail to find the
     * column in this case */
    _col = get_column_by_name (tree, col_name);
    if (_col)
        return _col->ct_data;
    /* this means it's a column not loaded/used in tree. But, we know it does
     * exist (because filter has the ct) so we need to get it & load a ctdata,
     * if we haven't already */
    for (l = tree->priv->columns_filter; l; l = l->next)
    {
        struct column_filter *cf = l->data;

        if (streq (cf->name, col_name))
            return cf->ct_data;
    }

    {
        DonnaTreeViewPrivate *priv = tree->priv;
        struct column_filter *cf;
        gchar *col_type = NULL;

        cf = g_new0 (struct column_filter, 1);
        cf->name = g_strdup (col_name);
        donna_config_get_string (donna_app_peek_config (priv->app), &col_type,
                "columns/%s/type", col_name);
        cf->ct   = donna_app_get_columntype (priv->app,
                (col_type) ? col_type : col_name);
        g_free (col_type);
        donna_columntype_refresh_data (cf->ct, priv->name, col_name,
                priv->arrangement->columns_options, &cf->ct_data);
        priv->columns_filter = g_slist_prepend (priv->columns_filter, cf);
        return cf->ct_data;
    }
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
    /* special case: in mode list we can be our own ct, for the column showing
     * the line number. This is obviously has nothing to do w/ nodes, we handle
     * the rendering here instead of going through the ct interface */
    if (_col->ct == (DonnaColumnType *) tree)
    {
        GtkTreePath *path;
        gchar buf[10];
        gint ln = 0;
        gboolean refresh = FALSE;

        path = gtk_tree_model_get_path (model, iter);
        if (priv->ln_relative && (!priv->ln_relative_focused
                    || gtk_widget_has_focus ((GtkWidget *) tree)))
        {
            GtkTreePath *path_focus;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (path_focus)
            {
                static gint last = 0;

                /* get the focus number, and set refresh if it changed since
                 * last time, so that the entire column gets refreshed, as all
                 * relative numbers need to be updated */
                ln = gtk_tree_path_get_indices (path_focus)[0];
                refresh = last != ln;
                last = ln;

                /* calculate the relative number. For current line that falls to
                 * 0, which will then be turned to the current line number */
                ln -= gtk_tree_path_get_indices (path)[0];
                ln = ABS (ln);

                if (ln > 0)
                {
                    /* align relative numbers to the right */
                    g_object_set (renderer, "xalign", 1.0, NULL);
                    donna_renderer_set (renderer, "xalign", NULL);
                }

                gtk_tree_path_free (path_focus);
            }
        }
        if (ln == 0)
            ln = 1 + gtk_tree_path_get_indices (path)[0];

        snprintf (buf, 10, "%d", ln);
        g_object_set (renderer, "visible", TRUE, "text", buf, NULL);
        gtk_tree_path_free (path);

        if (refresh)
            gtk_widget_queue_draw_area ((GtkWidget *) tree, 0, 0,
                    gtk_tree_view_column_get_width (column),
                    gtk_widget_get_allocated_height ((GtkWidget*) tree));

        return;
    }
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

/* XXX: model is NOT our model, but the store's internal GtkTreeStore. See
 * tree_store_set_sort_func() for more */
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

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;
    _col = get_column_by_column (tree, column);

    /* special case: in mode list we can be our own ct, for the column showing
     * the line number. There's no sorting on that column obviously. */
    if (_col->ct == (DonnaColumnType *) tree)
        return 0;

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

static inline void
resort_tree (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeSortable *sortable;
    gint cur_sort_id;
    GtkSortType cur_sort_order;

    /* trigger a resort */

    sortable = (GtkTreeSortable *) priv->store;
    gtk_tree_sortable_get_sort_column_id (sortable, &cur_sort_id, &cur_sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable,
            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, cur_sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable, cur_sort_id, cur_sort_order);
}

static void
row_changed_cb (GtkTreeModel    *model,
                GtkTreePath     *path,
                GtkTreeIter     *iter,
                DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter it;
    gboolean resort = FALSE;
    gint wrong;

    /* row was updated, refresh was done, but there's no auto-resort. So let's
     * do it ourself */
    if (!priv->sort_column)
        return;

    wrong = (gtk_tree_view_column_get_sort_order (priv->sort_column)
            == GTK_SORT_DESCENDING) ? -1 : 1;

    it = *iter;
    if (gtk_tree_model_iter_previous (model, &it))
        /* should previous iter be switched? */
        if (sort_func (model, &it, iter, priv->sort_column) == wrong)
            resort = TRUE;

    it = *iter;
    if (!resort && gtk_tree_model_iter_next (model, &it))
        /* should next iter be switched? */
        if (sort_func (model, iter, &it, priv->sort_column) == wrong)
            resort = TRUE;

    if (resort)
        resort_tree (tree);
}

/* handling of removing rows from tree is done in remove_row_from_tree(). That
 * is, any row removed from the tree should always be so from there.
 * There is one special case though, where rows can be "removed" from the tree
 * without going through/coming form remove_row_from_tree(): when they're not
 * really removed, just filtered out (i.e. visual filter, or option show_hidden)
 * Since those are still in the model (just not the view) we shouldn't remove
 * them from our hashmap and whatnot, but there is one thing we wanna do: handle
 * the case of the focused row being "removed".
 * By default, GTK will do a set_cursor(), which isn't the best of behaviors. So
 * we'll try to make that better by simply doing a set_focused_row().
 * We need this special signal to do so, because we can then access the model
 * (where the row hasn't yet been "removed") and the view as well. Trying to do
 * it in row-deleted would fail because:
 * - post-GTK: it has already done its set_cursor()
 * - pre-GTK: the row is gone from the model, so there's no way to go prev/next,
 *   and adding to the fun the view still has the "old paths" in use, since it
 *   hasn't processed the row removal yet.
 */
static void
row_fake_deleted_cb (DonnaTreeStore *store,
                     GtkTreePath    *path,
                     GtkTreeIter    *iter,
                     DonnaTreeView  *tree)
{
    GtkTreePath *p;

    gtk_tree_view_get_cursor ((GtkTreeView *) tree, &p, NULL);
    if (gtk_tree_path_compare (path, p) == 0)
        handle_removing_row (tree, iter, TRUE);
    gtk_tree_path_free (p);
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

/* tree only -- there is a case where we might need to override/undo this
 * toggle: in mode tree, mini-tree, when the row is in EXPAND_PARTIAL.
 * In such a case, the expander might has vanished because of a store_refilter
 * (i.e. change of VF or show_hidden) that hide the last/only child. But since
 * this was a partial expand, there might actually be legitimate children that
 * could be shown there, so in such cases we need to put back a fake node &
 * trigger a has_children */
static void
row_has_child_toggled_cb (GtkTreeModel  *model,
                          GtkTreePath   *path,
                          GtkTreeIter   *iter,
                          DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    enum tree_expand es;
    DonnaNode *node;
    DonnaTask *task;

    if (!is_tree (tree) || !priv->is_minitree
            || gtk_tree_model_iter_has_child (model, iter))
        return;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            DONNA_TREE_COL_NODE,            &node,
            -1);
    if (es != DONNA_TREE_EXPAND_PARTIAL || !node)
    {
        if (node)
            g_object_unref (node);
        return;
    }

    set_es (priv, iter, DONNA_TREE_EXPAND_UNKNOWN);
    donna_tree_store_insert_with_values (priv->store, NULL, iter, 0,
            DONNA_TREE_COL_NODE,    NULL,
            -1);

    task = donna_node_has_children_task (node, priv->node_types, NULL);
    if (task)
    {
        struct node_children_data *data;

        data = g_slice_new0 (struct node_children_data);
        data->tree  = tree;
        data->iter  = *iter;
        watch_iter (tree, &data->iter);

        donna_task_set_callback (task,
                (task_callback_fn) node_has_children_cb,
                data,
                (GDestroyNotify) free_node_children_data);
        donna_app_run_task (priv->app, task);
    }
    g_object_unref (node);
}


struct node_updated_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *name;
};

static gboolean
real_node_updated_cb (struct node_updated_data *data)
{
    DonnaTreeView *tree = data->tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GSList *list, *l;
    guint i;

    /* do we have this node on tree? */
    l = g_hash_table_lookup (priv->hashtable, data->node);
    if (!l)
        goto done;

    /* should that property cause a refresh? */
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (data->name, cp->prop))
            break;
    }
    if (i >= priv->col_props->len)
        goto done;

    /* should we ignore this prop/node combo ? See refresh_node_prop_cb */
    g_mutex_lock (&priv->refresh_node_props_mutex);
    for (list = priv->refresh_node_props; list; list = list->next)
    {
        struct refresh_node_props_data *d = list->data;

        if (d->node == data->node)
        {
            for (i = 0; i < d->props->len; ++i)
            {
                if (streq (data->name, d->props->pdata[i]))
                    break;
            }
            if (i < d->props->len)
                break;
        }
    }
    g_mutex_unlock (&priv->refresh_node_props_mutex);
    if (list)
        goto done;

    /* trigger refresh on all rows for that node */
    model = (GtkTreeModel *) priv->store;
    for ( ; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;

        if (donna_tree_store_iter_is_visible (priv->store, iter))
        {
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }

done:
    if (streq (data->name, "name") || streq (data->name, "size"))
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    g_object_unref (data->node);
    g_free (data->name);
    g_free (data);
    return FALSE;
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 DonnaTreeView  *tree)
{
    struct node_updated_data *data;

    if (tree->priv->refresh_on_hold)
        return;

    /* we might not be in the main thread, but we need to be */

    data = g_new (struct node_updated_data, 1);
    data->tree = tree;
    data->node = g_object_ref (node);
    data->name = g_strdup (name);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_updated_cb, data);
}

struct node_deleted_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *location;
};

static void
free_node_deleted_data (struct node_deleted_data *data)
{
    g_object_unref (data->node);
    g_free (data->location);
    g_free (data);
}

static void
list_go_up_cb (DonnaTask                *task,
               gboolean                  timeout_called,
               struct node_deleted_data *data)
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
        free_node_deleted_data (data);
        return;
    }

    if (streq (data->location, "/"))
    {
        gchar *fl = donna_node_get_full_location (data->node);
        donna_app_show_error (data->tree->priv->app, err,
                "Treeview '%s': Failed to go to any parent of '%s'",
                data->tree->priv->name, fl);
        g_free (fl);
        free_node_deleted_data (data);
        return;
    }

    /* location can't be "/" since root can't be deleted */
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
        free_node_deleted_data (data);
        return;
    }
    donna_task_set_callback (t, (task_callback_fn) list_go_up_cb, data,
            (GDestroyNotify) free_node_deleted_data);
    donna_app_run_task (data->tree->priv->app, t);
}

static gboolean
real_node_deleted_cb (struct node_deleted_data *data)
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
                    "Treeview '%s': Current location (%s) has been deleted",
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
        GtkTreeIter it;

        next = list->next;
        it = * (GtkTreeIter *) list->data;
        /* this will remove the row from the list in hashtable. IOW, it will
         * remove the current list element (list); which is why we took the next
         * element ahead of time. Because it also assumes we own iter (to set it
         * to the next children) we need to use a local one */
        remove_row_from_tree (data->tree, &it, TRUE);
    }

free:
    free_node_deleted_data (data);
    /* don't repeat */
    return FALSE;
}

static void
node_deleted_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 DonnaTreeView  *tree)
{
    struct node_deleted_data *data;

    /* we might not be in the main thread, but we need to be */
    data = g_new0 (struct node_deleted_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_deleted_cb, data);
}

struct node_removed_from
{
    DonnaTreeView *tree;
    DonnaNode *node;
    DonnaNode *parent;
};

static gboolean
real_node_removed_from_cb (struct node_removed_from *nrf)
{
    DonnaTreeViewPrivate *priv = nrf->tree->priv;
    GSList *list;
    GSList *next;
    gboolean is_tree = is_tree (nrf->tree);

    if (!is_tree && priv->location != nrf->parent)
        goto finish;

    list = g_hash_table_lookup (priv->hashtable, nrf->node);
    if (!list)
        goto finish;

    for ( ; list; list = next)
    {
        GtkTreeIter it;

        next = list->next;
        it = * (GtkTreeIter *) list->data;

        /* for list we've already checked that current location == parent. For
         * tree however, we should only remove nodes of the parent matches */
        if (is_tree)
        {
            GtkTreeIter parent;
            DonnaNode *node;

            if (!gtk_tree_model_iter_parent ((GtkTreeModel *) priv->store,
                        &parent, &it))
                continue;

            gtk_tree_model_get ((GtkTreeModel *) priv->store, &parent,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            g_object_unref (node);
            if (node != nrf->parent)
                continue;
        }

        /* this will remove the row from the list in hashtable. IOW, it will
         * remove the current list element (list); which is why we took the next
         * element ahead of time. Because it also assumes we own iter (to set it
         * to the next children) we need to use a local one */
        remove_row_from_tree (nrf->tree, &it, TRUE);
    }

finish:
    g_object_unref (nrf->node);
    g_object_unref (nrf->parent);
    g_free (nrf);
    return FALSE;
}

static void
node_removed_from_cb (DonnaProvider *provider,
                      DonnaNode     *node,
                      DonnaNode     *parent,
                      DonnaTreeView *tree)
{
    struct node_removed_from *nrf;

    /* we might not be in the main thread, but we need to be */
    nrf = g_new0 (struct node_removed_from, 1);
    nrf->tree   = tree;
    nrf->node   = g_object_ref (node);
    nrf->parent = g_object_ref (parent);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_removed_from_cb, nrf);
}

struct node_children_cb_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNodeType    node_types;
    GPtrArray       *children;
};

static gboolean
real_node_children_cb (struct node_children_cb_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    enum tree_expand es;

    if (priv->location != data->node)
        goto free;

    if (!(data->node_types & priv->node_types))
        goto free;

    if (!is_tree (data->tree))
        set_children (data->tree, NULL, data->children, FALSE, FALSE);
    else
    {
        gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &priv->location_iter,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        if (es == DONNA_TREE_EXPAND_MAXI)
        {
            g_debug ("treeview '%s': updating children for current location",
                    priv->name);
            set_children (data->tree, &priv->location_iter, data->children, FALSE, FALSE);
        }
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
        gboolean was_empty;

        if (priv->cl == CHANGING_LOCATION_ASKED
                || priv->cl == CHANGING_LOCATION_SLOW)
        {
            if (!change_location (data->tree, CHANGING_LOCATION_GOT_CHILD,
                    data->node, NULL, NULL))
                goto free;
        }
        else if (priv->cl == CHANGING_LOCATION_GOT_CHILD)
        {
            if (priv->future_location != data->node)
                goto free;
        }
        else if (priv->location != data->node)
            goto free;

        was_empty = g_hash_table_size (priv->hashtable) == 0;

        /* we need to check if it's already there. This could happen on refresh,
         * where we get children via new-child signals first (e.g. search
         * results) */
        if (!g_hash_table_lookup (priv->hashtable, data->child)
                && add_node_to_tree (data->tree, NULL, data->child, NULL)
                && was_empty)
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
            ps->sid_node_deleted = g_signal_connect (provider, "node-deleted",
                    G_CALLBACK (node_deleted_cb), tree);
            ps->sid_node_removed_from = g_signal_connect (provider, "node-removed-from",
                    G_CALLBACK (node_removed_from_cb), tree);

            g_ptr_array_add (priv->providers, ps);
        }

        if (!priv->filling_list)
            check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
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
        ps->sid_node_deleted = g_signal_connect (provider, "node-deleted",
                G_CALLBACK (node_deleted_cb), tree);
        ps->sid_node_removed_from = g_signal_connect (provider, "node-removed-from",
                G_CALLBACK (node_removed_from_cb), tree);
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
        if (!priv->filling_list)
            check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
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
    if (!priv->filling_list)
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return TRUE;
}

gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node, GError **error)
{
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);

    if (!is_tree (tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "Treeview '%s': Cannot add root in mode List", tree->priv->name);
        return FALSE;
    }

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
    resort_tree (tree);
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
    g_free (arr->main_column);
    g_free (arr->sort_column);
    g_free (arr->second_sort_column);
    g_free (arr->columns_options);
    if (arr->color_filters)
        g_slist_free_full (arr->color_filters, g_object_unref);
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
    GtkTreeView          *treev = (GtkTreeView *) tree;
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
    GtkTreeViewColumn    *ctname_column = NULL;
    DonnaColumnType      *ctname;
    gint                  sort_id = 0;

    config = donna_app_peek_config (priv->app);
    sortable = (GtkTreeSortable *) priv->store;

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
    /* to set default for main (tree: & expander) column */
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

    priv->ln_relative = priv->ln_relative_focused = FALSE;

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

        /* ct "line-number" is a special one, which is handled by the treeview
         * itself (only supported in mode list) to show line numbers */
        if (!is_tree (tree) && streq (col_type, "line-number"))
        {
            ct = (DonnaColumnType *) g_object_ref (tree);
            column = NULL;
        }
        else
        {
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
                    else if (must_load_columns_options (arrangement,
                                priv->arrangement, force))
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

        if (!ctname_column && col_ct == ctname)
            ctname_column = column;

        if (!priv->main_column && arrangement->main_column
                && streq (col, arrangement->main_column))
            priv->main_column = column;

        /* size */
        if (snprintf (buf, 64, "columntypes/%s", col_type) >= 64)
            b = g_strdup_printf ("columntypes/%s", col_type);
        width = donna_config_get_int_column (config, priv->name, col,
                arrangement->columns_options, b, "width", 230);
        gtk_tree_view_column_set_min_width (column, 23);
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

        /* for line-number columns, there's no properties to watch, and this
         * shouldn't trigger a warning, obviously. Sorting also doesn't apply
         * there. */
        if (ct != (DonnaColumnType *) tree)
        {
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
        }

        last_column = column;

next:
        g_free (col_type);
        if (is_last_col)
            break;
        *e = ',';
        col = e + 1;
    }
    g_object_unref (ctname);

    /* ensure we have an expander column */
    if (!expander_column)
        expander_column = (ctname_column) ? ctname_column : first_column;

    /* ensure we have a main column */
    if (!priv->main_column)
        priv->main_column = (ctname_column) ? ctname_column : first_column;

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
    gtk_tree_view_set_expander_column (treev, expander_column);

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
        g_ptr_array_unref (_col->renderers);
#ifndef GTK_IS_JJK
        gtk_widget_unparent (btn);
#endif
        g_slice_free (struct column, _col);
        list = g_slist_delete_link (list, list);
    }

    /* remove any column_filter we had loaded */
    g_slist_free_full (priv->columns_filter, (GDestroyNotify) free_column_filter);
    priv->columns_filter = NULL;
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
            check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
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
    change_location (data->tree, CHANGING_LOCATION_SLOW, NULL, data, NULL);
}

static void switch_provider (DonnaTreeView *tree,
                             DonnaProvider *provider_current,
                             DonnaProvider *provider_future);

/* mode list only */
static void
node_get_children_list_cb (DonnaTask                            *task,
                           gboolean                              timeout_called,
                           struct node_get_children_list_data   *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean changed_location;
    GtkTreeIter iter, *it = &iter;
    const GValue *value;
    GPtrArray *arr;
    gboolean check_dupes;
    guint i;

    if (priv->get_children_task == task)
    {
        g_object_unref (priv->get_children_task);
        priv->get_children_task = NULL;
    }

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        if (priv->future_location == data->node)
        {
            const GError *error;
            gchar *fl = donna_node_get_full_location (data->node);

            error = donna_task_get_error (task);
            donna_app_show_error (priv->app, error,
                    "Treeview '%s': Failed to get children for node '%s'",
                    priv->name, fl);
            g_free (fl);

            if (priv->cl == CHANGING_LOCATION_GOT_CHILD)
            {
                /* GOT_CHILD means that we've already switch our current
                 * location, and don't remember what the old one was. It also
                 * means we got some children listed, so we should stay there
                 * (e.g. search results but the search failed/got cancelled
                 * halfway through).
                 * We keep priv->cl there, so donna_tree_view_get_children()
                 * will still not send anything (since we only have an
                 * incomplete list), but we reset priv->future_location */
                priv->future_location = NULL;

                /* Also update the location_task */
                if (priv->location_task)
                    g_object_unref (priv->location_task);
                priv->location_task = (donna_task_can_be_duplicated (task))
                    ? g_object_ref (task) : NULL;
            }
            else
            {
                GError *err = NULL;

                /* go back -- this is needed to maybe switch back providers,
                 * also we might have gone SLOW/DRAW_WAIT and need to
                 * re-fill/ask for children again */

                /* first let's make sure any tree sync-ed with us knows where we
                 * really are (else they could try to get us to change location
                 * back to where we tried & failed) */
                g_object_notify_by_pspec ((GObject *) data->tree,
                        donna_tree_view_props[PROP_LOCATION]);

                /* we hadn't done anything else yet, so all we need is switched
                 * back to listen to the right provider */
                if (priv->cl == CHANGING_LOCATION_ASKED)
                {
                    switch_provider (data->tree,
                            donna_node_peek_provider (priv->future_location),
                            donna_node_peek_provider (priv->location));
                    priv->cl = CHANGING_LOCATION_NOT;
                    priv->future_location = NULL;
                    priv->future_history_direction = 0;
                    priv->future_history_nb = 0;
                    goto free;
                }

                /* we actually need to get_children again */
                if (priv->location_task)
                {
                    struct node_get_children_list_data *d;
                    DonnaTask *task;

                    task = donna_task_get_duplicate (priv->location_task, &err);
                    if (!task)
                        goto no_task;
                    set_get_children_task (data->tree, task);

                    d = g_slice_new0 (struct node_get_children_list_data);
                    d->tree = data->tree;
                    d->node = g_object_ref (priv->location);

                    donna_task_set_callback (task,
                            (task_callback_fn) node_get_children_list_cb,
                            d,
                            (GDestroyNotify) free_node_get_children_list_data);
                    donna_app_run_task (priv->app, task);
                }
                else if (!change_location (data->tree, CHANGING_LOCATION_ASKED,
                            priv->location, NULL, &err))
                    goto no_task;

                check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
                goto free;
no_task:
                fl = donna_node_get_full_location (priv->location);
                donna_app_show_error (priv->app, err,
                        "Treeview '%s': Failed to go back to '%s'",
                        priv->name, fl);
                g_free (fl);
                g_clear_error (&err);

                check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
            }
        }

        goto free;
    }

    changed_location = priv->location && donna_node_peek_provider (data->node)
        != donna_node_peek_provider (priv->location);
    check_dupes = priv->cl == CHANGING_LOCATION_GOT_CHILD;

    if (!change_location (data->tree, CHANGING_LOCATION_NOT, data->node, NULL, NULL))
        goto free;

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

        priv->filling_list = TRUE;
        for (i = 0; i < arr->len; ++i)
        {
            if (check_dupes)
            {
                GSList *l;

                l = g_hash_table_lookup (priv->hashtable, arr->pdata[i]);
                if (l)
                    it = l->data;
                else
                    add_node_to_tree (data->tree, NULL, arr->pdata[i], it);
            }
            else
                add_node_to_tree (data->tree, NULL, arr->pdata[i], it);

            if (data->child == arr->pdata[i])
                /* don't change iter no more */
                it = NULL;
        }
        priv->filling_list = FALSE;

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
            gtk_tree_view_set_focused_row ((GtkTreeView *) data->tree, path);
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

        /* because when relative number are used and the tree was cleared, there
         * was no cursor, and so relative number couldn't be calculated (so it
         * fell back to "regular" line number) */
        if (priv->ln_relative)
            gtk_widget_queue_draw ((GtkWidget *) data->tree);
    }
    else
    {
        /* show the "location empty" message */
        priv->draw_state = DRAW_EMPTY;
        gtk_widget_queue_draw ((GtkWidget *) data->tree);
    }

    if (priv->location_task)
        g_object_unref (priv->location_task);
    priv->location_task = (donna_task_can_be_duplicated (task))
        ? g_object_ref (task) : NULL;

    /* emit signal */
    g_object_notify_by_pspec ((GObject *) data->tree,
            donna_tree_view_props[PROP_LOCATION]);
    check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);

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
        if (priv->future_location == data->child)
        {
            const GError *error;
            gchar *fl = donna_node_get_full_location (data->child);

            error = donna_task_get_error (task);
            donna_app_show_error (priv->app, error,
                    "Treeview '%s': Failed to get parent for node '%s'",
                    priv->name, fl);
            g_free (fl);

            /* this is needed to maybe switch back providers, also we might have
             * gone SLOW/DRAW_WAIT and need to re-fill/ask for children */
            change_location (data->tree, CHANGING_LOCATION_ASKED,
                    priv->location, NULL, NULL);
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
    priv->future_history_direction = 0;
    priv->future_history_nb = 0;

    task = donna_node_get_children_task (data->node, priv->node_types, NULL);
    set_get_children_task (data->tree, task);
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

/* mode list only */
static void
switch_provider (DonnaTreeView *tree,
                 DonnaProvider *provider_current,
                 DonnaProvider *provider_future)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (provider_current != provider_future)
    {
        struct provider_signals *ps;
        guint i;
        gint done = (provider_current) ? 0 : 1;
        gint found = -1;

        for (i = 0; i < priv->providers->len; ++i)
        {
            ps = priv->providers->pdata[i];

            if (ps->provider == provider_future)
            {
                found = i;
                ps->nb_nodes++;
                ++done;
            }
            else if (ps->provider == provider_current)
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
                    g_signal_handler_disconnect (ps->provider,
                            ps->sid_node_children);
                    ps->sid_node_children = 0;
                }
                ++done;
            }

            if (done == 2)
                break;
        }
        if (found < 0)
        {
            ps = g_new0 (struct provider_signals, 1);
            ps->provider = g_object_ref (provider_future);
            ps->nb_nodes = 1;
            ps->sid_node_updated = g_signal_connect (provider_future,
                    "node-updated", G_CALLBACK (node_updated_cb), tree);
            ps->sid_node_deleted = g_signal_connect (provider_future,
                    "node-deleted", G_CALLBACK (node_deleted_cb), tree);
            ps->sid_node_removed_from = g_signal_connect (provider_future,
                    "node-removed-from", G_CALLBACK (node_removed_from_cb), tree);

            g_ptr_array_add (priv->providers, ps);
        }
        else
            ps = priv->providers->pdata[found];
        /* whether or not we created ps, we need to connect to new_child, since
         * it's only useful for current location */
        ps->sid_node_new_child = g_signal_connect (provider_future,
                "node-new-child", G_CALLBACK (node_new_child_cb), tree);
        ps->sid_node_children = g_signal_connect (provider_future,
                "node-children", (GCallback) node_children_cb, tree);
    }
}

struct history_move
{
    DonnaTreeView *tree;
    DonnaHistoryDirection direction;
    guint nb;
};

static void
free_history_move (struct history_move *data)
{
    g_slice_free (struct history_move, data);
}

static inline gboolean
handle_history_move (DonnaTreeView *tree, DonnaNode *node)
{
    GValue v = G_VALUE_INIT;
    DonnaTask *task;
    DonnaNodeHasValue has;

    if (!streq ("internal", donna_node_get_domain (node)))
        return FALSE;

    donna_node_get (node, FALSE, "history-tree", &has, &v, NULL);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    if (tree != (DonnaTreeView *) g_value_get_object (&v))
    {
        g_value_unset (&v);
        return FALSE;
    }
    g_value_unset (&v);

    task = donna_node_trigger_task (node, NULL);
    if (!task)
        return FALSE;

    donna_app_run_task (tree->priv->app, task);
    return TRUE;
}

/* mode list only */
static gboolean
change_location (DonnaTreeView *tree,
                 enum cl        cl,
                 DonnaNode     *node,
                 gpointer       _data,
                 GError       **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaProvider *provider_current;
    DonnaProvider *provider_future;

    if (cl > CHANGING_LOCATION_ASKED && priv->cl > cl)
        /* this is ignoring e.g. CHANGING_LOCATION_SLOW if we're already at
         * CHANGING_LOCATION_GOT_CHILD */
        return FALSE;

    if (cl == CHANGING_LOCATION_ASKED)
    {
        DonnaTask *task;
        struct node_get_children_list_data *data;

        /* if that's already happening, nothing needs to be done. This can
         * happen sometimes when multiple selection-changed in a tree occur,
         * thus leading to multiple call to set_location() if they happen before
         * list completed the change :
         *
         * - first selection-changed, call to set_location()
         * - another selection-changed, list is still changing location, tree
         *   calls set_location() again. This would cancel the first one, and
         *   by the time the second one would end the first one has set
         *   future_location to NULL thus it gets ignored (this is all because
         *   they both point to the same location).
         *
         * Why would multiple selection-changed occur? Besides the fact that it
         * can happen be design, this would most likely happen because the
         * selection mode wasn't BROWSE, and setting it to BROWSE (in idle)
         * might emit the signal again.
         *
         * A reproducible way to get this is:
         * - go to a flat domain, e.g. register:/ [1]
         * - click on tree, there you go.
         *
         * [1] tree gets out of sync; See selection_changed_cb() for more
         */
        if (priv->future_location == node)
            return TRUE;

        provider_future = donna_node_peek_provider (node);

        if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
        {
            task = donna_node_get_children_task (node, priv->node_types, error);
            if (!task)
                return FALSE;
            set_get_children_task (tree, task);

            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->node = g_object_ref (node);

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
            if (donna_provider_get_flags (provider_future) == DONNA_PROVIDER_FLAG_FLAT)
            {
                gchar *fl;

                /* special case: if this is a node from history_get_node() we
                 * will process it as a move in history. This will allow e.g.
                 * dynamic marks to move backward/forward/etc */
                if (handle_history_move (tree, node))
                    return TRUE;

                fl = donna_node_get_full_location (node);
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
                        "Treeview '%s': Cannot set node '%s' as current location, "
                        "provider is flat (i.e. no parent to go to)",
                        priv->name, fl);
                g_free (fl);
                return FALSE;
            }

            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->child = g_object_ref (node);

            task = donna_node_get_parent_task (node, NULL);
            donna_task_set_timeout (task, 800, /* FIXME */
                    (task_timeout_fn) node_get_children_list_timeout,
                    data,
                    NULL);
            donna_task_set_callback (task,
                    (task_callback_fn) node_get_parent_list_cb,
                    data,
                    (GDestroyNotify) free_node_get_children_list_data);
        }

        /* if we're not or already switched, current location is as expected */
        if (priv->cl == CHANGING_LOCATION_NOT
                || priv->cl == CHANGING_LOCATION_GOT_CHILD)
        {
            if (G_LIKELY (priv->location))
                provider_current = donna_node_peek_provider (priv->location);
            else
                provider_current = NULL;
        }
        else
            /* but for ASKED and SLOW we've already switched to future provider,
             * so we should consider it as our current one */
            provider_current = donna_node_peek_provider (priv->future_location);

        /* we don't ref this node, since we should only have it for a short
         * period of time, and will only use it to compare (the pointer) in the
         * task's timeout/cb, to make sure the new location is still valid */
        priv->future_location = node;
        /* we might have gotten extra info if this is a move in history */
        if (_data)
        {
            struct history_move *hm = _data;
            priv->future_history_direction = hm->direction;
            priv->future_history_nb = hm->nb;
        }
        else
        {
            priv->future_history_direction = 0;
            priv->future_history_nb = 0;
        }

        /* connect to provider's signals of future location (if needed) */
        switch_provider (tree, provider_current, provider_future);

        /* update cl now to make sure we don't overwrite the task we're about to
         * run or something. That is, said task could be an INTERNAL_FAST one
         * (e.g. in config) and therefore run right now blockingly. And once
         * done its callback will set priv->cl to NOT, which we would then
         * overwrite with our ASKED, leading to troubles. */
        priv->cl = cl;

        /* now that we're ready, let's get those children */
        donna_app_run_task (priv->app, task);

        /* return now, since we've handled cl already */
        return TRUE;
    }
    else if (cl == CHANGING_LOCATION_SLOW)
    {
        struct node_get_children_list_data *data = _data;

        /* is this still valid (or did the user click away already) ? */
        if (data->node)
        {
            if (priv->future_location != data->node)
                return FALSE;
        }
        else if (priv->future_location != data->child)
            return FALSE;

        /* clear the list (see selection_changed_cb() for why filling_list) */
        priv->filling_list = TRUE;
        donna_tree_store_clear (priv->store);
        priv->filling_list = FALSE;
        /* also the hashtable (we don't need to unref nodes (keys), as our ref was
         * handled by the store) */
        g_hash_table_remove_all (priv->hashtable);
        /* and show the "please wait" message */
        priv->draw_state = DRAW_WAIT;
        gtk_widget_queue_draw ((GtkWidget *) tree);
    }
    else /* CHANGING_LOCATION_GOT_CHILD || CHANGING_LOCATION_NOT */
    {
        if (node != priv->future_location)
            return FALSE;

        if (priv->cl < CHANGING_LOCATION_GOT_CHILD)
        {
            /* clear the list (see selection_changed_cb() for why filling_list) */
            priv->filling_list = TRUE;
            donna_tree_store_clear (priv->store);
            priv->filling_list = FALSE;
            /* also the hashtable (we don't need to unref nodes (keys), as our ref was
             * handled by the store) */
            g_hash_table_remove_all (priv->hashtable);
            /* no special drawing */
            priv->draw_state = DRAW_NOTHING;
        }

        /* GOT_CHILD, or NOT which means finalizing the switch, in which case we
         * also need to do the switch if it hasn't been done before */
        if (cl == CHANGING_LOCATION_GOT_CHILD
                || priv->cl < CHANGING_LOCATION_GOT_CHILD)
        {
            GtkStyleContext *context;
            const gchar *domain;
            gchar buf[64];

            context = gtk_widget_get_style_context ((GtkWidget *) tree);
            /* update current location (now because we need this done to build
             * arrangement) */
            if (priv->location)
            {
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
            priv->location = g_object_ref (node);
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
            /* update arrangement for new location if needed */
            donna_tree_view_build_arrangement (tree, FALSE);

            /* update history */
            if (priv->future_history_direction > 0)
            {
                const gchar *h;

                /* this is a move in history */
                if (G_LIKELY ((h = donna_history_move (priv->history,
                        priv->future_history_direction,
                        priv->future_history_nb,
                        NULL))))
                {
                    gchar *fl = donna_node_get_full_location (priv->location);

                    if (G_LIKELY (streq (fl, h)))
                        g_free (fl);
                    else
                    {
                        /* this means the history changed during the change of
                         * location, and e.g. change of history_max option
                         * (could have resulted in he needed items to be lost) */
                        g_warning ("Treeview '%s': History move couldn't be validated, "
                                "adding current location as new one instead",
                                priv->name);
                        donna_history_take_item (priv->history, fl);
                    }

                    priv->future_history_direction = 0;
                    priv->future_history_nb = 0;
                }
                else
                {
                    /* this means the history changed during the change of
                     * location, and e.g. was cleared. */
                    g_warning ("Treeview '%s': History move couldn't be validated, "
                            "adding current location as new one instead",
                            priv->name);
                    donna_history_take_item (priv->history,
                            donna_node_get_full_location (priv->location));
                }
            }
            else
                /* add new location to history */
                donna_history_take_item (priv->history,
                        donna_node_get_full_location (priv->location));

            /* emit signal */
            g_object_notify_by_pspec ((GObject *) tree,
                    donna_tree_view_props[PROP_LOCATION]);
            check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
        }

        if (cl == CHANGING_LOCATION_NOT)
            priv->future_location = NULL;
    }

    priv->cl = cl;
    return TRUE;
}

gboolean
donna_tree_view_set_location (DonnaTreeView  *tree,
                              DonnaNode      *node,
                              GError        **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = tree->priv;

    if (is_tree (tree))
    {
        GtkTreeIter *iter;

        if (!(priv->node_types & donna_node_get_node_type (node)))
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
        return change_location (tree, CHANGING_LOCATION_ASKED, node, NULL, error);
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
        GtkTreeView *treev  = (GtkTreeView *) tree;
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
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

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (gtk_tree_model_get_iter (model, iter, path))
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
                GtkTreeIter it;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter (model, iter, path))
                    return ROW_ID_INVALID;
                it = *iter;

                for (;;)
                {
                    if (!donna_tree_model_iter_previous (model, iter))
                    {
                        /* no previous row, simply return the current one.
                         * Avoids getting "invalid row-id" error message just
                         * because you press Up while on the first row, or
                         * similarly breaking "v3k" when there's only 2 rows
                         * above, etc */
                        *iter = it;
                        return ROW_ID_ROW;
                    }
                    if (!is_tree (tree) || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("next", s))
            {
                GtkTreePath *path;
                GtkTreeIter it;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter (model, iter, path))
                    return ROW_ID_INVALID;
                it = *iter;

                for (;;)
                {
                    if (!donna_tree_model_iter_next (model, iter))
                    {
                        /* same as prev */
                        *iter = it;
                        return ROW_ID_ROW;
                    }
                    if (!is_tree (tree) || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("last", s))
            {
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

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_up (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
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

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_down (path);

                if (gtk_tree_model_get_iter (model, iter, path)
                        && is_row_accessible (tree, iter))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("top", s))
            {
                GtkTreePath *path;

                if (!gtk_tree_view_get_visible_range (treev, &path, NULL))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    GdkRectangle rect;

                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    if (rect.y < -(rect.height / 3))
                    {
                        for (;;)
                        {
                            if (!donna_tree_model_iter_next (model, iter))
                                return ROW_ID_INVALID;
                            if (!is_tree (tree) || is_row_accessible (tree, iter))
                                break;
                        }
                    }
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("bottom", s))
            {
                GtkTreePath *path;

                if (!gtk_tree_view_get_visible_range (treev, NULL, &path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    GdkRectangle rect_visible;
                    GdkRectangle rect;

                    gtk_tree_view_get_visible_rect (treev, &rect_visible);

                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    if (rect.y + 2 * (rect.height / 3) > rect_visible.height)
                    {
                        for (;;)
                        {
                            if (!donna_tree_model_iter_previous (model, iter))
                                return ROW_ID_INVALID;
                            if (!is_tree (tree) || is_row_accessible (tree, iter))
                                break;
                        }
                    }
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("prev-same-depth", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_prev (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
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

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_next (path);

                if (gtk_tree_model_get_iter (model, iter, path))
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
            GtkTreePath *path;
            GtkTreeIter iter_top;
            gchar *end;
            gint i;
            enum
            {
                LINE,
                PCTG_TREE,
                PCTG_VISIBLE
            } flg = LINE;

            if (*s == '%')
            {
                flg = PCTG_VISIBLE;
                ++s;
            }

            i = g_ascii_strtoll (s, &end, 10);
            if (i < 0)
                return ROW_ID_INVALID;

            if (end[0] == '%' && end[1] == '\0')
                flg = PCTG_TREE;
            else if (*end == '\0')
                i = MAX (1, i);
            else
                return ROW_ID_INVALID;

            if (flg != LINE)
            {
                DonnaTreeRowId rid = { DONNA_ARG_TYPE_PATH, NULL };
                GdkRectangle rect;
                gint height;
                gint rows;
                gint top;

                /* locate first/top row */
                if (flg == PCTG_TREE)
                    path = gtk_tree_path_new_from_indices (0, -1);
                else
                {
                    rid.ptr = ":top";
                    if (convert_row_id_to_iter (tree, &rid, &iter_top) == ROW_ID_INVALID)
                        return ROW_ID_INVALID;
                    path = gtk_tree_model_get_path (model, &iter_top);
                    if (!is_tree (tree))
                        top = gtk_tree_path_get_indices (path)[0];
                }
                gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                gtk_tree_path_free (path);
                height = ABS (rect.y);

                /* locate last/bottom row */
                if (flg == PCTG_TREE)
                {
                    if (!donna_tree_model_iter_last (model, iter))
                        return ROW_ID_INVALID;
                }
                else
                {
                    rid.ptr = ":bottom";
                    if (convert_row_id_to_iter (tree, &rid, iter) == ROW_ID_INVALID)
                            return ROW_ID_INVALID;
                }
                path = gtk_tree_model_get_path (model, iter);
                if (!path)
                    return ROW_ID_INVALID;
                gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                gtk_tree_path_free (path);
                height += ABS (rect.y) + rect.height;

                /* nb of rows accessible/visible on tree */
                rows = height / rect.height;

                /* get the one at specified percent */
                i = (rows * ((gdouble) i / 100.0)) + 1;
                i = CLAMP (i, 1, rows);

                if (flg == PCTG_VISIBLE && !is_tree (tree))
                    i += top;
            }

            if (is_tree (tree))
            {
                /* we can't just get a path, so we'll go to the first/top row
                 * and move down */
                if (flg == PCTG_VISIBLE)
                    *iter = iter_top;
                else
                    if (!gtk_tree_model_iter_children (model, iter, NULL))
                        return ROW_ID_INVALID;

                --i;
                while (i > 0)
                {
                    if (!donna_tree_model_iter_next (model, iter))
                        return ROW_ID_INVALID;
                    if (is_row_accessible (tree, iter))
                        --i;
                }
                path = gtk_tree_model_get_path (model, iter);
            }
            else
                path = gtk_tree_path_new_from_indices (i - 1, -1);

            if (gtk_tree_model_get_iter (model, iter, path))
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
                DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
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
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return TRUE;
}

gboolean
donna_tree_view_set_cursor (DonnaTreeView        *tree,
                            DonnaTreeRowId       *rowid,
                            gboolean              no_scroll,
                            GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
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
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    if (!is_tree (tree))
        gtk_tree_selection_unselect_all (sel);
    gtk_tree_selection_select_path (sel, path);
    gtk_tree_path_free (path);
    /* no_scroll instead of scroll so in command (which mimics the params, but
     * where that one is optional) the default is FALSE, i.e. scroll, but it can
     * be set to TRUE to disable scrolling. */
    if (!no_scroll)
        scroll_to_iter (tree, &iter);
    return TRUE;
}

gboolean
donna_tree_view_activate_row (DonnaTreeView      *tree,
                              DonnaTreeRowId     *rowid,
                              GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GtkTreeIter iter;
    row_id_type type;
    gboolean ret = TRUE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot activate row, invalid row-id",
                priv->name);
        return FALSE;
    }

    if (type == ROW_ID_SELECTION)
        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    if (type == ROW_ID_SELECTION || type == ROW_ID_ALL)
        /* for SELECTION we'll also iter through each row, and check whether or
         * not they're selected. Might not seem like the best of choices, but
         * this is what gtk_tree_selection_get_selected_rows() actually does,
         * so this makes this code simpler (and avoids GList "overhead") */
        if (!gtk_tree_model_iter_children (model, &iter, NULL))
            /* empty tree. I consider this a success */
            return TRUE;

    for (;;)
    {
        DonnaNode   *node;

        if (type == ROW_ID_SELECTION
                && !gtk_tree_selection_iter_is_selected (sel, &iter))
            goto next;

        gtk_tree_model_get (model, &iter,
                DONNA_TREE_VIEW_COL_NODE,   &node,
                -1);
        if (G_UNLIKELY (!node))
            goto next;

        /* since the node is in the tree, we already have a ref on it */
        g_object_unref (node);

        if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
        {
            /* only for single row; else we'd risk having to go into multiple
             * locations, so we just don't support it/ignore them */
            if (type == ROW_ID_ROW)
                ret = (donna_tree_view_set_location (tree, node, error)) ? ret : FALSE;
        }
        else /* DONNA_NODE_ITEM */
        {
            DonnaTask *task;

            task = donna_node_trigger_task (node, error);
            if (G_UNLIKELY (!task))
            {
                ret = FALSE;
                goto next;
            }

            donna_task_set_callback (task,
                    (task_callback_fn) show_err_on_task_failed, tree, NULL);
            donna_app_run_task (priv->app, task);
        }

next:
        if (type == ROW_ID_ROW || !donna_tree_model_iter_next (model, &iter))
            break;
    }
    return ret;
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
    GtkTreeIter iter;
    row_id_type type;

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
    GtkTreeIter         *iter;
    GtkTreePath         *path;
};

enum
{
    INLINE_EDIT_DONE = 0,
    INLINE_EDIT_PREV,
    INLINE_EDIT_NEXT
};

struct inline_edit
{
    DonnaTreeView       *tree;
    GtkTreeViewColumn   *column;
    DonnaTreeRow        *row;
    guint                move;
};

static gboolean
move_inline_edit (struct inline_edit *ie)
{
    DonnaTreeRowId rid;
    struct column *_col;

    rid.type = DONNA_ARG_TYPE_ROW;
    rid.ptr  = ie->row;

    _col = get_column_by_column (ie->tree, ie->column);
    donna_tree_view_edit_column (ie->tree, &rid, _col->name, NULL);
    g_object_unref (ie->row->node);
    g_slice_free (DonnaTreeRow, ie->row);
    g_slice_free (struct inline_edit, ie);
    return FALSE;
}

static void
editable_remove_widget_cb (GtkCellEditable *editable, struct inline_edit *ie)
{
    DonnaTreeViewPrivate *priv = ie->tree->priv;

    g_signal_handler_disconnect (editable,
            priv->renderer_editable_remove_widget_sid);
    priv->renderer_editable_remove_widget_sid = 0;
    priv->renderer_editable = NULL;
    if (ie->move != INLINE_EDIT_DONE)
    {
        DonnaTreeRowId rid;
        row_id_type type;
        GtkTreeIter iter;

        /* we need to call move_inline_edit() via an idle source, because
         * otherwise the entry doesn't get properly destroyed, etc
         * But, we need to get the prev/next row right now, because after events
         * have been processed and since there might have been a property
         * update, the sort order could be affected and it would look odd that
         * e.g. pressing Up goes to the row above *after* the resort has been
         * triggered.
         * This is why we get the iter of the prev/next row now, and store it
         * (as a DonnaTreeRow) for move_inline_edit() to use */

        rid.type = DONNA_ARG_TYPE_PATH;
        rid.ptr  = (ie->move == INLINE_EDIT_PREV) ? ":prev" : ":next";
        type = convert_row_id_to_iter (ie->tree, &rid, &iter);
        if (type == ROW_ID_ROW)
        {
            GSList *list;

            ie->row = g_slice_new (DonnaTreeRow);

            gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                    DONNA_TREE_VIEW_COL_NODE,   &ie->row->node,
                    -1);

            list = g_hash_table_lookup (priv->hashtable, ie->row->node);
            for ( ; list; list = list->next)
                if (itereq (&iter, (GtkTreeIter *) list->data))
                {
                    ie->row->iter = list->data;
                    break;
                }

            g_idle_add ((GSourceFunc) move_inline_edit, ie);
            return;
        }
    }
    g_slice_free (struct inline_edit, ie);
}

static gboolean
kp_up_down_cb (GtkEntry *entry, GdkEventKey *event, struct inline_edit *ie)
{
    if (event->keyval == GDK_KEY_Up)
        ie->move = INLINE_EDIT_PREV;
    else if (event->keyval == GDK_KEY_Down)
        ie->move = INLINE_EDIT_NEXT;
    return FALSE;
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct inline_edit  *ie)
{
    DonnaTreeViewPrivate *priv = ie->tree->priv;

    g_signal_handler_disconnect (renderer, priv->renderer_editing_started_sid);
    priv->renderer_editing_started_sid = 0;

    donna_app_ensure_focused (priv->app);

    if (GTK_IS_ENTRY (editable))
        /* handle using Up/Down to move the editing to the prev/next row */
        g_signal_connect (editable, "key-press-event",
                (GCallback) kp_up_down_cb, ie);

    /* in case we need to abort the editing */
    priv->renderer_editable = editable;
    /* when the editing will be done */
    priv->renderer_editable_remove_widget_sid = g_signal_connect (
            editable, "remove-widget",
            (GCallback) editable_remove_widget_cb, ie);
}

static gboolean
renderer_edit (GtkCellRenderer *renderer, struct re_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    struct inline_edit *ie;
    GdkEventAny event = { .type = GDK_NOTHING };
    GdkRectangle cell_area;
    gint offset;
    gboolean ret;

    /* shouldn't happen, but to be safe */
    if (G_UNLIKELY (priv->renderer_editable))
        return FALSE;

    /* this is needed to set the renderer to our cell, since it might have been
     * used for another cell/row and that would cause confusion (e.g. wrong
     * text used in the entry, etc) */
    gtk_tree_view_column_cell_set_cell_data (data->column,
            (GtkTreeModel *) priv->store,
            data->iter, FALSE, FALSE);
    /* get the cell_area (i.e. where editable will be placed */
    gtk_tree_view_get_cell_area ((GtkTreeView *) data->tree,
            data->path, data->column, &cell_area);
    /* in case there are other renderers in that column */
    if (gtk_tree_view_column_cell_get_position (data->column, renderer,
            &offset, &cell_area.width))
        cell_area.x += offset;

    ie = g_slice_new (struct inline_edit);
    ie->tree = data->tree;
    ie->column = data->column;
    ie->move = INLINE_EDIT_DONE;

    /* so we can get the editable to be able to abort if needed */
    priv->renderer_editing_started_sid = g_signal_connect (
            renderer, "editing-started",
            (GCallback) editing_started_cb, ie);

    ret = gtk_cell_area_activate_cell (
            gtk_cell_layout_get_area ((GtkCellLayout *) data->column),
            (GtkWidget *) data->tree,
            renderer,
            (GdkEvent *) &event,
            &cell_area,
            0);

    if (G_UNLIKELY (!ret))
    {
        g_signal_handler_disconnect (renderer, priv->renderer_editing_started_sid);
        priv->renderer_editing_started_sid = 0;
        g_slice_free (struct inline_edit, ie);
    }

    return ret;
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
        .iter   = &iter,
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
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);

    gtk_tree_path_free (re_data.path);
    g_object_unref (node);
    return TRUE;
}

struct refresh_list
{
    DonnaTreeView *tree;
    DonnaNode *node;
};

/* list only */
static void
node_get_children_refresh_list_cb (DonnaTask            *task,
                                   gboolean              timeout_called,
                                   struct refresh_list  *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    if (priv->get_children_task == task)
    {
        g_object_unref (priv->get_children_task);
        priv->get_children_task = NULL;
    }

    if (data->node != priv->location)
        goto free;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        donna_app_show_error (priv->app, donna_task_get_error (task),
                "Treeview '%s': Failed to refresh", priv->name);
        return;
    }

    set_children (data->tree, NULL,
            g_value_get_boxed (donna_task_get_return_value (task)),
            FALSE, TRUE);
free:
    g_free (data);
}

struct node_children_refresh_data
{
    DonnaTreeView *tree;
    GtkTreeIter iter;
};

/* tree only */
static void
node_get_children_refresh_tree_cb (DonnaTask                         *task,
                                   gboolean                           timeout_called,
                                   struct node_children_refresh_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        goto free;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        donna_app_show_error (priv->app, donna_task_get_error (task),
                "Treeview '%s': Failed to refresh", priv->name);
        goto free;
    }

    set_children (data->tree, &data->iter,
            g_value_get_boxed (donna_task_get_return_value (task)),
            FALSE, TRUE);

free:
    g_free (data);
}

/* tree only */
static gboolean
may_get_children_refresh (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GError *err = NULL;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreePath *path;
    DonnaTask *task;
    DonnaNode *node;
    enum tree_expand es;
    gboolean ret = FALSE;

    path = gtk_tree_model_get_path (model, iter);

    /* refresh the node */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_NODE,            &node,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    task = donna_node_refresh_task (node, &err, DONNA_NODE_REFRESH_SET_VALUES);
    if (!task)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_warning ("Treeview '%s': Failed to refresh '%s': %s",
                priv->name, fl, err->message);
        g_clear_error (&err);
        g_free (fl);
        g_object_unref (node);
        gtk_tree_path_free (path);
        return FALSE;
    }
    donna_app_run_task (priv->app, task);

    /* if EXPAND_MAXI, update children */
    if (es == DONNA_TREE_EXPAND_MAXI)
    {
        struct node_children_refresh_data *data;

        ret = TRUE;
        task = donna_node_get_children_task (node,
                priv->node_types, &err);
        if (!task)
        {
            gchar *fl = donna_node_get_full_location (node);
            g_warning ("Treeview '%s': Failed to trigger children update for '%s': %s",
                    priv->name, fl, err->message);
            g_clear_error (&err);
            g_free (fl);
            g_object_unref (node);
            gtk_tree_path_free (path);
            return FALSE;
        }

        data = g_new0 (struct node_children_refresh_data, 1);
        data->tree = tree;
        data->iter = *iter;
        watch_iter (tree, &data->iter);

        donna_task_set_callback (task,
                (task_callback_fn) node_get_children_refresh_tree_cb,
                data, NULL);
        donna_app_run_task (priv->app, task);
    }
    gtk_tree_path_free (path);
    g_object_unref (node);
    return ret;
}

gboolean
donna_tree_view_refresh (DonnaTreeView          *tree,
                         DonnaTreeRefreshMode    mode,
                         GError                **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (mode == DONNA_TREE_REFRESH_VISIBLE
            || mode == DONNA_TREE_REFRESH_SIMPLE
            || mode == DONNA_TREE_REFRESH_NORMAL
            || mode == DONNA_TREE_REFRESH_RELOAD, FALSE);
    priv = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (mode == DONNA_TREE_REFRESH_VISIBLE || mode == DONNA_TREE_REFRESH_SIMPLE)
    {
        struct refresh_data *data;
        GtkTreePath *start = NULL;
        GtkTreePath *end = NULL;
        GtkTreeIter  it_end;
        GtkTreeIter  it;
        guint nb_org, nb_real;

        if (donna_tree_model_get_count (model) == 0)
            return TRUE;

        if (mode == DONNA_TREE_REFRESH_VISIBLE)
        {
            if (!gtk_tree_view_get_visible_range (treev, &start, &end)
                    || !gtk_tree_model_get_iter (model, &it, start)
                    || !gtk_tree_model_get_iter (model, &it_end, end))
            {
                if (start)
                    gtk_tree_path_free (start);
                if (end)
                    gtk_tree_path_free (end);
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Failed to get visible range of rows",
                        priv->name);
                return FALSE;
            }
            gtk_tree_path_free (start);
            gtk_tree_path_free (end);
        }
        else /* DONNA_TREE_REFRESH_SIMPLE */
        {
            if (!gtk_tree_model_iter_children (model, &it, NULL))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Failed to get first row",
                        priv->name);
                return FALSE;
            }
        }

        /* see refresh_node_cb() for more about this */
        data = g_new (struct refresh_data, 1);
        data->tree = tree;
        g_mutex_init (&data->mutex);
        data->count = nb_org = donna_tree_model_get_count (model);
        data->done = FALSE;
        priv->refresh_on_hold = TRUE;

        nb_real = 0;
        do
        {
            GError *err = NULL;
            DonnaNode *node;
            DonnaTask *task;

            if (!is_row_accessible (tree, &it))
                continue;

            gtk_tree_model_get (model, &it,
                    DONNA_TREE_COL_NODE,    &node,
                    -1);
            task = donna_node_refresh_task (node, &err, DONNA_NODE_REFRESH_SET_VALUES);
            if (G_UNLIKELY (!task))
            {
                gchar *fl = donna_node_get_full_location (node);
                g_warning ("Treeview '%s': Failed to refresh '%s': %s",
                        priv->name, fl, err->message);
                g_clear_error (&err);
                g_free (fl);
                continue;
            }
            donna_task_set_callback (task,
                    (task_callback_fn) refresh_node_cb,
                    data, NULL);
            donna_app_run_task (priv->app, task);
            g_object_unref (node);
            ++nb_real;
        } while ((mode == DONNA_TREE_REFRESH_SIMPLE || !itereq (&it, &it_end))
                && donna_tree_model_iter_next (model, &it));

        /* we might have to adjust the number we set, either because some task
         * failed to be created, or just because this is mode VISIBLE */
        if (nb_real != nb_org)
        {
            g_mutex_lock (&data->mutex);
            data->count -= nb_org - nb_real;
            g_mutex_unlock (&data->mutex);
        }
        /* set flag done to TRUE and handles things in odd change all tasks are
         * already done */
        refresh_node_cb (NULL, FALSE, data);

        return TRUE;
    }
    else if (mode == DONNA_TREE_REFRESH_NORMAL)
    {
        if (is_tree (tree))
        {
            GtkTreeIter it;
            gboolean (*next_fn) (GtkTreeModel *model, GtkTreeIter *iter);

            if (donna_tree_model_get_count (model) == 0)
                return TRUE;

            if (!gtk_tree_model_iter_children (model, &it, NULL))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Failed to get first root",
                        priv->name);
                return FALSE;
            }

            do
            {
                if (!is_row_accessible (tree, &it))
                    continue;

                if (may_get_children_refresh (tree, &it))
                    next_fn = gtk_tree_model_iter_next;
                else
                    next_fn = donna_tree_model_iter_next;
            } while (next_fn (model, &it));

            return TRUE;
        }
        else
        {
            DonnaTask *task;
            struct refresh_list *data;

            if (priv->location_task)
                task = donna_task_get_duplicate (priv->location_task, error);
            else
                task = donna_node_get_children_task (priv->location,
                        priv->node_types, error);
            if (!task)
                return FALSE;
            set_get_children_task (tree, task);

            data = g_new (struct refresh_list, 1);
            data->tree = tree;
            data->node = priv->location;

            donna_task_set_callback (task,
                    (task_callback_fn) node_get_children_refresh_list_cb,
                    data, g_free);
            donna_app_run_task (priv->app, task);
            return TRUE;
        }
    }
    /* DONNA_TREE_REFRESH_RELOAD */

    if (is_tree (tree))
    {
        /* TODO save to file; clear; load arr; load from file... or something */
    }
    else
    {
        if (priv->location_task)
        {
            struct node_get_children_list_data *data;
            DonnaTask *task;

            task = donna_task_get_duplicate (priv->location_task, error);
            if (!task)
                return FALSE;
            set_get_children_task (tree, task);

            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->node = g_object_ref (priv->location);

            donna_task_set_callback (task,
                    (task_callback_fn) node_get_children_list_cb,
                    data,
                    (GDestroyNotify) free_node_get_children_list_data);
        }
        else
            return change_location (tree, CHANGING_LOCATION_ASKED, priv->location,
                    NULL, error);
    }

    return TRUE;
}

gboolean
donna_tree_view_filter_nodes (DonnaTreeView *tree,
                              GPtrArray     *nodes,
                              const gchar   *filter_str,
                              GError       **error)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    return _donna_app_filter_nodes (tree->priv->app, nodes, filter_str,
            (get_ct_data_fn) get_ct_data, tree, error);
}

gboolean
donna_tree_view_goto_line (DonnaTreeView      *tree,
                           DonnaTreeSet        set,
                           DonnaTreeRowId     *rowid,
                           guint               nb,
                           DonnaTreeGoto       nb_type,
                           DonnaTreeSelAction  action,
                           gboolean            to_focused,
                           GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;
    GtkTreeIter   iter;
    row_id_type   type;
    GtkTreePath  *path  = NULL;
    GtkTreeIter   tb_iter;
    guint         is_tb = 0;
    guint         rows  = 0;
    guint         max;
    GdkRectangle  rect_visible;
    GdkRectangle  rect;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (nb_type == DONNA_TREE_GOTO_PERCENT)
    {
        GdkRectangle rect;
        gint height;

        /* locate first row */
        path = gtk_tree_path_new_from_indices (0, -1);
        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        gtk_tree_path_free (path);
        height = ABS (rect.y);

        /* locate last row */
        if (!donna_tree_model_iter_last (model, &iter))
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "Treeview '%s': Failed getting the last line",
                    priv->name);
            return FALSE;
        }
        path = gtk_tree_model_get_path (model, &iter);
        if (!path)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "Treeview '%s': Failed getting path to the last line",
                    priv->name);
            return FALSE;
        }
        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        gtk_tree_path_free (path);
        height += ABS (rect.y) + rect.height;

        /* nb of rows accessible on tree */
        rows = height / rect.height;

        /* get the one at specified percent */
        nb = (rows * ((gdouble) nb / 100.0)) + 1;

        /* this can now be treated as LINE */
        nb_type = DONNA_TREE_GOTO_LINE;
    }

    if (nb_type == DONNA_TREE_GOTO_LINE && nb > 0)
    {
        if (!is_tree (tree))
        {
            /* list, so line n is path n-1 */
            path = gtk_tree_path_new_from_indices (nb - 1, -1);
            if (!gtk_tree_model_get_iter (model, &iter, path))
            {
                gtk_tree_path_free (path);
                /* row doesn't exist, i.e. number is too high, let's just go to
                 * the last one */
                if (!donna_tree_model_iter_last (model, &iter))
                {
                    g_set_error (error, DONNA_TREE_VIEW_ERROR,
                            DONNA_TREE_VIEW_ERROR_OTHER,
                            "Treeview '%s': Failed getting the last line (<%d)",
                            priv->name, nb);
                    return FALSE;
                }
                path = gtk_tree_model_get_path (model, &iter);
            }
        }
        else
        {
            GtkTreeIter it;
            guint i;

            /* tree, so we'll go to the first and move down */
            if (!gtk_tree_model_iter_children (model, &iter, NULL))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Failed getting the first line (going to %d)",
                        priv->name, nb);
                return FALSE;
            }

            it = iter;
            for (i = 1; i < nb; )
            {
                if (!donna_tree_model_iter_next (model, &iter))
                {
                    /* row doesn't exist, i.e. number is too high, let's just go
                     * to the last one */
                    iter = it;
                    break;
                }
                if (is_row_accessible (tree, &iter))
                {
                    it = iter;
                    ++i;
                }
            }
            path = gtk_tree_model_get_path (model, &iter);
        }
        nb = 1;
        goto move;
    }

    /* those are special cases, where if the focus is already there, we want to
     * go one up/down more screen */
    if (rowid->type == DONNA_ARG_TYPE_PATH
            && (streq (rowid->ptr, ":top") || streq (rowid->ptr, ":bottom")))
    {
        is_tb = 1;
        gtk_tree_view_get_cursor (treev, &path, NULL);
        if (!path)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "Treeview '%s': Cannot go to line, failed to get cursor",
                    priv->name);
            return FALSE;
        }
        if (!gtk_tree_model_get_iter (model, &tb_iter, path))
        {
            gtk_tree_path_free (path);
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "Treeview '%s': Cannot go to line, failed to get cursor",
                    priv->name);
            return FALSE;
        }
        gtk_tree_path_free (path);
        path = NULL;
    }

    if (nb > 1 && nb_type == DONNA_TREE_GOTO_REPEAT)
    {
        /* only those make sense to be repeated */
        if (rowid->type != DONNA_ARG_TYPE_PATH
                || !(is_tb || streq (rowid->ptr, ":prev")
                    || streq (rowid->ptr, ":next")
                    || streq (rowid->ptr, ":up")
                    || streq (rowid->ptr, ":down")
                    || streq (rowid->ptr, ":prev-same-depth")
                    || streq (rowid->ptr, ":next-same-depth")))
            nb = 1;
    }
    else
        nb = 1;

    for ( ; nb > 0; --nb)
    {
        if (is_tb < 2)
        {
            if (path)
                gtk_tree_path_free (path);

            type = convert_row_id_to_iter (tree, rowid, &iter);
            if (type != ROW_ID_ROW)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                        "Treeview '%s': Cannot go to line, invalid row-id",
                        priv->name);
                return FALSE;
            }

            path = gtk_tree_model_get_path (model, &iter);
        }

        if (is_tb)
        {
            if (is_tb == 1 && itereq (&iter, &tb_iter))
                is_tb = 2;

            /* scroll only; or we're already there: let's go beyond */
            if (set == DONNA_TREE_SET_SCROLL || is_tb == 2)
            {
                if (!rows)
                {
                    gtk_tree_view_get_visible_rect (treev, &rect_visible);
                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    rows = rect_visible.height / rect.height;
                    max  = donna_tree_model_get_count (model) - 1;
                }

                if (!is_tree (tree))
                {
                    gint *indices;
                    gint i;

                    indices = gtk_tree_path_get_indices (path);
                    i = indices[0];
                    if (((gchar *) rowid->ptr)[1] == 't')
                    {
                        i -= rows;
                        i = MAX (i, 0);
                    }
                    else
                    {
                        i += rows;
                        i = MIN ((guint) i, max);
                    }

                    gtk_tree_path_free (path);
                    path = gtk_tree_path_new_from_indices (i, -1);
                    gtk_tree_model_get_iter (model, &iter, path);
                }
                else
                {
                    guint i;
                    gboolean (*move_fn) (GtkTreeModel *, GtkTreeIter *);

                    if (((gchar *) rowid->ptr)[1] == 't')
                        move_fn = donna_tree_model_iter_previous;
                    else
                        move_fn = donna_tree_model_iter_next;

                    for (i = 1; i < rows; )
                    {
                        if (!move_fn (model, &iter))
                        {
                            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                                    DONNA_TREE_VIEW_ERROR_OTHER,
                                    "Treeview '%s': Failed moving around",
                                    priv->name);
                            return FALSE;
                        }
                        if (is_row_accessible (tree, &iter))
                            ++i;
                    }
                    path = gtk_tree_model_get_path (model, &iter);
                }

            }
            is_tb = 2;
        }
move:
        if (action == DONNA_TREE_SEL_SELECT || action == DONNA_TREE_SEL_UNSELECT
                || action == DONNA_TREE_SEL_INVERT)
        {
            DonnaTreeRowId rid;
            DonnaTreeRow r;
            GSList *l;

            rid.type = DONNA_ARG_TYPE_ROW;
            rid.ptr  = &r;

            gtk_tree_model_get (model, &iter,
                    DONNA_TREE_VIEW_COL_NODE,   &r.node,
                    -1);
            l = g_hash_table_lookup (priv->hashtable, r.node);
            r.iter = l->data;

            donna_tree_view_selection (tree, action, &rid, to_focused, NULL);
            g_object_unref (r.node);
        }

        if (set & DONNA_TREE_SET_FOCUS)
            gtk_tree_view_set_focused_row (treev, path);
        if (set & DONNA_TREE_SET_CURSOR)
        {
            if (!(set & DONNA_TREE_SET_FOCUS))
                    gtk_tree_view_set_focused_row (treev, path);
            gtk_tree_selection_select_path (
                    gtk_tree_view_get_selection (treev), path);
        }
    }

    if (set & DONNA_TREE_SET_SCROLL)
    {
        /* get visible area, so we can determine if it is already visible */
        gtk_tree_view_get_visible_rect (treev, &rect_visible);

        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        if (nb_type == DONNA_TREE_GOTO_LINE)
        {
            /* when going to a specific line, let's center it */
            if (rect.y < 0 || rect.y > rect_visible.height - rect.height)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.5, 0.0);
        }
        else
        {
            /* only scroll if not visible. Using FALSE is supposed to get the tree
             * to do the minimum of scrolling, but that's apparently prety bugged,
             * and sometimes for a row only half visible on the bottom, GTK feels
             * that minimum scrolling means putting it on top (!!).
             * So, this is why we force it ourself as such. */
            if (rect.y < 0)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.0, 0.0);
            if (rect.y > rect_visible.height - rect.height)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 1.0, 0.0);
        }
    }

    gtk_tree_path_free (path);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return TRUE;
}

DonnaNode *
donna_tree_view_get_node_at_row (DonnaTreeView  *tree,
                                 DonnaTreeRowId *rowid,
                                 GError        **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;
    DonnaNode    *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot get node, invalid row-id",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);

    return node;
}

void
donna_tree_view_set_key_mode (DonnaTreeView *tree, const gchar *key_mode)
{
    DonnaTreeViewPrivate *priv;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    priv = tree->priv;

    g_free (priv->key_mode);
    priv->key_mode = g_strdup (key_mode);
    check_statuses (tree, STATUS_CHANGED_ON_KEYMODE);
}

gboolean
donna_tree_view_remove_row (DonnaTreeView   *tree,
                            DonnaTreeRowId  *rowid,
                            GError         **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter iter;
    row_id_type type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (!is_tree (tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "Treeview '%s': Cannot remove row in mode List",
                priv->name);
        return FALSE;
    }
    else if (!priv->is_minitree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot remove row, option is_minitree not enabled",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot remove row, invalid row-id",
                priv->name);
        return FALSE;
    }

    remove_row_from_tree (tree, &iter, FALSE);
    return TRUE;
}

void
donna_tree_view_reset_keys (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    priv = tree->priv;

    g_free (priv->key_mode);
    priv->key_mode = NULL;

    /* wrong_key */
    g_free (priv->key_combine_name);
    priv->key_combine_name = NULL;
    priv->key_combine = 0;
    priv->key_combine_spec = 0;
    priv->key_spec_type = SPEC_NONE;
    priv->key_m = 0;
    priv->key_val = 0;
    priv->key_motion_m = 0;
    priv->key_motion = 0;

    check_statuses (tree, STATUS_CHANGED_ON_KEYS | STATUS_CHANGED_ON_KEYMODE);
}

/* mode list only */
void
donna_tree_view_abort (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    priv = tree->priv;

    if (priv->get_children_task)
    {
        if (!(donna_task_get_state (priv->get_children_task) & DONNA_TASK_POST_RUN))
            donna_task_cancel (priv->get_children_task);
        g_object_unref (priv->get_children_task);
        priv->get_children_task = NULL;
    }
}

GPtrArray *
donna_tree_view_get_nodes (DonnaTreeView      *tree,
                           DonnaTreeRowId     *rowid,
                           gboolean            to_focused,
                           GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GtkTreeIter iter_last;
    GtkTreeIter iter;
    row_id_type type;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "Treeview '%s': Cannot get nodes, invalid row-id",
                priv->name);
        return NULL;
    }

    if (is_tree (tree) && type == ROW_ID_ROW && to_focused)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
                "Treeview '%s': Cannot get nodes using 'to_focused' flag in mode tree",
                priv->name);
        return NULL;
    }

    if (type == ROW_ID_ROW)
    {
        if (to_focused)
        {
            GtkTreePath *path_focus;
            GtkTreePath *path;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (!path_focus)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot get nodes, failed to get focused row",
                        priv->name);
                return NULL;
            }
            path = gtk_tree_model_get_path (model, &iter);
            if (!path)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Treeview '%s': Cannot get nodes, failed to get path",
                        priv->name);
                gtk_tree_path_free (path_focus);
                return NULL;
            }

            if (gtk_tree_path_compare (path, path_focus) > 0)
            {
                gtk_tree_model_get_iter (model, &iter, path_focus);
                gtk_tree_model_get_iter (model, &iter_last, path);
            }
            else
                gtk_tree_model_get_iter (model, &iter_last, path_focus);

            gtk_tree_path_free (path_focus);
            gtk_tree_path_free (path);
        }
        else
            iter_last = iter;
    }
    else
        if (!gtk_tree_model_iter_children (model, &iter, NULL))
            /* empty tree, let's just return "nothing" then */
            return g_ptr_array_new ();

    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    arr = g_ptr_array_new_with_free_func (g_object_unref);
    for (;;)
    {
        if (type != ROW_ID_SELECTION
                || gtk_tree_selection_iter_is_selected (sel, &iter))
        {
            DonnaNode *node;

            gtk_tree_model_get (model, &iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (G_LIKELY (node))
                g_ptr_array_add (arr, node);
        }

        if ((type == ROW_ID_ROW && itereq (&iter, &iter_last))
                || !gtk_tree_model_iter_next (model, &iter))
            break;
    }

    return arr;
}

/* mode list only */
static DonnaTaskState
history_goto (DonnaTask *task, DonnaNode *node)
{
    GError *err = NULL;
    GValue v = G_VALUE_INIT;
    DonnaNodeHasValue has;
    DonnaTreeView *tree;
    DonnaHistoryDirection direction;
    DonnaTaskState ret = DONNA_TASK_DONE;

    donna_node_get (node, FALSE, "history-tree", &has, &v, NULL);
    if (G_UNLIKELY (has != DONNA_NODE_VALUE_SET))
        /* current location; nothing to do */
        return DONNA_TASK_DONE;
    tree = g_value_get_object (&v);
    g_value_unset (&v);

    donna_node_get (node, FALSE, "history-direction", &has, &v, NULL);
    direction = g_value_get_uint (&v);
    g_value_unset (&v);

    donna_node_get (node, FALSE, "history-pos", &has, &v, NULL);

    if (!donna_tree_view_history_move (tree, direction, g_value_get_uint (&v), &err))
    {
        ret = DONNA_TASK_FAILED;
        donna_task_take_error (task, err);
    }
    g_value_unset (&v);

    return ret;
}

/* mode list only */
static DonnaNode *
get_node_for_history (DonnaTreeView         *tree,
                      DonnaProviderInternal *pi,
                      const gchar           *name,
                      DonnaHistoryDirection  direction,
                      guint                  nb,
                      GError               **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;

    node = donna_provider_internal_new_node (pi, name, NULL, NULL,
            (internal_worker_fn) history_goto, NULL, NULL, error);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (error, "Treeview '%s': Failed to get history; "
                "couldn't create node: ",
                priv->name);
        return NULL;
    }

    /* no direction == node for current location */
    if (direction == 0)
        return node;

    g_value_init (&v, DONNA_TYPE_TREE_VIEW);
    g_value_set_object (&v, tree);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-tree",
                    DONNA_TYPE_TREE_VIEW, &v,
                    (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Treeview '%s': Failed to get history; "
                "couldn't add property 'history-tree': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, direction);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-direction",
                    G_TYPE_UINT, &v, (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Treeview '%s': Failed to get history; "
                "couldn't add property 'history-direction': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_uint (&v, nb);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-pos",
                    G_TYPE_UINT, &v, (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Treeview '%s': Failed to get history; "
                "couldn't add property 'history-pos': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    return node;
}

/* mode list only */
GPtrArray *
donna_tree_view_history_get (DonnaTreeView          *tree,
                             DonnaHistoryDirection   direction,
                             guint                   nb,
                             GError                **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaProviderInternal *pi;
    DonnaNode *node;
    GPtrArray *arr;
    gchar **items, **s;
    gchar *name;
    guint pos;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (!(direction & (DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot get history, no valid direction(s) given",
                priv->name);
        return NULL;
    }

    pi = (DonnaProviderInternal *) donna_app_get_provider (priv->app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot get history, failed to get provider 'internal'",
                priv->name);
        return NULL;
    }

    arr = g_ptr_array_new_with_free_func (g_object_unref);

    if (direction & DONNA_HISTORY_BACKWARD)
    {
        items = donna_history_get_items (priv->history, DONNA_HISTORY_BACKWARD,
                nb, error);
        if (G_UNLIKELY (!items))
        {
            g_prefix_error (error, "Treeview '%s': Failed to get history: ",
                    priv->name);
            g_ptr_array_unref (arr);
            g_object_unref (pi);
            return NULL;
        }

        for (s = items, pos = 0; *s; ++s)
            ++pos;

        /* we got items from oldest to most recent. This is the order we want to
         * preserve if we're also showing FORWARD, so it all makes sense.
         * However, if only showing BACKWARD we shall reverse them, as then the
         * expectation would be to have the most recent first */
        if (direction & DONNA_HISTORY_FORWARD)
            /* reset to first */
            s = items;
        else if (s > items)
            /* back to last (unless first == last == NULL, i.e. no items) */
            --s;

        while (*s)
        {
            if (streqn (*s, "fs:", 3))
                name = *s + 3;
            else
                name = *s;

            node = get_node_for_history (tree, pi, name,
                    DONNA_HISTORY_BACKWARD, pos--, error);
            if (G_UNLIKELY (!node))
            {
                g_strfreev (items);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);

            if (direction & DONNA_HISTORY_FORWARD)
                ++s;
            /* got back to the first item? */
            else if (s == items)
                break;
            else
                --s;
        }
        g_strfreev (items);

        /* if there's also forward, we add the current location on the list */
        if (direction & DONNA_HISTORY_FORWARD)
        {
            name = (gchar *) donna_history_get_item (priv->history,
                    DONNA_HISTORY_BACKWARD, 0, error);
            if (G_UNLIKELY (!name))
            {
                g_prefix_error (error, "Treeview '%s': Failed to get history; "
                        "couldn't get item: ",
                        priv->name);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }

            if (streqn (name, "fs:", 3))
                name += 3;

            node = get_node_for_history (tree, pi, name, 0, 0, error);
            if (G_UNLIKELY (!node))
            {
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);

            /* TODO add property menu-sensitive=FALSE so when displayed on menu
             * it can't be clicked. Also a menu-bold or something would be nice
             * as well... Also a different icon is needed */
        }
    }

    if (direction & DONNA_HISTORY_FORWARD)
    {
        items = donna_history_get_items (priv->history, DONNA_HISTORY_FORWARD,
                nb, error);
        if (G_UNLIKELY (!items))
        {
            g_prefix_error (error, "Treeview '%s': Failed to get history: ",
                    priv->name);
            g_ptr_array_unref (arr);
            g_object_unref (pi);
            return NULL;
        }

        pos = 0;
        for (s = items; *s; ++s)
        {
            if (streqn (*s, "fs:", 3))
                name = *s + 3;
            else
                name = *s;

            node = get_node_for_history (tree, pi, name,
                    DONNA_HISTORY_FORWARD, ++pos, error);
            if (G_UNLIKELY (!node))
            {
                g_strfreev (items);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);
        }
        g_strfreev (items);
    }

    g_object_unref (pi);
    return arr;
}

/* mode list only */
DonnaNode *
donna_tree_view_history_get_node (DonnaTreeView          *tree,
                                  DonnaHistoryDirection   direction,
                                  guint                   nb,
                                  GError                **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaProviderInternal *pi;
    DonnaNode *node;
    const gchar *item;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    pi = (DonnaProviderInternal *) donna_app_get_provider (priv->app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Cannot get history node, "
                "failed to get provider 'internal'",
                priv->name);
        return NULL;
    }

    item = donna_history_get_item (priv->history, direction, nb, error);
    if (!item)
    {
        g_prefix_error (error, "Treeview '%s': Failed getting history node: ",
                priv->name);
        g_object_unref (pi);
        return NULL;
    }

    node = get_node_for_history (tree, pi,
            (streqn ("fs:", item, 3)) ? item + 3 : item,
            direction, nb, error);
    g_object_unref (pi);
    return node;
}

/* mode list only */
static void
history_move_get_node_cb (DonnaTask             *task,
                          gboolean               timeout_called,
                          struct history_move   *hm)
{
    GError *err = NULL;
    DonnaTreeViewPrivate *priv = hm->tree->priv;
    DonnaTaskState state;

    state = donna_task_get_state (task);
    if (state == DONNA_TASK_FAILED)
    {
        donna_app_show_error (priv->app, donna_task_get_error (task),
                "Treeview '%s': Failed to move in history; "
                "getting destination node failed",
                priv->name);
        return;
    }
    else if (state == DONNA_TASK_CANCELLED)
        return;

    if (!change_location (hm->tree, CHANGING_LOCATION_ASKED,
                g_value_get_object (donna_task_get_return_value (task)), hm, &err))
        donna_app_show_error (priv->app, err,
                "Treeview '%s': Failed to move in history",
                priv->name);
    free_history_move (hm);
}

/* mode list only */
gboolean
donna_tree_view_history_move (DonnaTreeView         *tree,
                              DonnaHistoryDirection  direction,
                              guint                  nb,
                              GError               **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaTask *task;
    const gchar *fl;
    struct history_move *data;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    priv = tree->priv;

    fl = donna_history_get_item (priv->history, direction, nb, error);
    if (!fl)
    {
        g_prefix_error (error, "Treeview '%s': Failed to move in history: ",
                priv->name);
        return FALSE;
    }

    task = donna_app_get_node_task (priv->app, fl);
    if (G_UNLIKELY (!task))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Canot move in history; "
                "failed to create task to get node '%s'",
                priv->name, fl);
        return FALSE;
    }

    data = g_slice_new (struct history_move);
    data->tree = tree;
    data->direction = direction;
    data->nb = nb;

    donna_task_set_callback (task,
            (task_callback_fn) history_move_get_node_cb,
            data,
            (GDestroyNotify) free_history_move);
    donna_app_run_task (priv->app, task);
    return TRUE;
}

/* mode list only */
gboolean
donna_tree_view_history_clear (DonnaTreeView        *tree,
                               DonnaHistoryDirection direction,
                               GError              **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    priv = tree->priv;

    if (is_tree (tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "Treeview '%s': No history in mode Tree",
                priv->name);
        return FALSE;
    }

    donna_history_clear (priv->history, direction);
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
    if (!is_tree (tree) && tree->priv->cl >= CHANGING_LOCATION_SLOW)
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
#else
            /* because (only) in vanilla, we could be there for the blank
             * column, which isn't in our internal list of columns */
            if (_col)
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
    if (G_UNLIKELY (!loc_node))
        return;
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
            gtk_tree_view_set_focused_row (treev, loc_path);
            if (n == loc_node)
            {
                sel = gtk_tree_view_get_selection (treev);
                gtk_tree_selection_select_path (sel, loc_path);
            }
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

#define is_regular_left_click(click, event)             \
    ((click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))  \
     == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)         \
     && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))

enum
{
    CLICK_REGULAR = 0,
    CLICK_ON_BLANK,
    CLICK_ON_EXPANDER,
};

struct conv
{
    DonnaTreeView *tree;
    DonnaTreeRow  *row;
    gchar         *col_name;
    guint          key_m;
};

static gboolean
tree_conv_flag (const gchar      c,
                DonnaArgType    *type,
                gpointer        *ptr,
                GDestroyNotify  *destroy,
                struct conv     *conv)
{
    DonnaTreeViewPrivate *priv = conv->tree->priv;

    switch (c)
    {
        case 'o':
            *type = DONNA_ARG_TYPE_TREEVIEW;
            *ptr = conv->tree;
            return TRUE;

        case 'L':
            if (G_UNLIKELY (!priv->location))
                return FALSE;
            *type = DONNA_ARG_TYPE_STRING;
            if (streq ("fs", donna_node_get_domain (priv->location)))
                *ptr = donna_node_get_location (priv->location);
            else
                *ptr = donna_node_get_full_location (priv->location);
            *destroy = g_free;
            return TRUE;

        case 'l':
            if (G_UNLIKELY (!priv->location))
                return FALSE;
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = priv->location;
            return TRUE;

        case 'R':
            if (G_UNLIKELY (!conv->col_name))
                return FALSE;
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = conv->col_name;
            return TRUE;

        case 'r':
            if (G_UNLIKELY (!conv->row))
                return FALSE;
            *type = DONNA_ARG_TYPE_ROW;
            *ptr = conv->row;
            return TRUE;

        case 'N':
            if (G_UNLIKELY (!conv->row))
                return FALSE;
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = donna_node_get_location (conv->row->node);
            *destroy = g_free;
            return TRUE;

        case 'n':
            if (G_UNLIKELY (!conv->row))
                return FALSE;
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = conv->row->node;
            return TRUE;

        /* keys only */

        case 'm':
            *type = DONNA_ARG_TYPE_INT;
            *ptr = &conv->key_m;
            return TRUE;
    }

    return FALSE;
}

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
    struct conv conv = { NULL, };
    struct column *_col;
    GPtrArray *intrefs = NULL;
    gchar *fl;
    gboolean is_tree = is_tree (tree);
    gboolean is_selected;
    gchar *clicks = NULL;
    /* longest possible is "blankcol_ctrl_shift_middle_double_click_on_rls"
     * (len=46) */
    gchar buf[47];
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

    _col = get_column_by_column (tree, column);
    conv.tree = tree;
    if (_col)
        conv.col_name = _col->name;

    if (!iter)
    {
        memcpy (buf, "blankrow_", 9 * sizeof (gchar));
        b = buf;
    }
    else if (!_col)
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


    if (is_tree && iter)
        gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
                DONNA_TREE_COL_CLICKS,  &clicks,
                -1);

    /* list only: different source when the clicked item is selected */
    is_selected = !is_tree && iter && gtk_tree_selection_iter_is_selected (
            gtk_tree_view_get_selection ((GtkTreeView *) tree), iter);

    config = donna_app_peek_config (priv->app);

    if (event->type == GDK_BUTTON_PRESS && !priv->on_release_triggered)
    {
        gboolean on_rls = FALSE;
        gchar *e;

        e = b + strlen (b);
        memcpy (e, "_on_rls", 8 * sizeof (gchar)); /* 8 to include NUL */

        /* should we delay the trigger to button-release ? */
        if (!_donna_config_get_boolean_tree_column (config, priv->name,
                    conv.col_name,
                    (is_tree) ? TREE_COL_TREE : (is_selected) ? TREE_COL_LIST_SELECTED : TREE_COL_LIST,
                    (is_tree) ? clicks
                    : (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    (is_tree) ? "treeviews/tree" : "treeviews/list",
                    b, &on_rls) && is_selected)
            /* nothing found under "selected", fallback to regular clicks */
            _donna_config_get_boolean_tree_column (config, priv->name,
                    conv.col_name,
                    TREE_COL_LIST,
                    (priv->arrangement) ?  priv->arrangement->columns_options : NULL,
                    "treeviews/list",
                    b, &on_rls);

        if (on_rls)
        {
            priv->on_release_click  = click;
            priv->on_release_x      = event->x;
            priv->on_release_y      = event->y;
            return;
        }
        *e = '\0';
    }

    /* a few of those should have valid defaults, just in case */
    if (is_tree)
    {
        if (streq (b, "left_click"))
            def = "command:tree_set_cursor (%o, %r)";
        else if (streq (b, "left_double_click")
                || streq (b, "expander_left_click"))
            def = "command:tree_toggle_row (%o, %r, standard)";
    }
    else
    {
        if (streq (b, "left_click"))
            def = "command:tree_set_focus (%o, %r)";
        else if (streq (b, "blank_left_click")
                || streq (b, "blankcol_left_click")
                || streq (b, "blankrow_left_click"))
            def = "command:tree_selection (%o, unselect, :all, )";
        else if (streq (b, "left_double_click"))
            def = "command:tree_activate_row (%o, %r)";
    }

    fl = _donna_config_get_string_tree_column (config, priv->name,
            conv.col_name,
            (is_tree) ? TREE_COL_TREE : (is_selected) ? TREE_COL_LIST_SELECTED : TREE_COL_LIST,
            (is_tree) ? clicks
            : (priv->arrangement) ? priv->arrangement->columns_options : NULL,
            (is_tree) ? "treeviews/tree" : "treeviews/list",
            b, (gchar *) def);
    if (!fl && is_selected)
        /* nothing found under "selected", fallback to regular clicks */
        fl = _donna_config_get_string_tree_column (config, priv->name,
                conv.col_name,
                TREE_COL_LIST,
                (priv->arrangement) ?  priv->arrangement->columns_options : NULL,
                "treeviews/list",
                b, (gchar *) def);

    g_free (clicks);

    if (!fl)
        return;

    if (iter)
        conv.row = get_row_for_iter (tree, iter);

    fl = donna_app_parse_fl (priv->app, fl, "olLrRnN",
            (conv_flag_fn) tree_conv_flag, &conv, &intrefs);

    if (iter)
        g_free (conv.row);

    donna_app_trigger_fl (priv->app, fl, intrefs, FALSE, NULL);
    g_free (fl);
}

/* for obvious reason (grabbing the focus happens here) this can only be called
 * once per click. However, we might call this twice, first checking if a rubber
 * banding operation can start or not, and then when the trigger_click() occurs.
 * The way we handle this is that if tree_might_grab_focus is NULL there will be
 * no focus grabbed, since the rubber banding is list-only and there we don't
 * care about this (i.e. it's NULL) */
static gboolean
skip_focusing_click (DonnaTreeView  *tree,
                     DonnaClick      click,
                     GdkEventButton *event,
                     gboolean       *tree_might_grab_focus)
{
    gboolean might_grab_focus = FALSE;

    /* a click will grab the focus if:
     * - tree: it's a regular left click (i.e. no Ctrl/Shift held) unless
     *   click was on expander
     * - list: it's a left click (event w/ Ctrl/Shift)
     * and, ofc, focus isn't on treeview already. */
    if (is_tree (tree))
        /* might, as we don't know if this was on expander or not yet */
        might_grab_focus = is_regular_left_click (click, event)
            && !gtk_widget_is_focus ((GtkWidget *) tree);
    else if ((click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))
            == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)
            && !gtk_widget_is_focus ((GtkWidget *) tree))
    {
        GtkWidget *w = NULL;

        if (tree->priv->focusing_click)
            /* get the widget that currently has the focus */
            w = gtk_window_get_focus ((GtkWindow *) gtk_widget_get_toplevel (
                        (GtkWidget *) tree));

        /* see notes above for why */
        if (tree_might_grab_focus)
            gtk_widget_grab_focus ((GtkWidget *) tree);

        /* we "skip" the click if focusing_click, unless the widget that had
         * the focus was a children of ours, i.e. a column header */
        if (tree->priv->focusing_click && w && gtk_widget_get_ancestor (w,
                    DONNA_TYPE_TREE_VIEW) != (GtkWidget *) tree)
            return TRUE;
    }

    if (tree_might_grab_focus)
        *tree_might_grab_focus = might_grab_focus;
    return FALSE;
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
    gboolean tree_might_grab_focus;

    if (event->button == 1)
        click |= DONNA_CLICK_LEFT;
    else if (event->button == 2)
        click |= DONNA_CLICK_MIDDLE;
    else if (event->button == 3)
        click |= DONNA_CLICK_RIGHT;

    /* the focus thing only matters on the actual click (i.e. on press), so we
     * ignore it when triggering a click on release */
    if (event->type == GDK_BUTTON_PRESS
            && skip_focusing_click (tree, click, event, &tree_might_grab_focus))
        return FALSE;

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
        {
            if (tree_might_grab_focus)
                gtk_widget_grab_focus ((GtkWidget *) tree);
            handle_click (tree, click, event, &iter, column, renderer, CLICK_ON_BLANK);
        }
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
                /* i.e. clicked on an expander (never grab focus) */
                handle_click (tree, click, event, &iter, column, renderer,
                        CLICK_ON_EXPANDER);
                return TRUE;
            }
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
#endif
                as = get_as_for_node (tree, node, &as_idx, FALSE);

            if (!as)
            {
                if (tree_might_grab_focus)
                    gtk_widget_grab_focus ((GtkWidget *) tree);
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
            if (tree_might_grab_focus)
                gtk_widget_grab_focus ((GtkWidget *) tree);
            /* there was no as for this column */
            handle_click (tree, click, event, &iter, column, renderer, CLICK_REGULAR);
        }
    }
    else
    {
        if (tree_might_grab_focus)
            gtk_widget_grab_focus ((GtkWidget *) tree);
        handle_click (tree, click, event, NULL, NULL, NULL, CLICK_ON_BLANK);
    }
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

#ifdef GTK_IS_JJK
    gint x, y;

    /* rubber band only happens on left click */
    if (event->button == 1)
    {
        /* make sure we're over a row, i.e. not on blank space after the last
         * row, because that would cause trouble/is unsupported by GTK */
        gtk_tree_view_convert_bin_window_to_widget_coords ((GtkTreeView *) tree,
                event->x, event->y, &x, &y);
        if (gtk_tree_view_get_tooltip_context ((GtkTreeView *) tree, &x, &y, 0,
                    NULL, NULL, NULL)
                /* don't start if this is a focusing click to be skipped. We
                 * know it's LEFT, it might not be SINGLE but pretending should
                 * be fine, since anything else wouldn't be a focusing click */
                && !is_tree (tree) && !skip_focusing_click (tree,
                    DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT, event, NULL))
            /* this will only "prepare", the actual operation starts if there's
             * a drag/motion. If/when that happens, signal rubber-banding-active
             * will be emitted. Either way, the click will be processed as
             * usual. */
            gtk_tree_view_start_rubber_banding ((GtkTreeView *) tree, event);
    }
#endif

    priv->on_release_triggered = FALSE;

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
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) widget)->priv;
    gboolean ret;
    GSList *l;

#ifdef GTK_IS_JJK
    if (gtk_tree_view_is_rubber_banding_active ((GtkTreeView *) widget))
        /* this ensures stopping rubber banding will not move the focus */
        gtk_tree_view_stop_rubber_banding ((GtkTreeView *) widget, FALSE);
#endif

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
    for (l = priv->columns; l; l = l->next)
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

    if (priv->on_release_click)
    {
        gint distance;

        g_object_get (gtk_settings_get_default (),
                "gtk-double-click-distance",    &distance,
                NULL);

        /* only validate/trigger the click on release if it's within dbl-click
         * distance of the press event */
        if ((ABS (event->x - priv->on_release_x) <= distance)
                && ABS (event->y - priv->on_release_y) <= distance)
            trigger_click ((DonnaTreeView *) widget, priv->on_release_click, event);

        priv->on_release_click = 0;
    }
    else
        priv->on_release_triggered = TRUE;

    return ret;
}

#ifdef GTK_IS_JJK
static void
donna_tree_view_rubber_banding_active (GtkTreeView *treev)
{
    /* by default GTK will here toggle the row if Ctrl was held, to undo the
     * toggle it does when starting the rubebr band, since it assumes there was
     * one on button-press.
     * Since that assumption isn't valid for us, we simply do nothing (no chain
     * up) to not have this behavior.
     *
     * Of course, if our click w/ Ctrl did do a toggle, then when it gets
     * processes it will undo the toggle of the rubber band, thus creating a
     * "glitch."
     * TODO: The way we'll deal with this is by having a event of ours, where a
     * script could run, check if a toggle is needed and if so do it. That might
     * leave a "visual glitch" since the click is processed after a delay, but
     * it's only a small thing, and at least we'll get expected results.
     */
}
#endif

static inline gchar *
find_key_config (DonnaTreeView *tree, DonnaConfig *config, gchar *key)
{
    if (donna_config_has_category (config, "treeviews/%s/keys%s%s/key_%s",
                tree->priv->name,
                (tree->priv->key_mode) ? "/" : "",
                (tree->priv->key_mode) ? tree->priv->key_mode : "",
                key))
        return g_strdup_printf ("treeviews/%s/keys/key_%s", tree->priv->name, key);
    if (donna_config_has_category (config, "defaults/treeviews/%s/keys%s%s/key_%s",
                (is_tree (tree)) ? "tree" : "list",
                (tree->priv->key_mode) ? "/" : "",
                (tree->priv->key_mode) ? tree->priv->key_mode : "",
                key))
        return g_strdup_printf ("defaults/treeviews/%s/keys%s%s/key_%s",
                (is_tree (tree)) ? "tree" : "list",
                (tree->priv->key_mode) ? "/" : "",
                (tree->priv->key_mode) ? tree->priv->key_mode : "",
                key);
    return NULL;
}

static gint
find_key_from (DonnaTreeView *tree,
               DonnaConfig   *config,
               gchar        **key,
               gchar        **alias,
               gchar        **from)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gint level = 0;
    gint type;

    *from = find_key_config (tree, config, *key);
    if (!*from)
        return -1;

repeat:
    if (!donna_config_get_int (config, &type, "%s/type", *from))
        /* default */
        type = KEY_DIRECT;

    if (type == KEY_DISABLED)
        return -1;
    else if (type == KEY_ALIAS)
    {
        if (!donna_config_get_string (config, alias, "%s/key", *from))
        {
            g_warning ("Treeview '%s': Key '%s' of type ALIAS without alias set",
                    priv->name, *key);
            return -1;
        }
        g_free (*from);
        *from = find_key_config (tree, config, *alias);
        if (!*from)
            return -1;
        *key = *alias;
        if (++level > 10)
        {
            g_warning ("Treeview '%s': There might be an infinite loop in key aliasing, "
                    "bailing out on key '%s' reaching level %d",
                    priv->name, *key, level);
            return -1;
        }
        goto repeat;
    }
    return type;
}

#define wrong_key(beep) do {            \
    if (beep)                           \
    {                                   \
        printf ("\a"); /* beep */       \
        fflush (stdout);                \
    }                                   \
    g_free (from);                      \
    g_free (alias);                     \
    g_free (priv->key_combine_name);    \
    priv->key_combine_name = NULL;      \
    priv->key_combine = 0;              \
    priv->key_combine_spec = 0;         \
    priv->key_spec_type = SPEC_NONE;    \
    priv->key_m = 0;                    \
    priv->key_val = 0;                  \
    priv->key_motion_m = 0;             \
    priv->key_motion = 0;               \
    check_statuses (tree,               \
            STATUS_CHANGED_ON_KEYS);    \
    return TRUE;                        \
} while (0)

/* we parse those two on our own (i.e. not through donna_app_parse_fl())
 * because those are single-character, and we don't want them treated as
 * strings, because that would get them to be quoted (which isn't nice) */
static inline void
parse_specs (gchar *str, gchar spec, gchar combine)
{
    while ((str = strchr (str, '%')))
    {
        gchar *s;
        gint n = 1;

        if (str[1] == 's')
        {
            if (spec > 0)
                *str = spec;
            else
                ++n;
        }
        else if (str[1] == 'c')
        {
            if (combine > 0)
                *str = combine;
            else
                ++n;
        }
        else
        {
            str += 2;
            continue;
        }

        for (s = str + ((n == 1) ? 1 : 0); ; ++s)
        {
            *s = s[n];
            if (*s == '\0')
                break;
        }
        ++str;
    }
}

static gboolean
trigger_key (DonnaTreeView *tree, gchar spec)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    GtkTreePath *path;
    GtkTreeIter iter;
    gchar *key;
    gchar *alias = NULL;
    gchar *from  = NULL;
    gchar *fl;
    struct conv conv = { NULL, };
    GPtrArray *intrefs = NULL;

    config = donna_app_peek_config (priv->app);
    conv.tree = tree;

    /* is there a motion? */
    if (priv->key_motion)
    {
        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (!path)
            wrong_key (TRUE);
        gtk_tree_model_get_iter ((GtkTreeModel *) priv->store, &iter, path);
        gtk_tree_path_free (path);

        key = gdk_keyval_name (priv->key_motion);
        if (G_UNLIKELY (find_key_from (tree, config, &key, &alias, &from) == -1))
            wrong_key (TRUE);
        if (!donna_config_get_string (config, &fl, "%s/trigger", from))
            wrong_key (TRUE);

        parse_specs (fl, spec, 0);
        conv.key_m = priv->key_motion_m;
        conv.row = get_row_for_iter (tree, &iter);

        fl = donna_app_parse_fl (priv->app, fl, "olLrnNm",
                (conv_flag_fn) tree_conv_flag, &conv, &intrefs);
        if (!donna_app_trigger_fl (priv->app, fl, intrefs, TRUE, NULL))
        {
            g_free (fl);
            return TRUE;
        }
        g_free (fl);
        intrefs = NULL;
    }

    key = gdk_keyval_name (priv->key_val);
    if (G_UNLIKELY (find_key_from (tree, config, &key, &alias, &from) == -1))
        wrong_key (TRUE);
    if (!donna_config_get_string (config, &fl, "%s/trigger", from))
        wrong_key (TRUE);

    if (!conv.row)
    {
        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (path)
        {
            gtk_tree_model_get_iter ((GtkTreeModel *) priv->store, &iter, path);
            gtk_tree_path_free (path);
            conv.row = get_row_for_iter (tree, &iter);
        }
    }

    parse_specs (fl, spec, priv->key_combine_spec);
    conv.key_m = priv->key_m;
    fl = donna_app_parse_fl (priv->app, fl, "olLrnNm",
            (conv_flag_fn) tree_conv_flag, &conv, &intrefs);
    g_free (conv.row);
    donna_app_trigger_fl (priv->app, fl, intrefs, FALSE, NULL);
    g_free (fl);

    g_free (from);
    g_free (alias);
    g_free (priv->key_combine_name);
    priv->key_combine_name = NULL;
    priv->key_combine = 0;
    priv->key_combine_spec = 0;
    priv->key_spec_type = SPEC_NONE;
    priv->key_m = 0;
    priv->key_val = 0;
    priv->key_motion_m = 0;
    priv->key_motion = 0;
    check_statuses (tree, STATUS_CHANGED_ON_KEYS);
    return FALSE;
}

static gboolean
donna_tree_view_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
    DonnaTreeView *tree = (DonnaTreeView *) widget;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    gchar *key;
    gchar *alias = NULL;
    gchar *from  = NULL;
    enum key_type type;
    gint i;

    /* ignore modifier or AltGr */
    if (event->is_modifier || event->keyval == GDK_KEY_ISO_Level3_Shift)
        return FALSE;

    config = donna_app_peek_config (priv->app);
    key = gdk_keyval_name (event->keyval);
    if (!key)
        return FALSE;

    g_debug("key=%s",key);

    if (priv->key_spec_type != SPEC_NONE)
    {
        if (priv->key_spec_type & SPEC_LOWER)
            if (event->keyval >= GDK_KEY_a && event->keyval <= GDK_KEY_z)
                goto next;
        if (priv->key_spec_type & SPEC_UPPER)
            if (event->keyval >= GDK_KEY_A && event->keyval <= GDK_KEY_Z)
                goto next;
        if (priv->key_spec_type & SPEC_DIGITS)
            if ((event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
                    || (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9))
                goto next;
        if ((priv->key_spec_type & SPEC_EXTRA)
                && strchr (SPEC_EXTRA_CHARS, gdk_keyval_to_unicode (event->keyval)))
            goto next;
        if (priv->key_spec_type & SPEC_MOTION)
        {
            gboolean is_motion = FALSE;

            if (priv->key_motion_m == 0 && event->keyval == priv->key_val)
            {
                priv->key_spec_type = SPEC_NONE;
                goto next;
            }

            if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
            {
                /* modifier */
                priv->key_motion_m *= 10;
                priv->key_motion_m += event->keyval - GDK_KEY_0;
                check_statuses (tree, STATUS_CHANGED_ON_KEYS);
                return TRUE;
            }
            else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9)
            {
                /* modifier */
                priv->key_motion_m *= 10;
                priv->key_motion_m += event->keyval - GDK_KEY_KP_0;
                check_statuses (tree, STATUS_CHANGED_ON_KEYS);
                return TRUE;
            }

            if (find_key_from (tree, config, &key, &alias, &from) == -1)
                wrong_key (TRUE);

            donna_config_get_boolean (config, &is_motion, "%s/is_motion", from);
            if (is_motion)
                goto next;
        }
        wrong_key (TRUE);
        /* not reached */
next:
        if (priv->key_combine_name && priv->key_combine_spec == 0)
        {
            priv->key_combine_spec = (gchar) gdk_keyval_to_unicode (event->keyval);
            priv->key_spec_type = SPEC_NONE;
            g_free (from);
            g_free (alias);
            check_statuses (tree, STATUS_CHANGED_ON_KEYS);
            return TRUE;
        }
    }

    if (priv->key_val)
    {
        /* means the spec was just specified */

        if (priv->key_spec_type & SPEC_MOTION)
        {
            priv->key_spec_type = SPEC_NONE;
            priv->key_motion = event->keyval;

            type = find_key_from (tree, config, &key, &alias, &from);
            if (type == KEY_DIRECT)
                trigger_key (tree, 0);
            else if (type == KEY_SPEC)
            {
                if (!donna_config_get_int (config, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                    /* a motion can't ask for a motion */
                    wrong_key (TRUE);
                priv->key_spec_type = CLAMP (i, 1, 512);
            }
            else
                wrong_key (TRUE);
        }
        else
            trigger_key (tree, (gchar) gdk_keyval_to_unicode (event->keyval));
    }
    else if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
    {
        /* modifier */
        priv->key_m *= 10;
        priv->key_m += event->keyval - GDK_KEY_0;
    }
    else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9)
    {
        /* modifier */
        priv->key_m *= 10;
        priv->key_m += event->keyval - GDK_KEY_KP_0;
    }
    else
    {
        type = find_key_from (tree, config, &key, &alias, &from);
        if (G_UNLIKELY ((gint) type == -1 && event->keyval == GDK_KEY_Escape))
        {
            /* special case: GDK_KEY_Escape will always default to
             * tree_reset_keys if not defined. This is to ensure that if you set
             * a keymode, you can always get back to normal mode (even if you
             * forgot to define the key Escape to do so. Of course if you define
             * it to nothing/something else, it's on you. */
            donna_tree_view_reset_keys (tree);
            return TRUE;
        }

        switch (type)
        {
            case KEY_COMBINE:
                if (priv->key_m > 0 || priv->key_combine_name)
                    /* no COMBINE with a modifier; only one at a time */
                    wrong_key (TRUE);
                if (!donna_config_get_string (config, &priv->key_combine_name,
                            "%s/combine", from))
                {
                    g_warning ("Treeview '%s': Key '%s' missing its name as COMBINE",
                            priv->name, key);
                    wrong_key (TRUE);
                }
                if (!donna_config_get_int (config, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                {
                    g_warning ("Treeview '%s': Key '%s' cannot be COMBINE with spec MOTION",
                            priv->name, key);
                    wrong_key (TRUE);
                }
                priv->key_combine = (gchar) gdk_keyval_to_unicode (event->keyval);
                priv->key_spec_type = CLAMP (i, 1, 512);
                break;

            case KEY_DIRECT:
                priv->key_val = event->keyval;
                trigger_key (tree, 0);
                break;

            case KEY_SPEC:
                priv->key_val = event->keyval;
                if (!donna_config_get_int (config, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                    /* make sure there's no BS like SPEC_LOWER | SPEC_MOTION */
                    i = SPEC_MOTION;
                priv->key_spec_type = CLAMP (i, 1, 512);
                break;

            case KEY_ALIAS:
                /* to silence warning, but it can't happen since find_key_from()
                 * will "resolve" aliases */
                /* fall through */
            case KEY_DISABLED:
                /* fall through */
            default:
                wrong_key (priv->key_m || priv->key_combine_name);
        }
        if (type != KEY_COMBINE && priv->key_combine_name)
        {
            gchar *s;

            if (!donna_config_get_string (config, &s, "%s/combine", from))
                wrong_key (TRUE);
            if (!streq (s, priv->key_combine_name))
            {
                g_free (s);
                wrong_key (TRUE);
            }
            g_free (s);
        }
    }

    g_free (from);
    g_free (alias);
    check_statuses (tree, STATUS_CHANGED_ON_KEYS);
    return TRUE;
}

#undef wrong_key

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

    /* filling_list is also set when clearing the store, because that has GTK
     * trigger *a lot* of selection-changed (even when there's no selection)
     * which in turn would trigger lots of status refresh, which would be a
     * little slow (when there was lots of items) */
    if (!priv->filling_list)
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
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
                if (n)
                    g_object_unref (n);
                else
                    return;
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

        /* ideally this wouldn't happen. There are ways, though, for this to
         * occur. Known ways are:
         *
         * - Moving the focus up/outside the branch, then collapsing the parent
         *   of the selected node. No more selection!
         *   This is handled in donna_tree_view_test_collapse_row()
         *
         * - In minitree, removing the row of current location.
         *   This is handled in remove_row_from_tree()/handle_removing_row()
         *
         * - Then there's the case of the tree going out of sync. When no node
         *   was found, we switch to SINGLE and then unselect. However, the
         *   switch to SIGNLE will apparently emit selection-changed 3 times,
         *   the first one with nothing selected at all, but the mode is still
         *   BROWSE, thus leading here.
         *   This is handled via setting priv->changed_location prior, and
         *   ignoring below when it's set, ignoring the first/problematic
         *   signal.
         *
         *   It should be noted that because multiple signals can still emitted
         *   in other circustances, and a tree can end up doing multiple
         *   set_location() to its sync_with (if the change hasn't completed on
         *   the second signal). Since we can't avoid that, changed_location()
         *   has a special handling for that (ignoring request to change for the
         *   same future_location), see changing_sel_mode() for more.
         *
         * - There might be other ways GTK allows to get the selection removed
         *   in BROWSE, which ideally we should then learn and handle as well;
         *   Meanwhile, let's select the focused row. */

        if (priv->changing_sel_mode)
            return;

        g_warning ("Treeview '%s': the selection was lost in BROWSE mode",
                priv->name);

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (!path)
        {
            if (donna_tree_model_get_count ((GtkTreeModel *) priv->store) == 0)
            {
                /* if there's no more rows on tree, let's make sure we don't
                 * have an old (invalid) current location */
                if (priv->location)
                {
                    g_object_unref (priv->location);
                    priv->location = NULL;
                    priv->location_iter.stamp = 0;
                }
                return;
            }
            path = gtk_tree_path_new_from_string ("0");
        }
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

gboolean
donna_tree_view_is_tree (DonnaTreeView      *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    return is_tree (tree);
}

static void
check_statuses (DonnaTreeView *tree, enum changed_on changed)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);
        if (status->changed_on & changed)
            donna_status_provider_status_changed ((DonnaStatusProvider *) tree,
                    status->id);
    }
}

static guint
status_provider_create_status (DonnaStatusProvider    *sp,
                               gpointer                _name,
                               GError                **error)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    DonnaConfig *config;
    struct status status;
    const gchar *name = _name;
    gchar *s;

    config = donna_app_peek_config (priv->app);
    if (!donna_config_get_string (config, &s, "statusbar/%s/format", name))
    {
        g_set_error (error, DONNA_STATUS_PROVIDER_ERROR,
                DONNA_STATUS_PROVIDER_ERROR_INVALID_CONFIG,
                "Treeview '%s': Status '%s': Option 'format' not found",
                priv->name, name);
        return 0;
    }

    status.id  = ++priv->last_status_id;
    status.fmt = s;
    status.changed_on = 0;

    if (!donna_config_get_int (config, &status.digits, "statusbar/%s/digits", name))
        if (!donna_config_get_int (config, &status.digits, "defaults/size/digits"))
            status.digits = 1;
    if (!donna_config_get_boolean (config, &status.long_unit,
                "statusbar/%s/long_unit", name))
        if (!donna_config_get_boolean (config, &status.long_unit,
                    "defaults/size/long_unit"))
            status.long_unit = FALSE;

    if (!donna_config_get_boolean (config, &status.keymode_colors,
                "statusbar/%s/keymode_colors", name))
        status.keymode_colors = FALSE;
    if (status.keymode_colors)
    {
        status.name = g_strdup (name);
        status.changed_on |= STATUS_CHANGED_ON_KEYMODE;
    }
    else
        /* name only needed to load keymode_colors options */
        status.name = NULL;

    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'K':
                status.changed_on |= STATUS_CHANGED_ON_KEYMODE;
                break;

            case 'k':
                status.changed_on |= STATUS_CHANGED_ON_KEYS;
                break;

            case 'l':
            case 'L':
            case 's':
            case 'S':
            case 'v':
            case 'V':
            case 'a':
            case 'A':
            case 'n':
            case 'N':
                status.changed_on |= STATUS_CHANGED_ON_CONTENT;
                break;
        }
        s += 2;
    }

    g_array_append_val (priv->statuses, status);
    return status.id;
}

static void
status_provider_free_status (DonnaStatusProvider    *sp,
                             guint                   id)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);

        if (status->id == id)
        {
            g_array_remove_index_fast (priv->statuses, i);
            break;
        }
    }
}

static const gchar *
status_provider_get_renderers (DonnaStatusProvider    *sp,
                               guint                   id)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);

        if (status->id == id)
            return "t";
    }
    return NULL;
}

static gboolean
calculate_size (GtkTreeModel    *model,
                GtkTreePath     *path,
                GtkTreeIter     *iter,
                guint64         *total)
{
    DonnaNode *node;
    guint64 size;

    gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return FALSE;
    if (donna_node_get_node_type (node) == DONNA_NODE_ITEM
            && donna_node_get_size (node, TRUE, &size) == DONNA_NODE_VALUE_SET)
        *total += size;
    g_object_unref (node);
    return FALSE; /* keep iterating */
}

static void
st_render_size (DonnaStatusProvider *sp,
                struct status       *status,
                GString             *str,
                gchar                c,
                gchar               *fmt,
                GtkTreeSelection   **sel)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    guint64 size = 0;
    gchar buf[20], *b = buf;
    gssize len;

    switch (c)
    {
        case 'A':
            donna_tree_store_foreach (priv->store,
                    (GtkTreeModelForeachFunc) calculate_size, &size);
            break;

        case 'V':
            gtk_tree_model_foreach ((GtkTreeModel *) priv->store,
                    (GtkTreeModelForeachFunc) calculate_size, &size);
            break;

        case 'S':
            if (!*sel)
                *sel = gtk_tree_view_get_selection ((GtkTreeView *) sp);
            gtk_tree_selection_selected_foreach (*sel,
                    (GtkTreeSelectionForeachFunc) calculate_size, &size);
            break;
    }

    b = buf;
    len = donna_print_size (b, 20, fmt, size, status->digits, status->long_unit);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        donna_print_size (b, len, fmt, size, status->digits, status->long_unit);
    }
    g_string_append (str, b);
    if (b != buf)
        g_free (b);
}

static void
status_provider_render (DonnaStatusProvider    *sp,
                        guint                   id,
                        guint                   index,
                        GtkCellRenderer        *renderer)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    struct status *status;
    GtkTreeSelection *sel = NULL;
    GString *str = NULL;
    gchar *fmt;
    gchar *s;
    gchar *ss;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        status = &g_array_index (priv->statuses, struct status, i);
        if (status->id == id)
            break;
    }
    if (G_UNLIKELY (i >= priv->statuses->len))
    {
        g_warning ("Treeview '%s': Asked to render unknown status #%d",
                priv->name, id);
        return;
    }

    s = fmt = status->fmt;
    str = g_string_new (NULL);
    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'o':
                g_string_append_len (str, fmt, s - fmt);
                g_string_append (str, priv->name);
                s += 2;
                fmt = s;
                break;

            case 'l':
            case 'L':
                g_string_append_len (str, fmt, s - fmt);
                if (G_LIKELY (priv->location))
                {
                    if (s[1] == 'L'
                            && streq ("fs", donna_node_get_domain (priv->location)))
                        ss = donna_node_get_location (priv->location);
                    else
                        ss = donna_node_get_full_location (priv->location);
                    g_string_append (str, ss);
                    g_free (ss);
                }
                else
                    g_string_append_c (str, '-');
                s += 2;
                fmt = s;
                break;

            case 'K':
                g_string_append_len (str, fmt, s - fmt);
                if (priv->key_mode)
                    g_string_append (str, priv->key_mode);
                s += 2;
                fmt = s;
                break;

            case 'k':
                g_string_append_len (str, fmt, s - fmt);
                if (priv->key_combine)
                    g_string_append_c (str, priv->key_combine);
                if (priv->key_combine_spec)
                    g_string_append_c (str, priv->key_combine_spec);
                if (priv->key_m)
                    g_string_append_printf (str, "%d", priv->key_m);
                if (priv->key_val)
                    g_string_append_c (str, gdk_keyval_to_unicode (priv->key_val));
                if (priv->key_motion_m)
                    g_string_append_printf (str, "%d", priv->key_motion_m);
                s += 2;
                fmt = s;
                break;

            case 'a':
                g_string_append_len (str, fmt, s - fmt);
                g_string_append_printf (str, "%d",
                        donna_tree_store_get_count (priv->store));
                s += 2;
                fmt = s;
                break;

            case 'v':
                g_string_append_len (str, fmt, s - fmt);
                g_string_append_printf (str, "%d",
                        donna_tree_model_get_count ((GtkTreeModel *) priv->store));
                s += 2;
                fmt = s;
                break;

            case 's':
                g_string_append_len (str, fmt, s - fmt);
                if (!sel)
                    sel = gtk_tree_view_get_selection ((GtkTreeView *) sp);
                g_string_append_printf (str, "%d",
                        gtk_tree_selection_count_selected_rows (sel));
                s += 2;
                fmt = s;
                break;

            /* %{...}X is a syntax supported to have between brackets a format
             * for the size, when X represents a size (A/V/S) */
            case '{':
                ss = strchr (s, '}');
                if (!ss)
                {
                    s += 2;
                    continue;
                }

                if (ss[1] == 'A' || ss[1] == 'V' || ss[1] == 'S')
                {
                    g_string_append_len (str, fmt, s - fmt);
                    *ss = '\0';
                    st_render_size (sp, status, str, ss[1], s + 2, &sel);
                    *ss = '}';
                    s = ss + 2;
                }
                else
                    s += 2;

                fmt = s;
                break;

            case 'A':
            case 'V':
            case 'S':
                g_string_append_len (str, fmt, s - fmt);
                st_render_size (sp, status, str, s[1], "%R", &sel);
                s += 2;
                fmt = s;
                break;

            case 'n':
                {
                    GtkTreePath *path;
                    GtkTreeIter iter;

                    g_string_append_len (str, fmt, s - fmt);
                    gtk_tree_view_get_cursor ((GtkTreeView *) sp, &path, NULL);
                    if (path)
                    {
                        if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                                    &iter, path))
                        {
                            DonnaNode *node;

                            gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                                    DONNA_TREE_VIEW_COL_NODE,   &node,
                                    -1);
                            if (node)
                            {
                                ss = donna_node_get_name (node);
                                g_string_append (str, ss);
                                g_free (ss);
                                g_object_unref (node);
                            }
                        }
                        gtk_tree_path_free (path);
                    }
                    s += 2;
                    fmt = s;
                    break;
                }

            case 'N':
                {
                    gint nb;

                    g_string_append_len (str, fmt, s - fmt);
                    if (!sel)
                        sel = gtk_tree_view_get_selection ((GtkTreeView *) sp);
                    nb = gtk_tree_selection_count_selected_rows (sel);
                    if (nb == 1)
                    {
                        GtkTreeIter iter;
                        GList *list;

                        list = gtk_tree_selection_get_selected_rows (sel, NULL);
                        if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                                    &iter, list->data))
                        {
                            DonnaNode *node;

                            gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                                    DONNA_TREE_VIEW_COL_NODE,   &node,
                                    -1);
                            if (node)
                            {
                                ss = donna_node_get_name (node);
                                g_string_append (str, ss);
                                g_free (ss);
                                g_object_unref (node);
                            }
                        }
                        g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
                    }
                    else if (nb > 1)
                        g_string_append_printf (str, "%d items selected", nb);

                    s += 2;
                    fmt = s;
                    break;
                }

            default:
                s += 2;
                break;
        }
    }

    g_string_append (str, fmt);
    if (status->keymode_colors)
    {
        DonnaConfig *config;

        config = donna_app_peek_config (priv->app);
        if (priv->key_mode)
        {
            if (donna_config_get_string (config, &s,
                        "statusbar/%s/keymode_%s_background",
                        status->name, priv->key_mode))
            {
                g_object_set (renderer,
                        "background-set",   TRUE,
                        "background",       s,
                        NULL);
                donna_renderer_set (renderer, "background-set", NULL);
                g_free (s);
            }
            else if (donna_config_get_string (config, &s,
                        "statusbar/%s/keymode_%s_background-rgba",
                        status->name, priv->key_mode))
            {
                GdkRGBA rgba;

                if (gdk_rgba_parse (&rgba, s))
                {
                    g_object_set (renderer,
                            "background-set",   TRUE,
                            "background-rgba",  &rgba,
                            NULL);
                    donna_renderer_set (renderer, "background-set", NULL);
                }
                g_free (s);
            }

            if (donna_config_get_string (config, &s,
                        "statusbar/%s/keymode_%s_foreground",
                        status->name, priv->key_mode))
            {
                g_object_set (renderer,
                        "foreground-set",   TRUE,
                        "foreground",       s,
                        NULL);
                donna_renderer_set (renderer, "foreground-set", NULL);
                g_free (s);
            }
            else if (donna_config_get_string (config, &s,
                        "statusbar/%s/keymode_%s_foreground-rgba",
                        status->name, priv->key_mode))
            {
                GdkRGBA rgba;

                if (gdk_rgba_parse (&rgba, s))
                {
                    g_object_set (renderer,
                            "foreground-set",   TRUE,
                            "foreground-rgba",  &rgba,
                            NULL);
                    donna_renderer_set (renderer, "foreground-set", NULL);
                }
                g_free (s);
            }
        }
    }
    g_object_set (renderer, "visible", TRUE, "text", str->str, NULL);
    g_string_free (str, TRUE);
}

static gboolean
status_provider_set_tooltip (DonnaStatusProvider    *sp,
                             guint                   id,
                             guint                   index,
                             GtkTooltip             *tooltip)
{
    return FALSE;
}

static const gchar *
columntype_get_name (DonnaColumnType    *ct)
{
    return "line-numbers";
}

static const gchar *
columntype_get_renderers (DonnaColumnType    *ct)
{
    return "t";
}

static DonnaColumnTypeNeed
columntype_refresh_data (DonnaColumnType  *ct,
                         const gchar        *tv_name,
                         const gchar        *col_name,
                         const gchar        *arr_name,
                         gpointer           *data)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) ct)->priv;
    DonnaConfig *config;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;

    config = donna_app_peek_config (priv->app);

    if (priv->ln_relative != donna_config_get_boolean_column (config,
                tv_name, col_name, arr_name, "line-number", "relative", FALSE))
    {
        need |= DONNA_COLUMNTYPE_NEED_REDRAW;
        priv->ln_relative = !priv->ln_relative;
    }

    if (priv->ln_relative_focused != donna_config_get_boolean_column (config,
                tv_name, col_name, arr_name, "line-number", "relative_on_focus", TRUE))
    {
        if (priv->ln_relative)
            need |= DONNA_COLUMNTYPE_NEED_REDRAW;
        priv->ln_relative_focused = !priv->ln_relative_focused;
    }

    return need;
}

static void
columntype_free_data (DonnaColumnType    *ct,
                      gpointer            data)
{
    /* void */
}

static GPtrArray *
columntype_get_props (DonnaColumnType    *ct,
                      gpointer            data)
{
    return NULL;
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
    /* because on property update the refesh does only that, i.e. there's no
     * auto-resort */
    g_signal_connect (model, "row-changed", (GCallback) row_changed_cb, tree);
    /* handle "fake removal" (i.e. filtering out) of focused row, to move it
     * elsewhere. If not JJK, we can't really do better than GTK's default
     * set_cursor() (w/out much complications re: selection, etc) */
    g_signal_connect (model, "row-fake-deleted",
            (GCallback) row_fake_deleted_cb, tree);
    if (is_tree (tree))
        /* because we might have to "undo" this -- see has_child_toggled_cb() */
        priv->row_has_child_toggled_sid = g_signal_connect (model,
                "row-has-child-toggled",
                (GCallback) row_has_child_toggled_cb, tree);
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
