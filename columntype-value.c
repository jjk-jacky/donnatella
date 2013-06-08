
#include <glib-object.h>
#include "columntype.h"
#include "columntype-value.h"
#include "app.h"
#include "node.h"
#include "sort.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum
{
    RND_TEXT = 1,
    RND_COMBO
};

struct _DonnaColumnTypeValuePrivate
{
    DonnaApp    *app;
};

static void             ct_value_set_property       (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_value_get_property       (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_value_finalize           (GObject            *object);

/* ColumnType */
static const gchar *    ct_value_get_name           (DonnaColumnType    *ct);
static const gchar *    ct_value_get_renderers      (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_value_refresh_data    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_value_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_value_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_value_edit               (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_value_render             (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_value_node_cmp           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_value_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name         = ct_value_get_name;
    interface->get_renderers    = ct_value_get_renderers;
    interface->refresh_data     = ct_value_refresh_data;
    interface->free_data        = ct_value_free_data;
    interface->get_props        = ct_value_get_props;
    interface->edit             = ct_value_edit;
    interface->render           = ct_value_render;
    interface->node_cmp         = ct_value_node_cmp;
}

static void
donna_column_type_value_class_init (DonnaColumnTypeValueClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_value_set_property;
    o_class->get_property   = ct_value_get_property;
    o_class->finalize       = ct_value_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeValuePrivate));
}

static void
donna_column_type_value_init (DonnaColumnTypeValue *ct)
{
    DonnaColumnTypeValuePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_VALUE,
            DonnaColumnTypeValuePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeValue, donna_column_type_value,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_value_columntype_init)
        )

static void
ct_value_finalize (GObject *object)
{
    DonnaColumnTypeValuePrivate *priv;

    priv = DONNA_COLUMNTYPE_VALUE (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_value_parent_class)->finalize (object);
}

static void
ct_value_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_VALUE (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_value_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_VALUE (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_value_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_VALUE (ct), NULL);
    return "value";
}

static const gchar *
ct_value_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_VALUE (ct), NULL);
    return "tc";
}

static DonnaColumnTypeNeed
ct_value_refresh_data (DonnaColumnType    *ct,
                       const gchar        *tv_name,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       gpointer           *_data)
{
    DonnaColumnTypeValue *ctv = DONNA_COLUMNTYPE_VALUE (ct);
    DonnaConfig *config;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gboolean *data;

    config = donna_app_peek_config (ctv->priv->app);

    if (!*_data)
        *_data = g_new0 (gboolean, 1);
    data = * (gboolean **) _data;

    if (*data != donna_config_get_boolean_column (config,
                tv_name, col_name, arr_name, NULL, "show_type", FALSE))
    {
        need |= DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;
        *data = !*data;
    }

    return need;
}

static void
ct_value_free_data (DonnaColumnType    *ct,
                    gpointer            data)
{
    g_free (data);
}

static GPtrArray *
ct_value_get_props (DonnaColumnType  *ct,
                    gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_VALUE (ct), NULL);

    props = g_ptr_array_new_full (2, g_free);
    g_ptr_array_add (props, g_strdup ("option-value"));
    g_ptr_array_add (props, g_strdup ("option-extra"));

    return props;
}

enum
{
    KEY_LIMIT_NONE,
    KEY_LIMIT_INT,
    KEY_LIMIT_DOUBLE
};

struct editing_data
{
    DonnaColumnTypeValue    *ctv;
    DonnaTreeView           *tree;
    DonnaNode               *node;
    GtkCellRenderer         *rnd_combo;
    guint                    key_limit;
    gulong                   editing_started_sid;
    gulong                   editing_done_sid;
    gulong                   changed_sid;
    gulong                   key_press_event_sid;
};

static void
changed_cb (GtkCellRendererCombo    *renderer,
            gchar                   *path_string,
            GtkTreeIter             *iter,
            struct editing_data     *ed)
{
    GError *err = NULL;
    GtkTreeModel *model;
    GValue value = G_VALUE_INIT;

    g_object_get (renderer, "model", &model, NULL);

    if (ed->key_limit == KEY_LIMIT_INT)
    {
        gint id;

        g_value_init (&value, G_TYPE_INT);
        gtk_tree_model_get (model, iter, 1, &id, -1);
        g_value_set_int (&value, id);
    }
    else
    {
        gchar *s;

        g_value_init (&value, G_TYPE_STRING);
        gtk_tree_model_get (model, iter, 0, &s, -1);
        g_value_take_string (&value, s);
    }

