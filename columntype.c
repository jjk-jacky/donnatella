
#include <string.h>
#include "columntype.h"
#include "app.h"
#include "conf.h"
#include "macros.h"

static GtkSortType
default_get_default_sort_order (DonnaColumnType    *ct,
                                const gchar        *tv_name,
                                const gchar        *col_name,
                                const gchar        *arr_name,
                                gpointer            data)
{
    DonnaColumnTypeInterface *interface;
    DonnaApp *app;
    const gchar *type;
    gchar buf[55], *b = buf;
    GtkSortType order;

    g_object_get (ct, "app", &app, NULL);
    type = donna_columntype_get_name (ct);
    /* 42 == 55 - strlen ("columntypes/") - 1 */
    if (G_UNLIKELY (strnlen (type, 42) >= 42))
        b = g_strconcat ("columntypes/", type, NULL);
    else
        strcpy (stpcpy (buf, "columntypes/"), type);

    order = (donna_config_get_boolean_column (donna_app_peek_config (app),
                tv_name, col_name, arr_name, b, "desc_first", FALSE, NULL))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    if (G_UNLIKELY (b != buf))
        g_free (b);
    g_object_unref (app);
    return order;
}

static gboolean
default_can_edit (DonnaColumnType    *ct,
                  gpointer            data,
                  DonnaNode          *node,
                  GError            **error)
{
    g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_columntype_get_name (ct));
    return FALSE;
}

static gboolean
default_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderer,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_columntype_get_name (ct));
    return FALSE;
}

static gboolean
default_set_value (DonnaColumnType    *ct,
                   gpointer            data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_columntype_get_name (ct));
    return FALSE;
}

static gboolean
default_set_tooltip (DonnaColumnType    *ct,
                     gpointer            data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    return FALSE;
}

static gboolean
helper_can_edit (DonnaColumnType    *ct,
                 const gchar        *property,
                 DonnaNode          *node,
                 GError            **error)
{
    DonnaNodeHasProp has_prop;

    has_prop = donna_node_has_property (node, property);

    if (!(has_prop & DONNA_NODE_PROP_EXISTS))
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_NODE_NO_PROP,
                "ColumnType '%s': property '%s' doesn't exist",
                donna_columntype_get_name (ct), property);
        return FALSE;
    }

    if (!(has_prop & DONNA_NODE_PROP_WRITABLE))
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_NODE_NOT_WRITABLE,
                "ColumnType '%s': property '%s' isn't writable",
                donna_columntype_get_name (ct), property);
        return FALSE;
    }

    return TRUE;
}

static const gchar *
helper_get_save_location (DonnaColumnType    *ct,
                          const gchar       **extra,
                          gboolean            from_alias,
                          GError            **error)
{
    const gchar *save = "";
    g_assert (extra);

    if (*extra && (from_alias || **extra == '@'))
    {
        gchar *s;
        gsize len;

        if (!from_alias)
            ++*extra;
        s = strchr (*extra, ':');
        if (s)
            len = s - *extra;
        else
            len = strlen (*extra);

        if (len == 0)
            save = "";
        else if (streqn (*extra, "memory", len))
            save = "memory";
        else if (streqn (*extra, "current", len))
            save = "current";
        else if (streqn (*extra, "ask", len))
            save = "ask";
        else if (streqn (*extra, "arr", len))
            save = "arr";
        else if (streqn (*extra, "tree", len))
            save = "tree";
        else if (streqn (*extra, "col", len))
            save = "col";
        else if (streqn (*extra, "default", len))
            save = "default";
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType '%s': Invalid save location from extra: '%s''",
                    donna_columntype_get_name (ct), *extra);
            return NULL;
        }

        if (!s || s[1] == '\0')
            *extra = NULL;
        else
            *extra = s + 1;
    }

    return save;
}

struct asl
{
    GtkWidget *win;
    GSList *list;
    DonnaColumnOptionSaveLocation save_location;
    DonnaTreeView *tree;
    gulong sid;
};

static void
btn_clicked (struct asl *asl)
{
    for ( ; asl->list; asl->list = asl->list->next)
    {
        if (gtk_toggle_button_get_active ((GtkToggleButton *) asl->list->data))
        {
            gpointer sl;

            sl = g_object_get_data (asl->list->data, "_from");
            asl->save_location = GPOINTER_TO_UINT (sl);
            gtk_widget_destroy (asl->win);
            return;
        }
    }
    g_warn_if_reached ();
}

