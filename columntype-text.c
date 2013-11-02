
#include <glib-object.h>
#include "columntype.h"
#include "columntype-text.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct tv_col_data
{
    gchar            *property;
    DonnaSortOptions  options;
};

struct _DonnaColumnTypeTextPrivate
{
    DonnaApp                    *app;
};

static void             ct_text_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_text_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_text_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_text_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_text_get_renderers       (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_text_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_text_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_text_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_text_can_edit            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_text_edit                (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_text_set_value           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_text_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_text_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_text_is_match_filter     (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static void             ct_text_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_text_set_option       (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     const gchar        *value,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_text_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_text_get_context_item_info (
                                                     DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *item,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     DonnaContextInfo  *info,
                                                     GError           **error);

static void
ct_text_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_text_get_name;
    interface->get_renderers            = ct_text_get_renderers;
    interface->refresh_data             = ct_text_refresh_data;
    interface->free_data                = ct_text_free_data;
    interface->get_props                = ct_text_get_props;
    interface->can_edit                 = ct_text_can_edit;
    interface->edit                     = ct_text_edit;
    interface->set_value                = ct_text_set_value;
    interface->render                   = ct_text_render;
    interface->node_cmp                 = ct_text_node_cmp;
    interface->is_match_filter          = ct_text_is_match_filter;
    interface->free_filter_data         = ct_text_free_filter_data;
    interface->set_option               = ct_text_set_option;
    interface->get_context_alias        = ct_text_get_context_alias;
    interface->get_context_item_info    = ct_text_get_context_item_info;
}

static void
donna_column_type_text_class_init (DonnaColumnTypeTextClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_text_set_property;
    o_class->get_property   = ct_text_get_property;
    o_class->finalize       = ct_text_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTextPrivate));
}

static void
donna_column_type_text_init (DonnaColumnTypeText *ct)
{
    DonnaColumnTypeTextPrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_TEXT,
            DonnaColumnTypeTextPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeText, donna_column_type_text,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_text_columntype_init)
        )

static void
ct_text_finalize (GObject *object)
{
    DonnaColumnTypeTextPrivate *priv;

    priv = DONNA_COLUMNTYPE_TEXT (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_text_parent_class)->finalize (object);
}

static void
ct_text_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_TEXT (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_text_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_TEXT (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_text_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);
    return "text";
}

static const gchar *
ct_text_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);
    return "t";
}

#define check_option(opt_name_lower, opt_name_upper, value, def_val)          \
    if (donna_config_get_boolean_column (config, tv_name, col_name, arr_name, \
                "sort", opt_name_lower, def_val, NULL) == value)              \
    {                                                                         \
        if (!(data->options & opt_name_upper))                                \
        {                                                                     \
            need |= DONNA_COLUMNTYPE_NEED_RESORT;                             \
            data->options |= opt_name_upper;                                  \
        }                                                                     \
    }                                                                         \
    else if (data->options & opt_name_upper)                                  \
    {                                                                         \
        need |= DONNA_COLUMNTYPE_NEED_RESORT;                                 \
        data->options &= ~opt_name_upper;                                     \
    }

static DonnaColumnTypeNeed
ct_text_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      gpointer           *_data)
{
    DonnaColumnTypeText *cttext = DONNA_COLUMNTYPE_TEXT (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;

    config = donna_app_peek_config (cttext->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "property", "name", NULL);
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else
        g_free (s);

    check_option ("natural_order",  DONNA_SORT_NATURAL_ORDER,    TRUE,  TRUE);
    check_option ("dot_first",      DONNA_SORT_DOT_FIRST,        TRUE,  TRUE);
    check_option ("dot_mixed",      DONNA_SORT_DOT_MIXED,        TRUE,  FALSE);
    check_option ("case_sensitive", DONNA_SORT_CASE_INSENSITIVE, FALSE, FALSE);
    check_option ("ignore_spunct",  DONNA_SORT_IGNORE_SPUNCT,    TRUE,  FALSE);

    return need;
}

static void
ct_text_free_data (DonnaColumnType    *ct,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data);
}

static GPtrArray *
ct_text_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_string(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'text': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_STRING));               \
    g_free (location);                                  \
} while (0)

struct editing_data
{
    DonnaColumnTypeText *cttext;
    DonnaTreeView *tree;
    DonnaNode *node;
    struct tv_col_data *data;
    guint editing_started_sid;
    guint editing_done_sid;
};

static gboolean
set_value (const gchar      *property,
           const gchar      *value,
           DonnaNode        *node,
           DonnaTreeView    *tree,
           GError          **error)
{
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    if (!donna_tree_view_set_node_property (tree, node, property, &v, error))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_prefix_error (error, "ColumnType 'text': "
                "Unable to set property '%s' for '%s' to '%s'",
                property, fl, value);
        g_free (fl);
        g_value_unset (&v);
        return FALSE;
    }
    g_value_unset (&v);
    return TRUE;
}

