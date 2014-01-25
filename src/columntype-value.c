
#include "config.h"

#include <glib-object.h>
#include "columntype.h"
#include "columntype-value.h"
#include "app.h"
#include "node.h"
#include "sort.h"
#include "macros.h"

/**
 * SECTION:columntype-value
 * @Short_description: To show the type or value of an option.
 *
 * Column type to show the type or value of an option (from configuration). Note
 * that while firstly intended for the configuration, it can actually show the
 * value of any property on the node.
 *
 * <refsect2 id="ct-value-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>show_type</systemitem> (boolean) : Whether to show the type or
 *   value of the option; Defaults to false
 * - <systemitem>property_value</systemitem> (string) : Name of the property
 *   holding the value; Defaults to "option-value"
 * - <systemitem>property_extra</systemitem> (string) : Name of the property
 *   holding the name of the extra; Defaults to "option-extra"
 *
 * </para></refsect2>
 *
 * <refsect2 id="ct-value-filtering">
 * <title>Filtering</title>
 * <para>
 * There are no filtering possible on column of this type.
 * </para></refsect2>
 */

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


struct tv_col_data
{
    gchar *prop_value;
    gchar *prop_extra;
    gboolean show_type;
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
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_value_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_value_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_value_can_edit           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
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
static gboolean         ct_value_set_tooltip        (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_value_node_cmp           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static DonnaColumnTypeNeed ct_value_set_option      (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     const gchar        *value,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_value_get_context_alias  (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_value_get_context_item_info (
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
ct_value_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_value_get_name;
    interface->get_renderers            = ct_value_get_renderers;
    interface->refresh_data             = ct_value_refresh_data;
    interface->free_data                = ct_value_free_data;
    interface->get_props                = ct_value_get_props;
    interface->can_edit                 = ct_value_can_edit;
    interface->edit                     = ct_value_edit;
    interface->render                   = ct_value_render;
    interface->set_tooltip              = ct_value_set_tooltip;
    interface->node_cmp                 = ct_value_node_cmp;
    interface->set_option               = ct_value_set_option;
    interface->get_context_alias        = ct_value_get_context_alias;
    interface->get_context_item_info    = ct_value_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeValue, donna_column_type_value,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_value_column_type_init)
        )

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
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_VALUE,
            DonnaColumnTypeValuePrivate);
}

static void
ct_value_finalize (GObject *object)
{
    DonnaColumnTypeValuePrivate *priv;

    priv = DONNA_COLUMN_TYPE_VALUE (object)->priv;
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
        DONNA_COLUMN_TYPE_VALUE (object)->priv->app = g_value_dup_object (value);
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
        g_value_set_object (value, DONNA_COLUMN_TYPE_VALUE (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_value_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_VALUE (ct), NULL);
    return "value";
}

static const gchar *
ct_value_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_VALUE (ct), NULL);
    return "tc";
}

static DonnaColumnTypeNeed
ct_value_refresh_data (DonnaColumnType    *ct,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       const gchar        *tv_name,
                       gboolean            is_tree,
                       gpointer           *_data)
{
    DonnaColumnTypeValue *ctv = DONNA_COLUMN_TYPE_VALUE (ct);
    DonnaConfig *config;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    struct tv_col_data *data;
    gchar *s;

    config = donna_app_peek_config (ctv->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    if (data->show_type != donna_config_get_boolean_column (config, col_name,
                arr_name, tv_name, is_tree, "column_types/value",
                "show_type", FALSE, NULL))
    {
        need |= DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
        data->show_type = !data->show_type;
    }

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/value",
            "property_value", "option-value", NULL);
    if (!streq (s, data->prop_value))
    {
        g_free (data->prop_value);
        data->prop_value = s;

        need |= DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/value",
            "property_extra", "option-extra", NULL);
    if (!streq (s, data->prop_extra))
    {
        g_free (data->prop_extra);
        data->prop_extra = s;

        need |= DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else
        g_free (s);

    return need;
}

static void
ct_value_free_data (DonnaColumnType    *ct,
                    gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->prop_value);
    g_free (data->prop_extra);
    g_free (data);
}