    if (!donna_tree_view_set_node_property (ed->tree, ed->node,
                "option-value", &value, &err))
    {
        gchar *fl = donna_node_get_full_location (ed->node);
        donna_app_show_error (ed->ctv->priv->app, err,
                "ColumnType 'value': Unable to change value of '%s'",
                fl);
        g_free (fl);
        g_clear_error (&err);
        g_value_unset (&value);
        g_free (ed);
        return;
    }
    g_value_unset (&value);
}

static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *ed)
{
    gboolean canceled;

    g_signal_handler_disconnect (editable, ed->editing_done_sid);
    if (ed->rnd_combo)
    {
        if (ed->changed_sid)
            g_signal_handler_disconnect (ed->rnd_combo, ed->changed_sid);
        g_free (ed);
        return;
    }

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (!canceled)
    {
        GError *err = NULL;
        GValue value = G_VALUE_INIT;

        if (G_UNLIKELY (!GTK_IS_ENTRY (editable)))
        {
            gchar *fl = donna_node_get_full_location (ed->node);
            donna_app_show_error (ed->ctv->priv->app, NULL,
                    "ColumnType 'value': Unable to change property 'name' for '%s': "
                    "Editable widget isn't a GtkEntry", fl);
            g_free (fl);
            g_free (ed);
            return;
        }

        if (ed->key_limit == KEY_LIMIT_INT)
        {
            g_value_init (&value, G_TYPE_INT);
            g_value_set_int (&value,
                    g_ascii_strtoll (gtk_entry_get_text ((GtkEntry *) editable),
                        NULL, 10));
        }
        else if (ed->key_limit == KEY_LIMIT_DOUBLE)
        {
            g_value_init (&value, G_TYPE_DOUBLE);
            g_value_set_double (&value,
                    g_ascii_strtod (gtk_entry_get_text ((GtkEntry *) editable),
                        NULL));
        }
        else /* KEY_LIMIT_NONE */
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, gtk_entry_get_text ((GtkEntry *) editable));
        }

        if (!donna_tree_view_set_node_property (ed->tree, ed->node,
                "option-value", &value, &err))
        {
            gchar *fl = donna_node_get_full_location (ed->node);
            donna_app_show_error (ed->ctv->priv->app, err,
                    "ColumnType 'value': Unable to set value of '%s'",
                    fl);
            g_free (fl);
            g_clear_error (&err);
            g_value_unset (&value);
            g_free (ed);
            return;
        }
        g_value_unset (&value);
    }

    g_free (ed);
}

static gboolean
key_press_event_cb (GtkWidget *w, GdkEventKey *event, struct editing_data *ed)
{
    gunichar c = gdk_keyval_to_unicode (event->keyval);

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter
            || event->keyval == GDK_KEY_Escape)
        return FALSE;
    else if (g_unichar_isdigit (c))
        return FALSE;
    else if (ed->key_limit == KEY_LIMIT_INT)
        return TRUE;
    else
        return c != '.' && c != ',';
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *ed)
{
    g_signal_handler_disconnect (renderer, ed->editing_started_sid);
    ed->editing_started_sid = 0;

    if (ed->rnd_combo)
        ed->changed_sid = g_signal_connect (renderer, "changed",
                (GCallback) changed_cb, ed);
    ed->editing_done_sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, ed);
    if (ed->key_limit != KEY_LIMIT_NONE)
        ed->key_press_event_sid = g_signal_connect (editable, "key-press-event",
                (GCallback) key_press_event_cb, ed);
}