static void
tree_changed_location (struct asl *asl)
{
    g_signal_handler_disconnect (asl->tree, asl->sid);
    asl->sid = 0;
    gtk_widget_destroy (asl->win);
}

static DonnaColumnOptionSaveLocation
ask_save_location (DonnaApp     *app,
                   const gchar  *tv_name,
                   const gchar  *col_name,
                   const gchar  *arr_name,
                   const gchar  *def_cat,
                   const gchar  *option,
                   guint         from,
                   GError      **error)
{
    struct asl asl;
    GMainLoop *loop;
    GtkStyleContext *context;
    GtkWidget *win;
    GtkWidget *hbox;
    GtkWidget *btn;
    GtkWidget *w;
    GtkGrid *grid;
    GtkRadioButton *btn_grp;
    GtkWidget *btn_box;
    gchar *s;
    gint row;

    win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name (win, "columnoption-save-location");
    donna_app_add_window (app, (GtkWindow *) win, TRUE);
    gtk_window_set_default_size ((GtkWindow *) win, 420, -1);
    gtk_window_set_decorated ((GtkWindow *) win, FALSE);
    gtk_window_set_has_resize_grip ((GtkWindow *) win, FALSE);
    gtk_container_set_border_width ((GtkContainer *) win, 4);

    hbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) win, hbox);

    w = gtk_label_new ("Where do you want to save the new value ?");
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "title");
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);

    s = g_strdup_printf ("Column options can be saved in different locations, "
            "each with a different reach. Select where the new value for "
            "option '%s' of column '%s' will be saved.",
            option, col_name);
    w = gtk_label_new (s);
    g_object_set (w, "wrap", TRUE, NULL);
    g_free (s);
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "details");
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);

    w = gtk_grid_new ();
    grid = (GtkGrid *) w;
    gtk_grid_set_row_homogeneous (grid, TRUE);
    gtk_grid_set_column_homogeneous (grid, TRUE);
    gtk_grid_set_column_spacing (grid, 6);
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);
    row = 0;

    btn = gtk_radio_button_new (NULL);
    btn_grp = (GtkRadioButton *) btn;
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "In <b>current arrangement</b>");
    gtk_container_add ((GtkContainer *) btn, w);
    if (!arr_name)
    {
        gtk_widget_set_sensitive (btn, FALSE);
        gtk_grid_attach (grid, btn, 0, row, 2, 1);
    }
    else
    {
        gtk_grid_attach (grid, btn, 0, row, 1, 1);

        w = gtk_label_new (NULL);
        s = g_strdup_printf ("(<i>%s/columns_options/%s</i>)", arr_name, col_name);
        gtk_label_set_markup ((GtkLabel *) w, s);
        g_free (s);
        gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
        if (from == _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT)
            g_object_set (btn, "active", TRUE, NULL);
        gtk_grid_attach (grid, w, 1, row, 1, 1);
    }
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT));
    ++row;

    btn = gtk_radio_button_new_from_widget (btn_grp);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "As a <b>treeview</b> option");
    gtk_container_add ((GtkContainer *) btn, w);
    gtk_grid_attach (grid, btn, 0, row, 1, 1);
    w = gtk_label_new (NULL);
    s = g_strdup_printf ("(<i>treeviews/%s/columns/%s</i>)", tv_name, col_name);
    gtk_label_set_markup ((GtkLabel *) w, s);
    g_free (s);
    gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
    if (from == _DONNA_CONFIG_COLUMN_FROM_TREE)
        g_object_set (btn, "active", TRUE, NULL);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_TREE));
    ++row;

    btn = gtk_radio_button_new_from_widget (btn_grp);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "As a <b>column</b> option");
    gtk_container_add ((GtkContainer *) btn, w);
    gtk_grid_attach (grid, btn, 0, row, 1, 1);
    w = gtk_label_new (NULL);
    s = g_strdup_printf ("(<i>columns/%s</i>)", col_name);
    gtk_label_set_markup ((GtkLabel *) w, s);
    g_free (s);
    gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
    if (from == _DONNA_CONFIG_COLUMN_FROM_COLUMN)
        g_object_set (btn, "active", TRUE, NULL);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_COLUMN));
    ++row;

    btn = gtk_radio_button_new_from_widget (btn_grp);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "As a new <b>default</b>");
    gtk_container_add ((GtkContainer *) btn, w);
    if (!def_cat)
    {
        gtk_widget_set_sensitive (btn, FALSE);
        gtk_grid_attach (grid, btn, 0, row, 2, 1);
    }
    else
    {
        gtk_grid_attach (grid, btn, 0, row, 1, 1);

        w = gtk_label_new (NULL);
        s = g_strdup_printf ("(<i>defaults/%s</i>)", def_cat);
        gtk_label_set_markup ((GtkLabel *) w, s);
        g_free (s);
        gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
        if (from == _DONNA_CONFIG_COLUMN_FROM_DEFAULT)
            g_object_set (btn, "active", TRUE, NULL);
        gtk_grid_attach (grid, w, 1, row, 1, 1);
    }
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT));

    asl.win = win;
    asl.list = gtk_radio_button_get_group ((GtkRadioButton *) btn);
    asl.save_location = (guint) -1;

    btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_end ((GtkBox *) hbox, btn_box, FALSE, FALSE, 4);

    w = gtk_button_new_with_label ("Save option");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) btn_clicked, &asl);
    gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);

    w = gtk_button_new_with_label ("Cancel");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) gtk_widget_destroy, win);
    gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);


    /* if the tree changes location, we need to abort. Because we start a main
     * loop, the tree could change location, which could mean our arr_name and
     * col_name will point to random memory location */
    asl.tree = donna_app_get_treeview (app, tv_name);
    asl.sid = g_signal_connect_swapped (asl.tree, "notify::location",
            /* we don't connect gtk_widget_destroy in the off chance the tree
             * would notify of multiple change of locations */
            (GCallback) tree_changed_location, &asl);

    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (win, "destroy", (GCallback) g_main_loop_quit, loop);

    gtk_widget_show_all (win);
    g_main_loop_run (loop);

    if (asl.sid > 0)
        g_signal_handler_disconnect (asl.tree, asl.sid);
    g_object_unref (asl.tree);

    return asl.save_location;
}