static GPtrArray *
ct_value_get_props (DonnaColumnType  *ct,
                    gpointer          _data)
{
    struct tv_col_data *data = _data;
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_VALUE (ct), NULL);

    props = g_ptr_array_new_full (2, g_free);
    g_ptr_array_add (props, g_strdup (data->prop_value));
    g_ptr_array_add (props, g_strdup (data->prop_extra));

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
    struct tv_col_data      *data;
    /* most stuff -- combo (menu) or text (entry) */
    GtkCellRenderer         *rnd_combo;
    guint                    key_limit;
    gulong                   editing_started_sid;
    gulong                   editing_done_sid;
    gulong                   changed_sid;
    gulong                   key_press_event_sid;
    /* custom window for flags */
    GtkWidget               *window;
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
        gtk_tree_model_get (model, iter, 1, &s, -1);
        g_value_take_string (&value, s);
    }

    if (!donna_tree_view_set_node_property (ed->tree, ed->node,
                ed->data->prop_value, &value, &err))
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
            g_value_set_int (&value, (gint) g_ascii_strtoll (
                        gtk_entry_get_text ((GtkEntry *) editable),
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
                ed->data->prop_value, &value, &err))
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

    if (event->keyval == GDK_KEY_Return
            || event->keyval == GDK_KEY_KP_Enter
            || event->keyval == GDK_KEY_Escape
            || event->keyval == GDK_KEY_Up
            || event->keyval == GDK_KEY_Down
            || event->keyval == GDK_KEY_Left
            || event->keyval == GDK_KEY_Right
            || event->keyval == GDK_KEY_Delete
            || event->keyval == GDK_KEY_BackSpace)
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

static void
apply_cb (GtkButton *button, struct editing_data *ed)
{
    GError *err = NULL;
    GtkWidget *w;
    GList *list, *l;
    gint val = 0;
    GValue value = G_VALUE_INIT;

    gtk_widget_hide (ed->window);

    /* get the box */
    w = gtk_bin_get_child ((GtkBin *) ed->window);
    /* get children */
    list = gtk_container_get_children ((GtkContainer *) w);

    for (l = list; l; l = l->next)
    {
        w = l->data;

        if (GTK_IS_TOGGLE_BUTTON (w))
        {
            gboolean active;

            g_object_get (w, "active", &active, NULL);
            if (active)
                val |= GPOINTER_TO_INT (g_object_get_data ((GObject *) w, "flag-value"));
        }
    }

    g_value_init (&value, G_TYPE_INT);
    g_value_set_int (&value, val);
    if (!donna_tree_view_set_node_property (ed->tree, ed->node,
                ed->data->prop_value, &value, &err))
    {
        gchar *fl = donna_node_get_full_location (ed->node);
        donna_app_show_error (ed->ctv->priv->app, err,
                "ColumnType 'value': Unable to set value of '%s'",
                fl);
        g_free (fl);
        g_clear_error (&err);
    }
    g_value_unset (&value);

    g_list_free (list);
    gtk_widget_destroy (ed->window);
}

static gboolean
ct_value_can_edit (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node,
                   GError            **error)
{
    struct tv_col_data *data = _data;

    if (data->show_type)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
                "ColumnType 'value': Cannot change the type of an option");
        return FALSE;
    }

    return DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            data->prop_value, node, error);
}