static gboolean
ct_value_edit (DonnaColumnType    *ct,
               gpointer            data,
               DonnaNode          *node,
               GtkCellRenderer   **renderers,
               renderer_edit_fn    renderer_edit,
               gpointer            re_data,
               DonnaTreeView      *treeview,
               GError            **error)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    struct editing_data *ed;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GValue extra = G_VALUE_INIT;
    GType type;
    gint rnd;

    if (* (gboolean *) data)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType 'value': Cannot change the type of an option");
        return FALSE;
    }

    donna_node_get (node, TRUE, "option-value", &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType 'value': Failed to get property for '%s'");
        g_free (fl);
        return FALSE;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (&value);

    donna_node_get (node, FALSE, "option-extra", &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR
            || has == DONNA_NODE_VALUE_NEED_REFRESH)
        /* really, extra will always be set (in config at least) if it exists,
         * hence why we just treat NEED_REFRESH that way */
        has = DONNA_NODE_VALUE_NONE;

    if (has == DONNA_NODE_VALUE_SET)
    {
        GtkListStore *store;
        const DonnaConfigExtra *extras;

        /* extra, so we show a list of possible values via RND_COMBO */

        extras = donna_config_get_extras (donna_app_peek_config (priv->app),
                g_value_get_string (&extra), error);
        if (!extras)
        {
            gchar *fl = donna_node_get_full_location (node);
            g_prefix_error (error,
                    "ColumnType 'value': Failed to get labels for value of '%s'",
                    fl);
            g_free (fl);
            return FALSE;
        }

        if (type == G_TYPE_STRING && extras->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
        {
            DonnaConfigExtraList **extra;

            store = gtk_list_store_new (1, G_TYPE_STRING);
            for (extra = (DonnaConfigExtraList **) extras->values; *extra; ++extra)
                gtk_list_store_insert_with_values (store, NULL, -1,
                        0,  *extra,
                        -1);
        }
        else if (type == G_TYPE_INT && extras->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
        {
            DonnaConfigExtraListInt **extra;

            store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
            for (extra = (DonnaConfigExtraListInt **) extras->values; *extra; ++extra)
            {
                gtk_list_store_insert_with_values (store, NULL, -1,
                        0,  (*extra)->desc,
                        1,  (*extra)->value,
                        -1);
            }
        }
        else
        {
            gchar *fl = donna_node_get_full_location (node);
            g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                    "ColumnType 'value': Failed to get matching extras for '%s'");
            g_free (fl);
            return FALSE;
        }

        g_object_set (renderers[RND_COMBO - 1],
                "has-entry",    FALSE,
                "model",        store,
                "text-column",  0,
                NULL);
        rnd = RND_COMBO - 1;
    }
    else if (type == G_TYPE_BOOLEAN)
    {
        GValue v = G_VALUE_INIT;
        gboolean ret;

        /* just switch value */

        g_value_init (&v, G_TYPE_BOOLEAN);
        g_value_set_boolean (&v, !g_value_get_boolean (&value));
        ret = donna_tree_view_set_node_property (treeview, node,
                "option-value", &v, error);
        g_value_unset (&v);
        return ret;
    }
    else
    {
        /* go inline editing mode */
        rnd = RND_TEXT - 1;
    }
    g_value_unset (&value);
    if (has == DONNA_NODE_VALUE_SET)
        g_value_unset (&extra);

    ed = g_new0 (struct editing_data, 1);
    ed->ctv       = (DonnaColumnTypeValue *) ct;
    ed->tree      = treeview;
    ed->node      = node;
    ed->rnd_combo = (has == DONNA_NODE_VALUE_SET) ? renderers[rnd] : NULL;
    ed->key_limit = (type == G_TYPE_DOUBLE) ? KEY_LIMIT_DOUBLE
        : ((type == G_TYPE_INT) ? KEY_LIMIT_INT : KEY_LIMIT_NONE);
    ed->editing_started_sid = g_signal_connect (renderers[rnd],
            "editing-started",
            (GCallback) editing_started_cb, ed);

    g_object_set (renderers[rnd], "editable", TRUE, NULL);
    if (!renderer_edit (renderers[rnd], re_data))
    {
        g_signal_handler_disconnect (renderers[rnd], ed->editing_started_sid);
        g_free (ed);
        g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType 'value': Failed to put renderer in edit mode");
        return FALSE;
    }
    return TRUE;
}

static GPtrArray *
render_type (DonnaColumnType    *ct,
             guint               index,
             DonnaNode          *node,
             GtkCellRenderer    *renderer)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GValue extra = G_VALUE_INIT;
    GType type;
    const gchar *lbl;
    gchar *s = NULL;

    if (index == RND_COMBO)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    donna_node_get (node, FALSE, "option-value", &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (2, g_free);
        g_ptr_array_add (arr, g_strdup ("option-value"));
        g_ptr_array_add (arr, g_strdup ("option-extra"));
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (&value);

    donna_node_get (node, FALSE, "option-extra", &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        const DonnaConfigExtra *extras;

        extras = donna_config_get_extras (donna_app_peek_config (priv->app),
                g_value_get_string (&extra), NULL);
        lbl = s = g_strdup_printf ("%s (%s)",
                (type == G_TYPE_INT) ? "Integer" : "String",
                (extras) ? g_value_get_string (&extra) : "<unknown extra>");
    }
    else if (type == G_TYPE_BOOLEAN)
        lbl = "Boolean";
    else if (type == G_TYPE_INT)
        lbl = "Integer";
    else if (type == G_TYPE_STRING)
        lbl = "String";
    else /* G_TYPE_DOUBLE */
        lbl = "Double";

    g_object_set (renderer,
            "text",     lbl,
            "visible",  TRUE,
            NULL);
    g_free (s);
    return NULL;
}

static GPtrArray *
render_value (DonnaColumnType    *ct,
              guint               index,
              DonnaNode          *node,
              GtkCellRenderer    *renderer)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GValue extra = G_VALUE_INIT;
    GType type;
    gchar *s;

    donna_node_get (node, FALSE, "option-value", &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (2, g_free);
        g_ptr_array_add (arr, g_strdup ("option-value"));
        g_ptr_array_add (arr, g_strdup ("option-extra"));
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (&value);

    donna_node_get (node, FALSE, "option-extra", &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR
            || has == DONNA_NODE_VALUE_NEED_REFRESH)
        /* really, extra will always be set (in config at least) if it exists,
         * hence why we just treat NEED_REFRESH that way */
        has = DONNA_NODE_VALUE_NONE;

    if (type == G_TYPE_STRING)
    {
        if ((has == DONNA_NODE_VALUE_NONE && index == RND_TEXT)
                || (has == DONNA_NODE_VALUE_SET && index == RND_COMBO))
            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     g_value_get_string (&value),
                    NULL);
        else
            g_object_set (renderer, "visible", FALSE, NULL);
    }
    else if (type == G_TYPE_INT)
    {
        if (has == DONNA_NODE_VALUE_NONE && index == RND_TEXT)
        {
            s = g_strdup_printf ("%d", g_value_get_int (&value));
            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     s,
                    NULL);
            g_free (s);
        }
        else if (has == DONNA_NODE_VALUE_SET && index == RND_COMBO)
        {
            const DonnaConfigExtra *extras;
            const gchar *label;
            gint id;

            s = NULL;
            label = "<failed to get label>";
            extras = donna_config_get_extras (donna_app_peek_config (priv->app),
                        g_value_get_string (&extra), NULL);
            if (extras && extras->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
            {
                DonnaConfigExtraListInt **extra;

                id = g_value_get_int (&value);
                for (extra = (DonnaConfigExtraListInt **) extras->values; *extra; ++extra)
                    if ((*extra)->value == id)
                    {
                        label = s = g_strdup_printf ("%d (%s)",
                                id, (*extra)->desc);
                        break;
                    }
            }

            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     label,
                    NULL);
            g_free (s);
        }
        else
            g_object_set (renderer, "visible", FALSE, NULL);
    }
    else if (type == G_TYPE_DOUBLE)
    {
        if (index == RND_TEXT)
        {
            s = g_strdup_printf ("%f", g_value_get_double (&value));
            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     s,
                    NULL);
            g_free (s);
        }
        else
            g_object_set (renderer, "visible", FALSE, NULL);
    }
    else if (type == G_TYPE_BOOLEAN)
    {
        if (index == RND_TEXT)
            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     (g_value_get_boolean (&value)) ? "TRUE" : "FALSE",
                    NULL);
        else
            g_object_set (renderer, "visible", FALSE, NULL);
    }
    else
    {
        if (index == RND_TEXT)
        {
            s = g_strdup_printf ("<unsupported option type:%s>",
                    g_type_name (type));
            g_object_set (renderer,
                    "visible",  TRUE,
                    "text",     s,
                    NULL);
            g_free (s);
        }
        else
            g_object_set (renderer, "visible", FALSE, NULL);
    }

    g_value_unset (&value);
    if (has == DONNA_NODE_VALUE_SET)
        g_value_unset (&extra);
    return NULL;
}