#define _cfg_set(type, value, location, ...)                                \
    if (!donna_config_set_##type (config, value, __VA_ARGS__))              \
    {                                                                       \
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,                         \
                DONNA_COLUMNTYPE_ERROR_OTHER,                               \
                "ColumnType '%s': Failed to save option '%s' in %s",        \
                donna_columntype_get_name (ct), option, location);          \
        g_object_unref (app);                                               \
        return FALSE;                                                       \
    }
#define cfg_set(location, ...)                                              \
    if (type == G_TYPE_STRING)                                              \
    {                                                                       \
        _cfg_set (string, * (gchar **) value, location, __VA_ARGS__);       \
    }                                                                       \
    else if (type == G_TYPE_BOOLEAN)                                        \
    {                                                                       \
        _cfg_set (boolean, * (gboolean *) value, location, __VA_ARGS__);    \
    }                                                                       \
    else if (type == G_TYPE_INT)                                            \
    {                                                                       \
        _cfg_set (int, * (gint *) value, location, __VA_ARGS__);            \
    }                                                                       \
    else /* G_TYPE_DOUBLE */                                                \
    {                                                                       \
        _cfg_set (double, * (gdouble *) value, location, __VA_ARGS__);      \
    }
static gboolean
helper_set_option (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   const gchar        *arr_name,
                   const gchar        *def_cat,
                   DonnaColumnOptionSaveLocation save_location,
                   const gchar        *option,
                   GType               type,
                   gpointer            current,
                   gpointer            value,
                   GError            **error)
{
    DonnaApp *app;
    DonnaConfig *config;

    g_return_val_if_fail (type == G_TYPE_STRING || type == G_TYPE_BOOLEAN
            || type == G_TYPE_INT || type == G_TYPE_DOUBLE, FALSE);

    g_object_get (ct, "app", &app, NULL);
    config = donna_app_peek_config (app);

    if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_CURRENT
            || save_location == DONNA_COLUMN_OPTION_SAVE_IN_ASK)
    {
        guint from;

        if (type == G_TYPE_STRING)
        {
            gchar *s;

            s = donna_config_get_string_column (config,
                    tv_name, col_name, arr_name, def_cat, option, NULL, &from);
            if (!streq (* (gchar ** ) current, s))
            {
                g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                        DONNA_COLUMNTYPE_ERROR_OTHER,
                        "ColumnType '%s': Cannot save option '%s' in current location: "
                        "Values not matching: '%s' (config) vs '%s' (memory)",
                        donna_columntype_get_name (ct), option,
                        s, * (gchar **) current);
                g_object_unref (app);
                g_free (s);
                return FALSE;
            }
            g_free (s);
        }
        else if (type == G_TYPE_BOOLEAN)
        {
            gboolean b;

            b = donna_config_get_boolean_column (config,
                    tv_name, col_name, arr_name, def_cat, option, FALSE, &from);
            if (b != * (gboolean *) current)
            {
                g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                        DONNA_COLUMNTYPE_ERROR_OTHER,
                        "ColumnType '%s': Cannot save option '%s' in current location: "
                        "Values not matching: '%s' (config) vs '%s' (memory)",
                        donna_columntype_get_name (ct), option,
                        (b) ? "true" : "false",
                        (* (gboolean *) current) ? "true" : "false");
                g_object_unref (app);
                return FALSE;
            }
        }
        else if (type == G_TYPE_INT)
        {
            gint i;

            i = donna_config_get_int_column (config,
                    tv_name, col_name, arr_name, def_cat, option, 0, &from);
            if (i != * (gint *) current)
            {
                g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                        DONNA_COLUMNTYPE_ERROR_OTHER,
                        "ColumnType '%s': Cannot save option '%s' in current location: "
                        "Values not matching: '%d' (config) vs '%d' (memory)",
                        donna_columntype_get_name (ct), option,
                        i, * (gint *) current);
                g_object_unref (app);
                return FALSE;
            }
        }
        else /* G_TYPE_DOUBLE */
        {
            gdouble d;

            d = donna_config_get_double_column (config,
                    tv_name, col_name, arr_name, def_cat, option, 0.0, &from);
            if (d != * (gdouble *) current)
            {
                g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                        DONNA_COLUMNTYPE_ERROR_OTHER,
                        "ColumnType '%s': Cannot save option '%s' in current location: "
                        "Values not matching: '%f' (config) vs '%f' (memory)",
                        donna_columntype_get_name (ct), option,
                        d, * (gdouble *) current);
                g_object_unref (app);
                return FALSE;
            }
        }

        if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_ASK)
        {
            save_location = ask_save_location (app, tv_name, col_name, arr_name,
                    def_cat, option, from, error);
            if (save_location == (guint) -1)
            {
                g_object_unref (app);
                return FALSE;
            }
        }
        else
        {
            switch (from)
            {
                case _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT:
                    save_location = DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_TREE:
                    save_location = DONNA_COLUMN_OPTION_SAVE_IN_TREE;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_COLUMN:
                    save_location = DONNA_COLUMN_OPTION_SAVE_IN_COLUMN;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_DEFAULT:
                    save_location = DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT;
                    break;
            }
        }
    }

    if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT && !arr_name)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType '%s': Cannot save option '%s' in arrangement: "
                "No current arrangement available",
                donna_columntype_get_name (ct), option);
        g_object_unref (app);
        return FALSE;
    }
    else if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT && !def_cat)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType '%s': Cannot save option '%s' in defaults: "
                "No default location for option",
                donna_columntype_get_name (ct), option);
        g_object_unref (app);
        return FALSE;
    }

    if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT)
    {
        cfg_set ("arrangement", "%s/columns_options/%s/%s",
                arr_name, col_name, option);
    }
    else if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_TREE)
    {
        cfg_set ("treeview", "treeviews/%s/columns/%s/%s",
                tv_name, col_name, option);
    }
    else if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_COLUMN)
    {
        cfg_set ("column", "columns/%s/%s", col_name, option);
    }
    else if (save_location == DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT)
    {
        cfg_set ("defaults", "defaults/%s/%s", def_cat, option);
    }

    g_object_unref (app);
    return TRUE;
}
#undef cfg_set
#undef _cfg_set