static gboolean
ct_value_edit (DonnaColumnType    *ct,
               gpointer            _data,
               DonnaNode          *node,
               GtkCellRenderer   **renderers,
               renderer_edit_fn    renderer_edit,
               gpointer            re_data,
               DonnaTreeView      *treeview,
               GError            **error)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    struct tv_col_data *data = _data;
    struct editing_data *ed;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GValue extra = G_VALUE_INIT;
    GType type;
    gint i;
    gint rnd;

    if (!ct_value_can_edit (ct, _data, node, error))
        return FALSE;

    donna_node_get (node, TRUE, data->prop_value, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType 'value': Failed to get property for '%s'", fl);
        g_free (fl);
        return FALSE;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (&value);

    donna_node_get (node, FALSE, data->prop_extra, &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR
            || has == DONNA_NODE_VALUE_NEED_REFRESH)
        /* really, extra should always be set (and will in config) if it exists,
         * hence why we just treat NEED_REFRESH that way */
        has = DONNA_NODE_VALUE_NONE;

    if (has == DONNA_NODE_VALUE_SET)
    {
        GtkListStore *store;
        const DonnaConfigExtra *_e;

        /* extra, so we show a list of possible values via RND_COMBO */

        _e = donna_config_get_extra (donna_app_peek_config (priv->app),
                g_value_get_string (&extra), error);
        if (!_e)
        {
            gchar *fl = donna_node_get_full_location (node);
            g_prefix_error (error,
                    "ColumnType 'value': Failed to get labels for value of '%s': ",
                    fl);
            g_free (fl);
            return FALSE;
        }

        if (type == G_TYPE_STRING && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
        {
            DonnaConfigExtraList *e = (DonnaConfigExtraList *) _e;

            store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
            for (i = 0; i < e->nb_items; ++i)
                gtk_list_store_insert_with_values (store, NULL, -1,
                        0,  (e->items[i].label) ? e->items[i].label : e->items[i].value,
                        1,  e->items[i].value,
                        -1);
        }
        else if (type == G_TYPE_INT && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
        {
            DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) _e;

            store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
            for (i = 0; i < e->nb_items; ++i)
            {
                gtk_list_store_insert_with_values (store, NULL, -1,
                        0,  (e->items[i].label) ? e->items[i].label : e->items[i].in_file,
                        1,  e->items[i].value,
                        -1);
            }
        }
        else if (type == G_TYPE_INT && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
        {
            DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) _e;
            GtkWindow *win;
            GtkBox *box;
            GtkWidget *w;
            gint cur = g_value_get_int (&value);

            ed = g_new0 (struct editing_data, 1);
            ed->ctv  = (DonnaColumnTypeValue *) ct;
            ed->tree = treeview;
            ed->node = node;
            ed->data = data;

            win = donna_column_type_new_floating_window (treeview, FALSE);
            ed->window = w = (GtkWidget *) win;
            g_signal_connect_swapped (win, "destroy", (GCallback) g_free, ed);

            box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
            gtk_container_add ((GtkContainer *) win, (GtkWidget *) box);

            for (i = 0; i < e->nb_items; ++i)
            {
                w = gtk_check_button_new_with_label (
                        (e->items[i].label) ? e->items[i].label : e->items[i].in_file);
                g_object_set_data ((GObject *) w, "flag-value",
                        GINT_TO_POINTER (e->items[i].value));
                g_object_set (w, "active", !!(cur & e->items[i].value), NULL);
                gtk_box_pack_start (box, w, 0, 0, FALSE);
            }

            w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            g_object_set (w, "margin-top", 10, NULL);
            gtk_box_pack_start (box, w, 0, 0, FALSE);
            box = (GtkBox *) w;

            w = gtk_button_new_with_label ("Apply");
            gtk_button_set_image ((GtkButton *) w,
                    gtk_image_new_from_icon_name ("gtk-apply", GTK_ICON_SIZE_MENU));
            g_signal_connect (w, "clicked", (GCallback) apply_cb, ed);
            gtk_box_pack_end (box, w, FALSE, FALSE, 3);

            w = gtk_button_new_with_label ("Cancel");
            gtk_button_set_image ((GtkButton *) w,
                    gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
            g_signal_connect_swapped (w, "clicked",
                    (GCallback) gtk_widget_destroy, win);
            gtk_box_pack_end (box, w, FALSE, FALSE, 3);

            gtk_widget_show_all (ed->window);
            donna_app_set_floating_window (ed->ctv->priv->app, win);
            return TRUE;
        }
        else
        {
            gchar *fl = donna_node_get_full_location (node);
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                    "ColumnType 'value': Failed to get matching extras for '%s'", fl);
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
                data->prop_value, &v, error);
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
    ed->data      = data;
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
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType 'value': Failed to put renderer in edit mode");
        return FALSE;
    }
    return TRUE;
}

static GPtrArray *
get_text_for_type (DonnaColumnType      *ct,
                   struct tv_col_data   *data,
                   guint                 index,
                   DonnaNode            *node,
                   GValue               *value,
                   const gchar         **text,
                   gchar               **free)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaNodeHasValue has;
    GValue extra = G_VALUE_INIT;
    GType type;

    if (index == RND_COMBO)
        return NULL;

    donna_node_get (node, FALSE, data->prop_value, &has, value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
        return NULL;
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (2, g_free);
        g_ptr_array_add (arr, g_strdup (data->prop_value));
        g_ptr_array_add (arr, g_strdup (data->prop_extra));
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (value);

    donna_node_get (node, FALSE, data->prop_extra, &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        const DonnaConfigExtra *_e;

        _e = donna_config_get_extra (donna_app_peek_config (priv->app),
                g_value_get_string (&extra), NULL);
        if (_e)
        {
            if (_e->any.title)
                *text = _e->any.title;
            else
                *text = *free = g_value_dup_string (&extra);
        }
        else
            *text = "<unknown extra>";
        g_value_unset (&extra);
    }
    else if (type == G_TYPE_BOOLEAN)
        *text = "Boolean";
    else if (type == G_TYPE_INT)
        *text = "Integer";
    else if (type == G_TYPE_STRING)
        *text = "String";
    else /* G_TYPE_DOUBLE */
        *text = "Double";

    return NULL;
}

static GPtrArray *
get_text_for_value (DonnaColumnType     *ct,
                    struct tv_col_data  *data,
                    guint                index,
                    DonnaNode           *node,
                    GValue              *value,
                    const gchar        **text,
                    gchar              **free)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaNodeHasValue has;
    GValue extra = G_VALUE_INIT;
    GType type;

    donna_node_get (node, FALSE, data->prop_value, &has, value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
        return NULL;
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (2, g_free);
        g_ptr_array_add (arr, g_strdup (data->prop_value));
        g_ptr_array_add (arr, g_strdup (data->prop_extra));
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    type = G_VALUE_TYPE (value);

    donna_node_get (node, FALSE, data->prop_extra, &has, &extra, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR
            || has == DONNA_NODE_VALUE_NEED_REFRESH)
        /* really, extra will always be set (in config at least) if it exists,
         * hence why we just treat NEED_REFRESH that way */
        has = DONNA_NODE_VALUE_NONE;

    if (type == G_TYPE_STRING)
    {
        if (has == DONNA_NODE_VALUE_NONE && index == RND_TEXT)
            *text = g_value_get_string (value);
        else if (has == DONNA_NODE_VALUE_SET && index == RND_COMBO)
        {
            const DonnaConfigExtra *_e;

            *text = g_value_get_string (value);
            _e = donna_config_get_extra (donna_app_peek_config (priv->app),
                        g_value_get_string (&extra), NULL);
            if (_e && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
            {
                DonnaConfigExtraList *e = (DonnaConfigExtraList *) _e;
                gint i;

                for (i = 0; i < e->nb_items; ++i)
                    if (streq (e->items[i].value, *text))
                    {
                        if (e->items[i].label)
                            *text = e->items[i].label;
                        break;
                    }
            }
        }
    }
    else if (type == G_TYPE_INT)
    {
        if (has == DONNA_NODE_VALUE_NONE && index == RND_TEXT)
            *text = *free = g_strdup_printf ("%d", g_value_get_int (value));
        else if (has == DONNA_NODE_VALUE_SET && index == RND_COMBO)
        {
            const DonnaConfigExtra *_e;
            gint id;
            gint i;

            *text = "<failed to get label>";
            _e = donna_config_get_extra (donna_app_peek_config (priv->app),
                        g_value_get_string (&extra), NULL);
            if (_e && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
            {
                DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) _e;

                id = g_value_get_int (value);
                for (i = 0; i < e->nb_items; ++i)
                    if (e->items[i].value == id)
                    {
                        if (e->items[i].label)
                            *text = e->items[i].label;
                        else
                            *text = e->items[i].in_file;
                        break;
                    }
            }
            else if (_e && _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
            {
                DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) _e;
                GString *str;

                id = g_value_get_int (value);
                str = g_string_sized_new (23 /* random */);
                for (i = 0; i < e->nb_items; ++i)
                    if (id & e->items[i].value)
                        g_string_append_printf (str, "%s, ", (e->items[i].label)
                                ? e->items[i].label : e->items[i].in_file);
                if (G_LIKELY (str->len > 0))
                {
                    /* remove trailing comma & space */
                    g_string_truncate (str, str->len - 2);
                    *text = *free = g_string_free (str, FALSE);
                }
                else
                {
                    g_string_free (str, TRUE);
                    *text = "(nothing)";
                }
            }
        }
    }
    else if (index == RND_TEXT)
    {
        if (type == G_TYPE_DOUBLE)
            *text = *free = g_strdup_printf ("%f", g_value_get_double (value));
        if (type == G_TYPE_BOOLEAN)
            *text = (g_value_get_boolean (value)) ? "TRUE" : "FALSE";
        else
            *text = *free = g_strdup_printf ("<unsupported option type:%s>",
                    g_type_name (type));
    }

    if (has == DONNA_NODE_VALUE_SET)
        g_value_unset (&extra);
    return NULL;
}

static GPtrArray *
ct_value_render (DonnaColumnType    *ct,
                 gpointer            _data,
                 guint               index,
                 DonnaNode          *node,
                 GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    GValue value = G_VALUE_INIT;
    const gchar *text = NULL;
    gchar *free = NULL;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_VALUE (ct), NULL);

    if (data->show_type)
        arr = get_text_for_type (ct, data, index, node, &value, &text, &free);
    else
        arr = get_text_for_value (ct, data, index, node, &value, &text, &free);

    if (text)
        g_object_set (renderer,
                "text",         text,
                "ellipsize",    PANGO_ELLIPSIZE_END,
                "ellipsize-set",TRUE,
                "visible",      TRUE,
                NULL);
    else
        g_object_set (renderer, "visible", FALSE, NULL);

    g_free (free);
    if (G_IS_VALUE (&value))
        g_value_unset (&value);
    return arr;
}

static gboolean
ct_value_set_tooltip (DonnaColumnType    *ct,
                      gpointer            _data,
                      guint               index,
                      DonnaNode          *node,
                      GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    GValue value = G_VALUE_INIT;
    const gchar *text = NULL;
    gchar *free = NULL;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_VALUE (ct), NULL);

    if (data->show_type)
        arr = get_text_for_type (ct, data, index, node, &value, &text, &free);
    else
        arr = get_text_for_value (ct, data, index, node, &value, &text, &free);

    if (arr)
        g_ptr_array_unref (arr);

    if (text)
        gtk_tooltip_set_text (tooltip, text);

    g_free (free);
    g_value_unset (&value);
    return text != NULL;
}