static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *ed)
{
    GError *err = NULL;
    gboolean canceled;

    g_signal_handler_disconnect (editable, ed->editing_done_sid);

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (canceled)
    {
        g_free (ed);
        return;
    }

    if (G_UNLIKELY (!GTK_IS_ENTRY (editable)))
    {
        gchar *fl = donna_node_get_full_location (ed->node);
        donna_app_show_error (ed->cttext->priv->app, NULL,
                "ColumnType 'text': Unable to change property '%s' for '%s': "
                "Editable widget isn't a GtkEntry",
                ed->data->property, fl);
        g_free (fl);
        g_free (ed);
        return;
    }

    if (!set_value (ed->data->property, gtk_entry_get_text ((GtkEntry *) editable),
            ed->node, ed->tree, &err))
    {
        donna_app_show_error (ed->cttext->priv->app, err, NULL);
        g_clear_error (&err);
        g_free (ed);
        return;
    }

    g_free (ed);
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *ed)
{
    g_signal_handler_disconnect (renderer, ed->editing_started_sid);
    ed->editing_started_sid = 0;

    ed->editing_done_sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, ed);
}

static gboolean
ct_text_can_edit (DonnaColumnType    *ct,
                  gpointer            data,
                  DonnaNode          *node,
                  GError            **error)
{
    return DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            ((struct tv_col_data *) data)->property, node, error);
}

static gboolean
ct_text_edit (DonnaColumnType    *ct,
              gpointer            _data,
              DonnaNode          *node,
              GtkCellRenderer   **renderers,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    struct tv_col_data *data = _data;
    struct editing_data *ed;

    if (!ct_text_can_edit (ct, _data, node, error))
        return FALSE;

    ed = g_new0 (struct editing_data, 1);
    ed->cttext  = (DonnaColumnTypeText *) ct;
    ed->tree    = treeview;
    ed->node    = node;
    ed->data    = data,
    ed->editing_started_sid = g_signal_connect (renderers[0], "editing-started",
            (GCallback) editing_started_cb, ed);

    g_object_set (renderers[0], "editable", TRUE, NULL);
    if (!renderer_edit (renderers[0], re_data))
    {
        g_signal_handler_disconnect (renderers[0], ed->editing_started_sid);
        g_free (ed);
        g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType 'text': Failed to put renderer in edit mode");
        return FALSE;
    }
    return TRUE;
}

static gboolean
ct_text_set_value (DonnaColumnType    *ct,
                   gpointer            _data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    struct tv_col_data *data = _data;

    if (nodes->len != 1)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_NOT_SUPPORTED,
                "ColumnType 'text': Can only set value to one node at a time");
        return FALSE;
    }

    if (!ct_text_can_edit (ct, _data, nodes->pdata[0], error))
        return FALSE;

    return set_value (data->property, value, nodes->pdata[0], treeview, error);
}

static GPtrArray *
ct_text_render (DonnaColumnType    *ct,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);

    donna_node_get (node, FALSE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (1, g_free);
        g_ptr_array_add (arr, g_strdup (data->property));
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    else if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
    {
        warn_not_string (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    g_object_set (renderer,
            "visible",  TRUE,
            "text",     g_value_get_string (&value),
            NULL);
    g_value_unset (&value);
    return NULL;
}

static gint
ct_text_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gchar *s1 = NULL;
    gchar *s2 = NULL;
    gint ret;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node1);
        else
            s1 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node2);
        else
            s2 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            ret = -1;
        else
            ret = 0;
        goto done;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
    {
        ret = 1;
        goto done;
    }

    ret = donna_strcmp (s1, s2, data->options);
done:
    g_free (s1);
    g_free (s2);
    return ret;
}

static gboolean
ct_text_is_match_filter (DonnaColumnType    *ct,
                         const gchar        *filter,
                         gpointer           *filter_data,
                         gpointer            _data,
                         DonnaNode          *node,
                         GError            **error)
{
    struct tv_col_data *data = _data;
    GPatternSpec *pspec;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gboolean ret;

    if (G_UNLIKELY (!*filter_data))
        pspec = *filter_data = g_pattern_spec_new (filter);
    else
        pspec = *filter_data;

    donna_node_get (node, TRUE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
        {
            warn_not_string (node);
            g_value_unset (&value);
            return FALSE;
        }
    }
    else
        return FALSE;

    ret = g_pattern_match_string (pspec, g_value_get_string (&value));
    g_value_unset (&value);
    return ret;
}

static void
ct_text_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    g_pattern_spec_free (filter_data);
}