static void
donna_columntype_default_init (DonnaColumnTypeInterface *interface)
{
    interface->helper_can_edit          = helper_can_edit;
    interface->helper_get_save_location = helper_get_save_location;
    interface->helper_set_option        = helper_set_option;

    interface->get_default_sort_order   = default_get_default_sort_order;
    interface->can_edit                 = default_can_edit;
    interface->edit                     = default_edit;
    interface->set_value                = default_set_value;
    interface->set_tooltip              = default_set_tooltip;

    g_object_interface_install_property (interface,
            g_param_spec_object ("app", "app", "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

G_DEFINE_INTERFACE (DonnaColumnType, donna_columntype, G_TYPE_OBJECT)

const gchar *
donna_columntype_get_name (DonnaColumnType *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_name != NULL, NULL);

    return (*interface->get_name) (ct);
}

const gchar *
donna_columntype_get_renderers (DonnaColumnType  *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_renderers != NULL, NULL);

    return (*interface->get_renderers) (ct);
}

DonnaColumnTypeNeed
donna_columntype_refresh_data (DonnaColumnType  *ct,
                               const gchar        *tv_name,
                               const gchar        *col_name,
                               const gchar        *arr_name,
                               gpointer           *data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (interface->refresh_data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    return (*interface->refresh_data) (ct, tv_name, col_name, arr_name, data);
}

void
donna_columntype_free_data (DonnaColumnType    *ct,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_if_fail (DONNA_IS_COLUMNTYPE (ct));

    if (G_UNLIKELY (!data))
        return;

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->free_data != NULL);

    return (*interface->free_data) (ct, data);
}

GPtrArray *
donna_columntype_get_props (DonnaColumnType    *ct,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_props != NULL, NULL);

    return (*interface->get_props) (ct, data);
}