union val
{
    const gchar *s;
    gint         i;
    gdouble      d;
};

static GType
load_val (struct tv_col_data *data,
          DonnaConfig        *config,
          DonnaNode          *node,
          GValue             *value,
          union val          *val)
{
    DonnaNodeHasValue has;
    GType type;

    donna_node_get (node, TRUE, data->prop_value, &has, value, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        GValue extra = G_VALUE_INIT;

        type = G_VALUE_TYPE (value);
        donna_node_get (node, TRUE, data->prop_extra, &has, &extra, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            const DonnaConfigExtra *_e;
            gint i;

            if (type == G_TYPE_STRING)
                val->s = g_value_get_string (value);
            else
                val->s = "<failed to get label>";
            type = G_TYPE_STRING;

            _e = donna_config_get_extra (config, g_value_get_string (&extra), NULL);
            if (_e)
            {
                if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                {
                    DonnaConfigExtraList *e = (DonnaConfigExtraList *) _e;

                    for (i = 0; i < e->nb_items; ++i)
                        if (streq (e->items[i].value, val->s))
                        {
                            if (e->items[i].label)
                                val->s = e->items[i].label;
                            break;
                        }
                }
                else if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
                {
                    DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) _e;
                    gint id;

                    id = g_value_get_int (value);
                    for (i = 0; i < e->nb_items; ++i)
                        if (e->items[i].value == id)
                        {
                            if (e->items[i].label)
                                val->s = e->items[i].label;
                            else
                                val->s = e->items[i].in_file;
                            break;
                        }
                }
                else if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
                {
                    DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) _e;
                    gint id;

                    id = g_value_get_int (value);
                    for (i = 0; i < e->nb_items; ++i)
                        if (id & e->items[i].value)
                        {
                            if (e->items[i].label)
                                val->s = e->items[i].label;
                            else
                                val->s = e->items[i].in_file;
                            break;
                        }
                }
                else
                    g_warning ("ColumnType 'value': Unknown extra type %d",
                            _e->any.type);
            }
            g_value_unset (&extra);
        }
        else if (type == G_TYPE_BOOLEAN)
            val->i = g_value_get_boolean (value);
        else if (type == G_TYPE_INT)
            val->i = g_value_get_int (value);
        else if (type == G_TYPE_DOUBLE)
            val->d = g_value_get_double (value);
        else if (type == G_TYPE_STRING)
            val->s = g_value_get_string (value);
    }
    else
        type = G_TYPE_INVALID;

    return type;
}