static DonnaColumnTypeNeed
ct_text_set_option (DonnaColumnType    *ct,
                    const gchar        *tv_name,
                    const gchar        *col_name,
                    const gchar        *arr_name,
                    gpointer            _data,
                    const gchar        *option,
                    const gchar        *value,
                    DonnaColumnOptionSaveLocation save_location,
                    GError            **error)
{
    struct tv_col_data *data = _data;
    gboolean c;
    gboolean v;

    if (streq (option, "property"))
    {
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, NULL, save_location,
                    option, G_TYPE_STRING, &data->property, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (data->property);
        data->property = g_strdup (value);
        return DONNA_COLUMNTYPE_NEED_RESORT | DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    if (!streq (value, "0") && !streq (value, "1")
            && !streq (value, "false") && !streq (value, "true"))
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType 'text': Invalid value for option '%s': "
                "Must be '0', 'false', '1' or 'true'",
                option);
        return DONNA_COLUMNTYPE_NEED_NOTHING;
    }
    v = (*value == '1' || streq (value, "true")) ? TRUE : FALSE;

    if (streq (option, "natural_order"))
    {
        c = (data->options & DONNA_SORT_NATURAL_ORDER) ? TRUE : FALSE;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "sort", save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        if (v)
            data->options |= DONNA_SORT_NATURAL_ORDER;
        else
            data->options &= ~DONNA_SORT_NATURAL_ORDER;
        return DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else if (streq (option, "dot_first"))
    {
        c = (data->options & DONNA_SORT_DOT_FIRST) ? TRUE : FALSE;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "sort", save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        if (v)
            data->options |= DONNA_SORT_DOT_FIRST;
        else
            data->options &= ~DONNA_SORT_DOT_FIRST;
        return DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else if (streq (option, "case_sensitive"))
    {
        c = (data->options & DONNA_SORT_CASE_INSENSITIVE) ? FALSE : TRUE;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "sort", save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        if (v)
            data->options &= ~DONNA_SORT_CASE_INSENSITIVE;
        else
            data->options |= DONNA_SORT_CASE_INSENSITIVE;
        return DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else if (streq (option, "dot_mixed"))
    {
        c = (data->options & DONNA_SORT_DOT_MIXED) ? TRUE : FALSE;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "sort", save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        if (v)
            data->options |= DONNA_SORT_DOT_MIXED;
        else
            data->options &= ~DONNA_SORT_DOT_MIXED;
        return DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else if (streq (option, "ignore_spunct"))
    {
        c = (data->options & DONNA_SORT_IGNORE_SPUNCT) ? TRUE : FALSE;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "sort", save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        if (v)
            data->options |= DONNA_SORT_IGNORE_SPUNCT;
        else
            data->options &= ~DONNA_SORT_IGNORE_SPUNCT;
        return DONNA_COLUMNTYPE_NEED_RESORT;
    }

    g_set_error (error, DONNA_COLUMNTYPE_ERROR,
            DONNA_COLUMNTYPE_ERROR_OTHER,
            "ColumnType 'text': Unknown option '%s'",
            option);
    return DONNA_COLUMNTYPE_NEED_NOTHING;
}

static gchar *
ct_text_get_context_alias (DonnaColumnType   *ct,
                           gpointer           _data,
                           const gchar       *alias,
                           const gchar       *extra,
                           DonnaContextReference reference,
                           DonnaNode         *node_ref,
                           get_sel_fn         get_sel,
                           gpointer           get_sel_data,
                           const gchar       *prefix,
                           GError           **error)
{
    struct tv_col_data *data = _data;
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'text': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'text': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "property:@", save_location, ",-,",
            prefix, "natural_order:@", save_location, ",",
            prefix, "dot_first:@", save_location, ",",
            prefix, "case_sensitive:@", save_location, ",",
            prefix, "dot_mixed:@", save_location, ",",
            prefix, "ignore_spunct:@", save_location,
            NULL);
}

static gboolean
ct_text_get_context_item_info (DonnaColumnType   *ct,
                               gpointer           _data,
                               const gchar       *item,
                               const gchar       *extra,
                               DonnaContextReference reference,
                               DonnaNode         *node_ref,
                               get_sel_fn         get_sel,
                               gpointer           get_sel_data,
                               DonnaContextInfo  *info,
                               GError           **error)
{
    struct tv_col_data *data = _data;
    const gchar *value = NULL;
    const gchar *ask_title;
    const gchar *ask_current;
    const gchar *save_location;

    save_location = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Node Property: ", data->property, NULL);
        info->free_name = TRUE;
        ask_title = "Enter the name of the property";
        ask_current = data->property;
    }
    else if (streq (item, "natural_order"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_NATURAL_ORDER) ? TRUE : FALSE;
        info->name = "Natural Order";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "dot_first"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_FIRST) ? TRUE : FALSE;
        info->name = "Show \"dot files\" first";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "case_sensitive"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_CASE_INSENSITIVE) ? FALSE: TRUE;
        info->name = "Case Sensitive";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "dot_mixed"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_MIXED) ? TRUE : FALSE;
        info->name = "Sort \"dot files\" amongst others";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "ignore_spunct"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_IGNORE_SPUNCT) ? TRUE : FALSE;
        info->name = "Ignore leading spunctuation characters";
        value = (info->is_active) ? "0" : "1";
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'text': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, FALSE,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