GtkSortType
donna_columntype_get_default_sort_order (DonnaColumnType    *ct,
                                         const gchar        *tv_name,
                                         const gchar        *col_name,
                                         const gchar        *arr_name,
                                         gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), GTK_SORT_ASCENDING);
    g_return_val_if_fail (tv_name != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (col_name != NULL, GTK_SORT_ASCENDING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (interface->get_default_sort_order != NULL, GTK_SORT_ASCENDING);

    return (*interface->get_default_sort_order) (ct, tv_name, col_name, arr_name, data);
}

gboolean
donna_columntype_can_edit (DonnaColumnType    *ct,
                           gpointer            data,
                           DonnaNode          *node,
                           GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->can_edit != NULL, FALSE);

    return (*interface->can_edit) (ct, data, node, error);
}

gboolean
donna_columntype_edit (DonnaColumnType    *ct,
                       gpointer            data,
                       DonnaNode          *node,
                       GtkCellRenderer   **renderers,
                       renderer_edit_fn    renderer_edit,
                       gpointer            re_data,
                       DonnaTreeView      *treeview,
                       GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (renderers != NULL && GTK_IS_CELL_RENDERER (renderers[0]), FALSE);
    g_return_val_if_fail (renderer_edit != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->edit != NULL, FALSE);

    return (*interface->edit) (ct, data, node, renderers,
            renderer_edit, re_data, treeview, error);
}

DonnaColumnTypeNeed
donna_columntype_set_option (DonnaColumnType    *ct,
                             const gchar        *tv_name,
                             const gchar        *col_name,
                             const gchar        *arr_name,
                             gpointer            data,
                             const gchar        *option,
                             const gchar        *value,
                             DonnaColumnOptionSaveLocation save_location,
                             GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (tv_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (option != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (value != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (interface->set_option != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    return (*interface->set_option) (ct, tv_name, col_name, arr_name, data,
            option, value, save_location, error);
}

gboolean
donna_columntype_set_value (DonnaColumnType    *ct,
                            gpointer            data,
                            GPtrArray          *nodes,
                            const gchar        *value,
                            DonnaNode          *node_ref,
                            DonnaTreeView      *treeview,
                            GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (!node_ref || DONNA_IS_NODE (node_ref), FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_value != NULL, FALSE);

    return (*interface->set_value) (ct, data, nodes, value, node_ref, treeview, error);
}

GPtrArray *
donna_columntype_render (DonnaColumnType    *ct,
                         gpointer            data,
                         guint               index,
                         DonnaNode          *node,
                         GtkCellRenderer    *renderer)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (GTK_IS_CELL_RENDERER (renderer), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->render != NULL, NULL);

    return (*interface->render) (ct, data, index, node, renderer);
}

gboolean
donna_columntype_set_tooltip (DonnaColumnType    *ct,
                              gpointer            data,
                              guint               index,
                              DonnaNode          *node,
                              GtkTooltip         *tooltip)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (GTK_IS_TOOLTIP (tooltip), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_tooltip != NULL, FALSE);

    return (*interface->set_tooltip) (ct, data, index, node, tooltip);
}

gint
donna_columntype_node_cmp (DonnaColumnType    *ct,
                           gpointer            data,
                           DonnaNode          *node1,
                           DonnaNode          *node2)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node1), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node2), 0);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->node_cmp != NULL, 0);

    return (*interface->node_cmp) (ct, data, node1, node2);
}