#define free_and_return(r)  { ret = r; goto done; }
static gint
ct_value_node_cmp (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node1,
                   DonnaNode          *node2)
{
    DonnaColumnTypeValuePrivate *priv = ((DonnaColumnTypeValue *) ct)->priv;
    DonnaConfig *config = donna_app_peek_config (priv->app);
    struct tv_col_data *data = _data;
    GType type1;
    GType type2;
    GValue value1 = G_VALUE_INIT;
    GValue value2 = G_VALUE_INIT;
    union val val1;
    union val val2;
    gint ret;

    if (* (gboolean *) data)
    {
        DonnaNodeHasValue has1;
        DonnaNodeHasValue has2;
        const gchar *t1;
        const gchar *t2;

        donna_node_get (node1, TRUE, data->prop_extra, &has1, &value1, NULL);
        if (has1 == DONNA_NODE_VALUE_SET)
        {
            const DonnaConfigExtra *_e;

            _e = donna_config_get_extra (config, g_value_get_string (&value1), NULL);
            t1 = (_e) ? ((_e->any.title) ? _e->any.title
                    : g_value_get_string (&value1)) : "<unknown extra>";
        }
        else
        {
            donna_node_get (node1, TRUE, data->prop_value, &has1, &value1, NULL);
            if (has1 == DONNA_NODE_VALUE_SET)
            {
                type1 = G_VALUE_TYPE (&value1);
                if (type1 == G_TYPE_BOOLEAN)
                    t1 = "Boolean";
                else if (type1 == G_TYPE_INT)
                    t1 = "Integer";
                else if (type1 == G_TYPE_STRING)
                    t1 = "String";
                else /* G_TYPE_DOUBLE */
                    t1 = "Double";
            }
            else
                t1 = "<unknown>";
        }

        donna_node_get (node2, TRUE, data->prop_extra, &has2, &value2, NULL);
        if (has2 == DONNA_NODE_VALUE_SET)
        {
            const DonnaConfigExtra *_e;

            _e = donna_config_get_extra (config, g_value_get_string (&value2), NULL);
            t2 = (_e) ? ((_e->any.title) ? _e->any.title
                    : g_value_get_string (&value2)) : "<unknown extra>";
        }
        else
        {
            donna_node_get (node2, TRUE, data->prop_value, &has2, &value2, NULL);
            if (has2 == DONNA_NODE_VALUE_SET)
            {
                type2 = G_VALUE_TYPE (&value2);
                if (type2 == G_TYPE_BOOLEAN)
                    t2 = "Boolean";
                else if (type2 == G_TYPE_INT)
                    t2 = "Integer";
                else if (type2 == G_TYPE_STRING)
                    t2 = "String";
                else /* G_TYPE_DOUBLE */
                    t2 = "Double";
            }
            else
                t2 = "<unknown>";
        }

        ret = donna_strcmp (t1, t2, DONNA_SORT_CASE_INSENSITIVE);
        if (has1 == DONNA_NODE_VALUE_SET)
            g_value_unset (&value1);
        if (has2 == DONNA_NODE_VALUE_SET)
            g_value_unset (&value2);
        return ret;
    }

    type1 = load_val (data, config, node1, &value1, &val1);
    type2 = load_val (data, config, node2, &value2, &val2);

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
        free_and_return ((gint) (val1.d - val2.d))

done:
    if (type1 != G_TYPE_INVALID)
        g_value_unset (&value1);
    if (type2 != G_TYPE_INVALID)
        g_value_unset (&value2);
    return (ret > 0) ? 1 : ((ret < 0) ? -1 : 0);
}