static GPtrArray *
ct_value_render (DonnaColumnType    *ct,
                 gpointer            data,
                 guint               index,
                 DonnaNode          *node,
                 GtkCellRenderer    *renderer)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_VALUE (ct), NULL);

    if (* (gboolean *) data)
        return render_type (ct, index, node, renderer);
    else
        return render_value (ct, index, node, renderer);
}

union val
{
    const gchar *s;
    gint         i;
    gdouble      d;
};

static GType
load_val (DonnaConfig *config, DonnaNode *node, GValue *value, union val *val)
{
    DonnaNodeHasValue has;
    GType type;

    donna_node_get (node, TRUE, "option-value", &has, value, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        GValue extra = G_VALUE_INIT;

        type = G_VALUE_TYPE (value);
        donna_node_get (node, TRUE, "option-extra", &has, &extra, NULL);
        if (has == DONNA_NODE_VALUE_SET)
            if (type != G_TYPE_STRING)
                type = G_TYPE_INVALID;

        if (type == G_TYPE_BOOLEAN)
            val->i = g_value_get_boolean (value);
        else if (type == G_TYPE_INT)
            val->i = g_value_get_int (value);
        else if (type == G_TYPE_DOUBLE)
            val->d = g_value_get_double (value);
        else if (type == G_TYPE_STRING)
            val->s = g_value_get_string (value);
        else /* G_TYPE_INVALID == DONNA_CONFIG_EXTRA_TYPE_LIST_INT */
        {
            const DonnaConfigExtra *extras;

            type = G_TYPE_STRING;
            val->s = "<failed to get label>";

            extras = donna_config_get_extras (config,
                    g_value_get_string (&extra), NULL);
            if (extras && extras->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
            {
                DonnaConfigExtraListInt **extra;
                gint id;

                id = g_value_get_int (value);
                for (extra = (DonnaConfigExtraListInt **) extras->values; *extra; ++extra)
                    if ((*extra)->value == id)
                    {
                        val->s = (*extra)->desc;
                        break;
                    }
            }
        }

        if (has == DONNA_NODE_VALUE_SET)
            g_value_unset (&extra);
    }
    else
        type = G_TYPE_INVALID;

    return type;
}

#define free_and_return(r)  { ret = r; goto done; }
static gint
ct_value_node_cmp (DonnaColumnType    *ct,
                   gpointer            data,
                   DonnaNode          *node1,
                   DonnaNode          *node2)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaConfig *config = donna_app_peek_config (priv->app);
    GType type1;
    GType type2;
    GValue value1 = G_VALUE_INIT;
    GValue value2 = G_VALUE_INIT;
    union val val1;
    union val val2;
    gint ret;

    if (* (gboolean *) data)
    {
        DonnaNodeHasValue has;

        donna_node_get (node1, TRUE, "option-value", &has, &value1, NULL);
        if (has != DONNA_NODE_VALUE_SET)
            type1 = G_TYPE_INVALID;
        else
            type1 = G_VALUE_TYPE (&value1);
        g_value_unset (&value1);

        donna_node_get (node2, TRUE, "option-value", &has, &value2, NULL);
        if (has != DONNA_NODE_VALUE_SET)
            type2 = G_TYPE_INVALID;
        else
            type2 = G_VALUE_TYPE (&value2);
        g_value_unset (&value2);

        if (type1 == type2)
            return 0;

        if (type1 == G_TYPE_INVALID)
            return 1;
        else if (type2 == G_TYPE_INVALID)
            return -1;

        if (type1 == G_TYPE_BOOLEAN)
            return -1;
        else if (type2 == G_TYPE_BOOLEAN)
            return 1;

        if (type1 == G_TYPE_STRING)
            return 1;
        else if (type2 == G_TYPE_STRING)
            return -1;

        return 0;
    }

    type1 = load_val (config, node1, &value1, &val1);
    type2 = load_val (config, node2, &value2, &val2);

    if (type1 != type2)
    {
        if (type1 == G_TYPE_INVALID)
            free_and_return (1)
        else if (type2 == G_TYPE_INVALID)
            free_and_return (-1)

        if (type1 == G_TYPE_BOOLEAN)
            free_and_return (-1)
        else if (type2 == G_TYPE_BOOLEAN)
            free_and_return (1)

        if (type1 == G_TYPE_STRING)
            free_and_return (1)
        else if (type2 == G_TYPE_STRING)
            free_and_return (-1)

        gdouble v1, v2;

        if (type1 == G_TYPE_INT)
            v1 = (gdouble) val1.d;
        else /* G_TYPE_DOUBLE */
            v1 = val1.d;

        if (type2 == G_TYPE_INT)
            v2 = (gdouble) val2.d;
        else /* G_TYPE_DOUBLE */
            v2 = val2.d;

        free_and_return ((v1 > v2) ? 1 : ((v1 < v2) ? -1 : 0))
    }

    /* type1 == type2 */

    if (type1 == G_TYPE_BOOLEAN || type1 == G_TYPE_INT)
        free_and_return (val1.i - val2.i)
    else if (type1 == G_TYPE_STRING)
        /* We could offer the sort options as options to the column, but that's
         * complications for something probably no one would ever use... */
        free_and_return (donna_strcmp (val1.s, val2.s, DONNA_SORT_NATURAL_ORDER
                    | DONNA_SORT_CASE_INSENSITIVE))
    else /* G_TYPE_DOUBLE */
        free_and_return (val1.d - val2.d)

done:
    if (type1 != G_TYPE_INVALID)
        g_value_unset (&value1);
    if (type2 != G_TYPE_INVALID)
        g_value_unset (&value2);
    return (ret > 0) ? 1 : ((ret < 0) ? -1 : 0);
}