gboolean
donna_columntype_is_match_filter (DonnaColumnType    *ct,
                                  const gchar        *filter,
                                  gpointer           *filter_data,
                                  gpointer            data,
                                  DonnaNode          *node,
                                  GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);
    g_return_val_if_fail (filter != NULL, 0);
    g_return_val_if_fail (filter_data != NULL, 0);
    g_return_val_if_fail (DONNA_IS_NODE (node), 0);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    if (!interface->is_match_filter)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType '%s': no filtering supported",
                donna_columntype_get_name (ct));
        return FALSE;
    }

    return (*interface->is_match_filter) (ct, filter, filter_data, data,
            node, error);
}

void
donna_columntype_free_filter_data (DonnaColumnType   *ct,
                                   gpointer           filter_data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);

    if (G_UNLIKELY (!filter_data))
        return;

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->free_filter_data != NULL, 0);

    return (*interface->free_filter_data) (ct, filter_data);
}

gchar *
donna_columntype_get_context_alias (DonnaColumnType    *ct,
                                    gpointer            data,
                                    const gchar        *alias,
                                    const gchar        *extra,
                                    DonnaContextReference reference,
                                    DonnaNode          *node_ref,
                                    get_sel_fn          get_sel,
                                    gpointer            get_sel_data,
                                    const gchar        *prefix,
                                    GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (alias != NULL, NULL);
    g_return_val_if_fail (prefix != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);
    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->get_context_alias == NULL)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType '%s': No context alias supported",
                donna_columntype_get_name (ct));
        return NULL;
    }

    return (*interface->get_context_alias) (ct, data,
            alias, extra, reference, node_ref, get_sel, get_sel_data,
            prefix, error);
}

gboolean
donna_columntype_get_context_item_info (DonnaColumnType    *ct,
                                        gpointer            data,
                                        const gchar        *item,
                                        const gchar        *extra,
                                        DonnaContextReference reference,
                                        DonnaNode          *node_ref,
                                        get_sel_fn          get_sel,
                                        gpointer            get_sel_data,
                                        DonnaContextInfo   *info,
                                        GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (item != NULL, FALSE);
    g_return_val_if_fail (info != NULL, FALSE);
    g_return_val_if_fail (node_ref == NULL || DONNA_IS_NODE (node_ref), FALSE);
    g_return_val_if_fail (get_sel != NULL, FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);
    g_return_val_if_fail (interface != NULL, FALSE);

    if (interface->get_context_item_info == NULL)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType '%s': No context item supported",
                donna_columntype_get_name (ct));
        return FALSE;
    }

    return (*interface->get_context_item_info) (ct, data,
            item, extra, reference, node_ref, get_sel, get_sel_data,
            info, error);
}

/** donna_columntype_new_floating_window:
 * @tree: #DonnaTreeView where the column is
 * @destroy_on_sel_changed: Whether to destroy the window on selection change
 *
 * Helper function to create a little window that can be used to perform some
 * property editing on a given node (or selection of nodes).
 * This will create the window, attach it to @tree, remove decorations, set
 * %GDK_WINDOW_TYPE_HINT_UTILITY, set position to mouse pointer, set a border of
 * 6 pixels, make it non-resizable, and make it destroyed on location change on
 * @tree, as well as selection change if @destroy_on_sel_changed is %TRUE.
 *
 * You should call donna_app_set_floating_window() on the returned window after
 * having made it visible, otherwise this could lead to an instant destruction
 * on the window (as this call can destroy a previous floating window, thus
 * giving the focus back to app, thus leading to destruction of the new floating
 * window).
 *
 * Return: (transfer full): A new g_object_ref_sink()ed #GtkWindow
 */
inline GtkWindow *
donna_columntype_new_floating_window (DonnaTreeView *tree,
                                      gboolean       destroy_on_sel_changed)
{
    GtkWindow *win;

    win = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (win, FALSE);
    gtk_window_set_type_hint (win, GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_attached_to (win, (GtkWidget *) tree);
    gtk_window_set_position (win, GTK_WIN_POS_MOUSE);
    gtk_window_set_resizable (win, FALSE);
    g_object_set (win, "border-width", 6, NULL);
    g_object_ref_sink (win);
    g_signal_connect_swapped (tree, "notify::location",
            (GCallback) gtk_widget_destroy, win);
    if (destroy_on_sel_changed)
    {
        GtkTreeSelection *sel;

        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
        g_signal_connect_swapped (sel, "changed",
                (GCallback) gtk_widget_destroy, win);
    }
    return win;
}