static DonnaColumnTypeNeed
ct_value_set_option (DonnaColumnType    *ct,
                     const gchar        *col_name,
                     const gchar        *arr_name,
                     const gchar        *tv_name,
                     gboolean            is_tree,
                     gpointer            _data,
                     const gchar        *option,
                     const gchar        *value,
                     DonnaColumnOptionSaveLocation save_location,
                     GError            **error)
{
    struct tv_col_data *data = _data;

    if (streq (option, "property_value"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/value",
                    save_location,
                    option, G_TYPE_STRING, &data->prop_value, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->prop_value);
        data->prop_value = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_extra"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/value",
                    save_location,
                    option, G_TYPE_STRING, &data->prop_extra, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->prop_extra);
        data->prop_extra = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "show_type"))
    {
        gboolean c, v;

        if (!streq (value, "0") && !streq (value, "1")
                && !streq (value, "false") && !streq (value, "true"))
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_OTHER,
                    "ColumnType 'value': Invalid value for option '%s': "
                    "Must be '0', 'false', '1' or 'true'",
                    option);
            return DONNA_COLUMN_TYPE_NEED_NOTHING;
        }

        c = data->show_type;
        v = (*value == '1' || streq (value, "true")) ? TRUE : FALSE;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/value",
                    save_location,
                    option, G_TYPE_BOOLEAN, &c, &v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        data->show_type = v;
        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'value': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_value_get_context_alias (DonnaColumnType   *ct,
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
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'value': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'value': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "property_value:@", save_location, ",",
            prefix, "property_extra:@", save_location, ",",
            prefix, "show_type:@", save_location,
            NULL);
}

static gboolean
ct_value_get_context_item_info (DonnaColumnType   *ct,
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
    const gchar *ask_title = NULL;
    const gchar *ask_current = NULL;
    const gchar *save_location = NULL;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property_value"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Property Value: ", data->prop_value, NULL);
        info->free_name = TRUE;
        value = NULL;
        ask_title = "Enter the name of the property holding the value";
        ask_current = data->prop_value;
    }
    else if (streq (item, "property_extra"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Property Extra: ", data->prop_extra, NULL);
        info->free_name = TRUE;
        value = NULL;
        ask_title = "Enter the name of the property holding the extra name";
        ask_current = data->prop_extra;
    }
    else if (streq (item, "show_type"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = "Show the type (instead of the value)";
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = data->show_type;
        value = (info->is_active) ? "0" : "1";
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'value': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, FALSE,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
