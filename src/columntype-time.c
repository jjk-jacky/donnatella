/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-time.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <glib-object.h>
#include <ctype.h>              /* isblank() */
#include <time.h>
#include "columntype.h"
#include "columntype-time.h"
#include "renderer.h"
#include "node.h"
#include "app.h"
#include "conf.h"
#include "util.h"
#include "macros.h"

/**
 * SECTION: columntype-time
 * @Short_description: To show a date/time from a property containing a
 * timestamp.
 *
 * Column type to show a date/time from a property containing a timestamp.
 *
 * <refsect2 id="ct-time-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>property</systemitem> (string) : Name of the property to use.
 *   Defaults to "mtime"
 * - <systemitem>format</systemitem> (string) : A format string on how to show
 *   the date/time. Defaults to "&percnt;f"
 *
 * And a few alternatives used specifically for the tooltip :
 *
 * - <systemitem>property_tooltip</systemitem>; You can use ":property"
 *   (default) to use the same as <systemitem>property</systemitem>
 * - <systemitem>format_tooltip</systemitem>; You can use ":format" to use the
 *   same as <systemitem>format</systemitem>. Defaults to "&percnt;R"
 *
 * The following options are shared between column and tooltip :
 *
 * - <systemitem>age_span_seconds</systemitem> (integer) : Number of seconds for
 *   &percnt;O If the timestamp is within this span, the age will be used, else
 *   <systemitem>age_fallback_format</systemitem> will be used; Defaults to
 *   7*24*3600 (7 days)
 * - <systemitem>age_fallback_format</systemitem> (string) : Format (using
 *   similar syntax as "format" only without &percnt;o/&percnt;O obviously) that
 *   will be used if not in the <systemitem>age_span_seconds</systemitem>;
 *   Defaults to "&percnt;F &percnt;T"
 * - <systemitem>fluid_time_format</systemitem> (string) : The format to use for
 *   the time when using &percnt;f Note that you cannot use donnatella
 *   extensions here (e.g. &percnt;o) Defaults to "%R"
 * - <systemitem>fluid_date_format</systemitem> (string) : The format to use for
 *   the date when using &percnt;f Note that you cannot use donnatella
 *   extensions here (e.g. &percnt;o) Defaults to "%F"
 * - <systemitem>fluid_short_weekday</systemitem> (boolean) : Whether or not to
 *   use abbreviated version of weekday name when using &percnt;f
 *   Specifically, whether "&percnt;a" or "&percnt;A" will be used. Defaults to
 *   false
 *
 * See g_date_time_format() for the supported format specifiers in
 * <systemitem>format</systemitem> and <systemitem>format_tooltip</systemitem>.
 * Additionally, donnatella adds the following additional format specifiers:
 *
 * - &percnt;o: the "age." It will show how much time has passed since the
 *   timestamp (or is left, should it be in the future). E.g: "1h 23m ago"
 * - &percnt;O: Same as &percnt;o only with a fallback format.
 * - &percnt;f: aka the "fluid" format. If the timestamp is from today, the time
 *   format will be used. If it is from yesterday, the time format will be used,
 *   prefixed with "Yesterday" If it is from one of the last 7 days, the time
 *   format will be used, prefixed by the weekday name. Else the date format
 *   will be used. The time and date formats used are defined via options
 *   <systemitem>fluid_time_format</systemitem> and
 *   <systemitem>fluid_date_format</systemitem> respectively; And whether the
 *   weekday names are used in full or abbreviated will be based on option
 *   <systemitem>fluid_short_weekday</systemitem>
 *
 * </para></refsect2>
 *
 * <refsect2 id="ct-time-filtering">
 * <title>Filtering</title>
 * <para>
 * You can filter by using the following format:
 * [UNIT] [COMP] VALUE
 *
 * Where COMP can be one of the usuals: <, <=, =, >=, or > If none specified, it
 * defaults to '='
 *
 * And UNIT defines which part of the date/time will be compared; It must be one
 * of the following:
 *
 * - Y : year
 * - m : month
 * - V : week
 * - d : day
 * - H : hour
 * - M : minute
 * - S : second
 * - D : full date
 * - j : day of the year
 * - u : day of the week (1-7 for Mon-Sun)
 * - w : day of the week (0-6 for Sun-Sat)
 * - A : age
 *
 * If none specified, it defaults to 'D'
 *
 * If UNIT is 'D' then VALUE should be a date, formatted YYYY-MM-DD[ HH:MM[:SS]]
 * If COMP is '=' then you can also follow it with a dash ('-') and another
 * date, using similar format, to indicate a range. It will then match if the
 * property is of a date/time within the given range.
 *
 * For all other units, VALUE must be a number. If UNIT is 'A' then the number
 * can be followed by another unit of Y, m, V, d, H, M, or S. If none is
 * specified, defaults to 'd'
 *
 * As with UNIT 'D' (except with UNIT 'A') when COMP is '=' then you can also
 * specify a range as VALUE, to match when the value compared is within the
 * specified range.
 *
 * Note that for days of the week ('u' and 'w') the order matters, so you can
 * use ranges like 6-1 to mean (with 'w') from Saturday to Monday (Sat, Sun,
 * Mon).
 *
 * This should be pretty obvious what can be done. There is one special case
 * worth mentioning, when comparing by age ('A') using '=' : in that case, "0d"
 * will mean today, "2V" two weeks ago, and so on.
 * So for instance, "A0d" will match if the timestamp is from today, and "A=2V"
 * will match if the timestamp is from the week from 2 weeks ago (as in, any
 * time during that week).
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
    WHICH_OTHER = 0,
    WHICH_MTIME,
    WHICH_ATIME,
    WHICH_CTIME,
};

struct tv_col_data
{
    gint8             which;
    gchar            *property;
    gchar            *format;
    DonnaTimeOptions  options;
    gint8             which_tooltip;
    gchar            *property_tooltip;
    gchar            *format_tooltip;
};

enum unit
{
    /* valid for unit & unit_age */
    UNIT_YEAR           = 'Y',
    UNIT_MONTH          = 'm',
    UNIT_WEEK           = 'V',
    UNIT_DAY            = 'd',
    UNIT_HOUR           = 'H',
    UNIT_MINUTE         = 'M',
    UNIT_SECOND         = 'S',
    /* invalid for unit_age */
    UNIT_DATE           = 'D',
    UNIT_DAY_OF_YEAR    = 'j',
    UNIT_DAY_OF_WEEK    = 'u',  /* 1-7, 1=Monday (ISO-8601) */
    UNIT_DAY_OF_WEEK_2  = 'w',  /* 0-6, 0=Sunday */
    /* special case */
    UNIT_AGE            = 'A',
};

enum comp
{
    COMP_LESSER_EQUAL,
    COMP_LESSER,
    COMP_EQUAL,
    COMP_GREATER,
    COMP_GREATER_EQUAL,
    COMP_IN_RANGE
};

struct filter_data
{
    enum unit   unit;
    enum unit   unit_age;
    enum comp   comp;
    guint64     ref;
    guint64     ref2;
};

struct _DonnaColumnTypeTimePrivate
{
    DonnaApp *app;
};

static void             ct_time_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_time_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_time_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_time_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_time_get_renderers       (DonnaColumnType    *ct);
static void             ct_time_get_options         (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed ct_time_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_time_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_time_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_time_can_edit            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_time_edit                (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_time_set_value           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_time_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_time_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_time_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gboolean         ct_time_refresh_filter_data (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     GError            **error);
static gboolean         ct_time_is_filter_match     (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     gpointer            filter_data,
                                                     DonnaNode          *node);
static void             ct_time_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_time_set_option       (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     gpointer            value,
                                                     gboolean            toggle,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_time_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_time_get_context_item_info (
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
ct_time_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_time_get_name;
    interface->get_renderers            = ct_time_get_renderers;
    interface->get_options              = ct_time_get_options;
    interface->refresh_data             = ct_time_refresh_data;
    interface->free_data                = ct_time_free_data;
    interface->get_props                = ct_time_get_props;
    interface->can_edit                 = ct_time_can_edit;
    interface->edit                     = ct_time_edit;
    interface->set_value                = ct_time_set_value;
    interface->render                   = ct_time_render;
    interface->set_tooltip              = ct_time_set_tooltip;
    interface->node_cmp                 = ct_time_node_cmp;
    interface->refresh_filter_data      = ct_time_refresh_filter_data;
    interface->is_filter_match          = ct_time_is_filter_match;
    interface->free_filter_data         = ct_time_free_filter_data;
    interface->set_option               = ct_time_set_option;
    interface->get_context_alias        = ct_time_get_context_alias;
    interface->get_context_item_info    = ct_time_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeTime, donna_column_type_time,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_time_column_type_init)
        )

static void
donna_column_type_time_class_init (DonnaColumnTypeTimeClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_time_set_property;
    o_class->get_property   = ct_time_get_property;
    o_class->finalize       = ct_time_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTimePrivate));
}

static void
donna_column_type_time_init (DonnaColumnTypeTime *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_TIME,
            DonnaColumnTypeTimePrivate);
}

static void
ct_time_finalize (GObject *object)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = DONNA_COLUMN_TYPE_TIME (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_time_parent_class)->finalize (object);
}

static void
ct_time_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_TIME (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_time_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_TIME (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_time_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TIME (ct), NULL);
    return "time";
}


static const gchar *
ct_time_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TIME (ct), NULL);
    return "t";
}

static void
ct_time_get_options (DonnaColumnType    *ct,
                     DonnaColumnOptionInfo **options,
                     guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "property",                   G_TYPE_STRING,      NULL },
        { "format",                     G_TYPE_STRING,      NULL },
        { "age_span_seconds",           G_TYPE_INT,         NULL },
        { "age_fallback_format",        G_TYPE_STRING,      NULL },
        { "fluid_time_format",          G_TYPE_STRING,      NULL },
        { "fluid_date_format",          G_TYPE_STRING,      NULL },
        { "fluid_short_weekday",        G_TYPE_BOOLEAN,     NULL },
        { "property_tooltip",           G_TYPE_STRING,      NULL },
        { "format_tooltip",             G_TYPE_STRING,      NULL }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

static DonnaColumnTypeNeed
ct_time_refresh_data (DonnaColumnType    *ct,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      const gchar        *tv_name,
                      gboolean            is_tree,
                      gpointer           *_data)
{
    DonnaColumnTypeTime *cttime = DONNA_COLUMN_TYPE_TIME (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;
    gboolean is_set;
    guint sec;

    config = donna_app_peek_config (cttime->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/time",
            "property", "mtime");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;

        if (streq (s, "mtime"))
            data->which = WHICH_MTIME;
        else if (streq (s, "atime"))
            data->which = WHICH_ATIME;
        else if (streq (s, "ctime"))
            data->which = WHICH_CTIME;
        else
            data->which = WHICH_OTHER;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "format", "%f");
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    sec = (guint) donna_config_get_int_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "age_span_seconds", 7*24*3600);
    if (data->options.age_span_seconds != sec)
    {
        data->options.age_span_seconds = sec;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "age_fallback_format", "%F %T");
    if (!streq (data->options.age_fallback_format, s))
    {
        g_free ((gchar *) data->options.age_fallback_format);
        data->options.age_fallback_format = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "fluid_time_format", "%R");
    if (!streq (data->options.fluid_time_format, s))
    {
        g_free ((gchar *) data->options.fluid_time_format);
        data->options.fluid_time_format = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "fluid_date_format", "%F %R");
    if (!streq (data->options.fluid_date_format, s))
    {
        g_free ((gchar *) data->options.fluid_date_format);
        data->options.fluid_date_format = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    is_set = donna_config_get_boolean_column (config, col_name,
            arr_name, tv_name, is_tree, "time",
            "fluid_short_weekday", FALSE);
    if (is_set != data->options.fluid_short_weekday)
    {
        data->options.fluid_short_weekday = is_set;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/time",
            "property_tooltip", ":property");
    if (!streq (data->property_tooltip, s))
    {
        g_free (data->property_tooltip);
        data->property_tooltip = s;

        if (streq (s, ":property"))
        {
            g_free (s);
            data->property_tooltip = NULL;
            s = data->property;
        }

        if (streq (s, "mtime"))
            data->which_tooltip = WHICH_MTIME;
        else if (streq (s, "atime"))
            data->which_tooltip = WHICH_ATIME;
        else if (streq (s, "ctime"))
            data->which_tooltip = WHICH_CTIME;
        else
            data->which_tooltip = WHICH_OTHER;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/time",
            "format_tooltip", "%R");
    if (!streq (data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        data->format_tooltip = s;

        if (streq (s, ":format"))
        {
            g_free (s);
            data->format_tooltip = NULL;
        }
    }
    else
        g_free (s);

    return need;
}

static void
ct_time_free_data (DonnaColumnType    *ct,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->format);
    g_free ((gchar *) data->options.age_fallback_format);
    g_free (data->property_tooltip);
    g_free (data->format_tooltip);
    g_free (data);
}

static GPtrArray *
ct_time_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TIME (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_uint64(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'time': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_UINT64));               \
    g_free (location);                                  \
} while (0)

struct editing_data
{
    struct tv_col_data *data;
    DonnaApp        *app;
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gulong           sid;
    GPtrArray       *arr;
    GtkWidget       *window;
    GtkToggleButton *rad_sel;
    GtkToggleButton *rad_ref;
    GtkEntry        *entry;
    GtkWidget       *btn_cancel;
};

static void
window_destroy_cb (struct editing_data *ed)
{
    if (ed->arr)
        g_ptr_array_unref (ed->arr);
    g_free (ed);
}

static gboolean
key_press_event_cb (GtkWidget *w, GdkEventKey *event, struct editing_data *ed)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_destroy (ed->window);
        return TRUE;
    }
    return FALSE;
}

static guint64
get_ref_time (struct tv_col_data *data, DonnaNode *node, gint which)
{
    GValue value = G_VALUE_INIT;
    DonnaNodeHasValue has;
    guint64 time;

    if (which == WHICH_OTHER)
        which = data->which;

    if (which == WHICH_MTIME)
        has = donna_node_get_mtime (node, TRUE, &time);
    else if (which == WHICH_ATIME)
        has = donna_node_get_atime (node, TRUE, &time);
    else if (which == WHICH_CTIME)
        has = donna_node_get_ctime (node, TRUE, &time);
    else
        donna_node_get (node, TRUE, data->property, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET && which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            return (guint64) -1;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    return (has == DONNA_NODE_VALUE_SET) ? time : (guint64) -1;
}

enum
{
    /* no joker in use */
    JOKER_NOT_SET = 0,
    /* before first element, use dt_ref */
    JOKER_BEFORE,
    /* use the joker */
    JOKER_USE
};

#define syntax_error()  do {                        \
    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,    \
            DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX, \
            "Invalid date format (must be [m|a|c|v[+/- x Y|m|V|d|h|m|s] [YYYY-MM-DD[ HH:MM[:SS]] or HH:MM[:SS]]): %s", \
            fmt);                                   \
    g_date_time_unref (dt_ref);                     \
    if (dt_cur)                                     \
        g_date_time_unref (dt_cur);                 \
    return (guint64) -1;                            \
} while (0)

#define get_element_from_dt(unit, dt)   do {                \
    switch (unit)                                           \
    {                                                       \
        case UNIT_YEAR:                                     \
            year = g_date_time_get_year (dt);               \
            break;                                          \
        case UNIT_MONTH:                                    \
            month = g_date_time_get_month (dt);             \
            break;                                          \
        case UNIT_DAY:                                      \
            day = g_date_time_get_day_of_month (dt);        \
            break;                                          \
        case UNIT_HOUR:                                     \
            hour = g_date_time_get_hour (dt);               \
            break;                                          \
        case UNIT_MINUTE:                                   \
            minute = g_date_time_get_minute (dt);           \
            break;                                          \
        case UNIT_SECOND:                                   \
            seconds = g_date_time_get_second (dt);          \
            break;                                          \
    }                                                       \
} while (0)

static guint64
get_ts (struct tv_col_data  *data,
        const gchar         *fmt,
        DonnaNode           *node_ref,
        DonnaNode           *node_cur,
        gboolean            *is_ts_fixed,
        GError             **error)
{
    GDateTime *dt_ref;
    GDateTime *dt_cur = NULL;
    guint i;
    guint64 ts;
    /* "default" values, when using joker '!' */
    gint year       = 1970;
    gint month      = 1;
    gint day        = 1;
    gint hour       = 0;
    gint minute     = 0;
    gdouble seconds = 0;

    if (is_ts_fixed)
        *is_ts_fixed = TRUE;
    skip_blank (fmt);

    /* is there a reference specified? */
    if (*fmt == 'n')
    {
        dt_ref = g_date_time_new_now_local ();
        ++fmt;
        skip_blank (fmt);
    }
    else
    {
        guint64 ref;

        switch (*fmt)
        {
            case 'm':
                ref = get_ref_time (data, node_ref, WHICH_MTIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'a':
                ref = get_ref_time (data, node_ref, WHICH_ATIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'c':
                ref = get_ref_time (data, node_ref, WHICH_CTIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'v':
                ++fmt;
                skip_blank (fmt);
                /* fall through */
            default:
                ref = get_ref_time (data, node_ref, WHICH_OTHER);
                break;
        }
        if (ref == (guint64) -1)
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_OTHER,
                    "Invalid reference time");
            return (guint64) -1;
        }
        dt_ref = g_date_time_new_from_unix_local ((gint64) ref);
    }

    /* any math to do on ref */
    while (*fmt == '+' || *fmt == '-')
    {
        GDateTime *dt;
        gint nb;

        nb = (gint) g_ascii_strtoll (fmt, (gchar **) &fmt, 10);
        skip_blank (fmt);
        switch (*fmt)
        {
            case UNIT_YEAR:
                dt = g_date_time_add_years (dt_ref, nb);
                break;
            case UNIT_MONTH:
                dt = g_date_time_add_months (dt_ref, nb);
                break;
            case UNIT_WEEK:
                dt = g_date_time_add_weeks (dt_ref, nb);
                break;
            case UNIT_DAY:
                dt = g_date_time_add_days (dt_ref, nb);
                break;
            case UNIT_HOUR:
                dt = g_date_time_add_hours (dt_ref, nb);
                break;
            case UNIT_MINUTE:
                dt = g_date_time_add_minutes (dt_ref, nb);
                break;
            case UNIT_SECOND:
                dt = g_date_time_add_seconds (dt_ref, nb);
                break;
            default:
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_OTHER,
                        "Invalid unit '%c' in reference math: %s",
                        *fmt, fmt);
                g_date_time_unref (dt_ref);
                return (guint64) -1;
        }
        g_date_time_unref (dt_ref);
        dt_ref = dt;
        ++fmt;
        skip_blank (fmt);
    }

    struct
    {
        gchar unit;
        gchar after;
    } elements[] = {
        { UNIT_YEAR,    '-' },
        { UNIT_MONTH,   '-' },
        { UNIT_DAY,      0  },
        { UNIT_HOUR,    ':' },
        { UNIT_MINUTE,  ':' },
        { UNIT_SECOND,   0  }
    };
    guint nb = sizeof (elements) / sizeof (elements[0]);
    gchar joker = 0;
    gchar unit_joker;
    gint  joker_st = JOKER_NOT_SET;

    /* process all date/time elements */
    for (i = 0; i < nb; ++i)
    {
        /* can we fill things with jokers? */
        if (joker_st != JOKER_NOT_SET)
        {
            if (elements[i].unit == unit_joker)
                joker_st = JOKER_USE;

            if (joker_st == JOKER_BEFORE)
                get_element_from_dt (elements[i].unit, dt_ref);
            else /* JOKER_USE */
            {
                if (joker == '#')
                    get_element_from_dt (elements[i].unit, dt_ref);
                else if (joker == '*')
                {
                    if (is_ts_fixed)
                        *is_ts_fixed = FALSE;
                    if (!dt_cur)
                    {
                        guint64 ref;

                        ref = get_ref_time (data, node_cur, WHICH_OTHER);
                        if (ref == (guint64) -1)
                        {
                            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                                    DONNA_COLUMN_TYPE_ERROR_OTHER,
                                    "Invalid current time");
                            g_date_time_unref (dt_ref);
                            return (guint64) -1;
                        }
                        dt_cur = g_date_time_new_from_unix_local ((gint64) ref);
                    }
                    get_element_from_dt (elements[i].unit, dt_cur);
                }
                /* else ('!') nothing to do, "defaults" are already set */
            }

            continue;
        }

        if (*fmt == '!' || *fmt == '*' || *fmt == '#')
        {
            joker = *fmt++;
            if (*fmt == UNIT_YEAR || *fmt == UNIT_MONTH || *fmt == UNIT_DAY
                    || *fmt == UNIT_HOUR || *fmt == UNIT_MINUTE
                    || *fmt == UNIT_SECOND || *fmt == '\0')
            {
                unit_joker = (*fmt == '\0') ? elements[i].unit : *fmt;
                /* will be adjusted on next loop iteration */
                joker_st = JOKER_BEFORE;
                /* next iteration should do thing element (again) */
                --i;
                continue;
            }
        }
        else if (*fmt >= '0' && *fmt <= '9')
        {
            guint64 num;

            num = g_ascii_strtoull (fmt, (gchar **) &fmt, 10);
            switch (elements[i].unit)
            {
                case UNIT_YEAR:
                    year = (gint) num;
                    break;
                case UNIT_MONTH:
                    month = (gint) num;
                    break;
                case UNIT_DAY:
                    day = (gint) num;
                    break;
                case UNIT_HOUR:
                    hour = (gint) num;
                    break;
                case UNIT_MINUTE:
                    minute = (gint) num;
                    break;
                case UNIT_SECOND:
                    seconds = (gdouble) num;
                    break;
            }
        }
        else if (*fmt == '\0')
            joker = '#';
        else
            syntax_error ();

        if (joker)
        {
            if (joker == '#')
                get_element_from_dt (elements[i].unit, dt_ref);
            else if (joker == '*')
            {
                if (is_ts_fixed)
                    *is_ts_fixed = FALSE;
                if (!dt_cur)
                {
                    guint64 ref;

                    ref = get_ref_time (data, node_cur, WHICH_OTHER);
                    if (ref == (guint64) -1)
                    {
                        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                                DONNA_COLUMN_TYPE_ERROR_OTHER,
                                "Invalid current time");
                        g_date_time_unref (dt_ref);
                        return (guint64) -1;
                    }
                    dt_cur = g_date_time_new_from_unix_local ((gint64) ref);
                }
                get_element_from_dt (elements[i].unit, dt_cur);
            }
            /* else ('!') nothing to do, "defaults" are already set */

            joker = 0;
        }

        if (elements[i].unit == UNIT_DAY)
        {
            /* moving to possible time */
            skip_blank (fmt);
        }
        else if (elements[i].after && *fmt == elements[i].after)
            /* skip separator */
            ++fmt;
    }

    g_date_time_unref (dt_ref);
    if (dt_cur)
        g_date_time_unref (dt_cur);

    dt_ref = g_date_time_new_local (year, month, day, hour, minute, seconds);
    ts = (guint64) g_date_time_to_unix (dt_ref);
    g_date_time_unref (dt_ref);
    return ts;
}

#undef get_date_bit
#undef syntax_error

static inline gboolean
set_prop (struct tv_col_data    *data,
          const gchar           *prop,
          guint64                ts,
          DonnaNode             *node,
          DonnaTreeView         *tree,
          GError               **error)
{
    GValue v = G_VALUE_INIT;
    guint64 ref;
    gboolean ret;

    ref = get_ref_time (data, node, data->which);
    if (ref != (guint64) -1 && ref == ts)
        return TRUE;

    g_value_init (&v, G_TYPE_UINT64);
    g_value_set_uint64 (&v, ts);
    ret = donna_tree_view_set_node_property (tree, node, prop, &v, error);
    g_value_unset (&v);
    return ret;
}

static gboolean
set_value (struct tv_col_data   *data,
           const gchar          *value,
           DonnaNode            *node_ref,
           GPtrArray            *nodes,
           DonnaTreeView        *tree,
           GError              **error)
{
    GError *err = NULL;
    GString *str = NULL;
    gboolean is_ts_fixed;
    const gchar *prop;
    guint64 ts = 0;
    guint i = 0;

    if (data->which == WHICH_MTIME)
        prop = "mtime";
    else if (data->which == WHICH_ATIME)
        prop = "atime";
    else if (data->which == WHICH_CTIME)
        prop = "ctime";
    else
        prop = data->property;

    if (node_ref)
    {
        ts = get_ts (data, value,
                /* node for reference time */
                node_ref,
                /* node for time preservation */
                nodes->pdata[0],
                /* whether there's time preservation or not */
                &is_ts_fixed,
                error);
        if (ts == (guint64) -1)
            return FALSE;
    }
    else
        is_ts_fixed = FALSE;

    if (node_ref)
        set_prop (data, prop, ts, nodes->pdata[i++], tree, error);

    for ( ; i < nodes->len; ++i)
    {
        if (!is_ts_fixed)
        {
            ts = get_ts (data, value,
                    (node_ref) ? node_ref : nodes->pdata[i],
                    nodes->pdata[i],
                    NULL, &err);
            if (ts == (guint64) -1)
            {
                gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                if (!str)
                    str = g_string_new (NULL);

                g_string_append_printf (str,
                        "\n- Failed to get new timestamp for '%s', skipping",
                        fl);

                g_free (fl);
                g_clear_error (&err);
                continue;
            }
        }

        if (!set_prop (data, prop, ts, nodes->pdata[i], tree, &err))
        {
            gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

            if (!str)
                str= g_string_new (NULL);

            g_string_append_printf (str, "\n- Failed to set '%s' on '%s': %s",
                    prop, fl, (err) ? err->message : "(no error message)");

            g_free (fl);
            g_clear_error (&err);
        }
    }

    if (!str)
        return TRUE;

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_PARTIAL_COMPLETION,
            "Some operations failed :\n%s", str->str);
    g_string_free (str, TRUE);

    return FALSE;
}

static void
apply_cb (struct editing_data *ed)
{
    GError *err = NULL;
    GPtrArray *arr;
    gboolean use_arr;
    gboolean is_ref_focused;
    gboolean is_ref_unique;

    if (ed->window)
    {
        gtk_widget_hide (ed->window);

        use_arr = (ed->arr && gtk_toggle_button_get_active (ed->rad_sel));
        is_ref_focused = !use_arr || !gtk_toggle_button_get_active (ed->rad_ref);
        is_ref_unique = !use_arr || (ed->arr->len == 1) || is_ref_focused;
    }
    else
    {
        use_arr = FALSE;
        is_ref_focused = is_ref_unique = TRUE;
    }

    if (use_arr)
        arr = g_ptr_array_ref (ed->arr);
    else
    {
        arr = g_ptr_array_sized_new (1);
        g_ptr_array_add (arr, ed->node);
    }

    if (!set_value (ed->data, gtk_entry_get_text (ed->entry),
            /* node for reference time */
            (is_ref_focused) ? ed->node
            : ((use_arr) ? ed->arr->pdata[0] : ed->node),
            arr, ed->tree, &err))
    {
        donna_app_show_error (ed->app, err,
                "ColumnType 'time': Operation failed");
        g_clear_error (&err);
    }

    if (ed->window)
        gtk_widget_destroy (ed->window);
}

static gboolean
key_pressed (struct editing_data *ed, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_activate (ed->btn_cancel);
        return TRUE;
    }

    return FALSE;
}


static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *ed)
{
    gboolean canceled;

    g_signal_handler_disconnect (editable, ed->sid);

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (!canceled)
        apply_cb (ed);
    /* when there's a window, ed gets free-d in window_destroy_cb; here we need
     * to do it now (and no known there's no ed->arr) */
    g_free (ed);
}

static void
set_entry_icon (GtkEntry *entry)
{
    gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, "help");
    gtk_entry_set_icon_activatable (entry, GTK_ENTRY_ICON_SECONDARY,
            FALSE);
    gtk_entry_set_icon_tooltip_markup (entry, GTK_ENTRY_ICON_SECONDARY,
            "[&lt;ref&gt;[&lt;math...&gt;]] [YYYY-MM-DD] [HH:MM:SS]\n"
            "\n"
            "&lt;ref&gt; can be <b>c</b>, <b>a</b>, <b>m</b>, <b>v</b> or <b>n</b> "
            "for ctime, atime, mtime, current value or current time (now).\n"
            "&lt;math&gt; must be +/-, a number and a unit: Y, m, V, d, H, M or S\n"
            "Calculation will be done to &lt;ref&gt; to get the reference value.\n"
            "If not specified, defaults to 'v' (i.e. current value).\n"
            "\n"
            "Each component of the date/time can be a number/value to use, or a joker:\n"
            "-<tt> * </tt> to preserve element from the current value\n"
            "-<tt> # </tt> to use element from the reference value\n"
            "-<tt> ! </tt> to use element from 1970-01-01 00:00:00\n"
            "\n"
            "If the last character is a joker, it applies to all remaining elements.\n"
            "Else, default joker<tt> # </tt>is used.\n"
            "\n"
            "The last joker can be followed by a unit (Y, m, d, H, M, or S);\n"
            "Then<tt> # </tt>is used until that unit, then the joker is used "
            "for this element and all remaining ones."
            );
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *ed)
{
    struct tv_col_data *data = ed->data;
    GValue value = G_VALUE_INIT;
    DonnaNodeHasValue has;
    guint64 time;
    gchar *s;

    g_signal_handler_disconnect (renderer, ed->sid);
    ed->entry = (GtkEntry *) editable;
    set_entry_icon (ed->entry);
    ed->sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, ed);

    if (data->which == WHICH_MTIME)
        has = donna_node_get_mtime (ed->node, FALSE, &time);
    else if (data->which == WHICH_ATIME)
        has = donna_node_get_atime (ed->node, FALSE, &time);
    else if (data->which == WHICH_CTIME)
        has = donna_node_get_ctime (ed->node, FALSE, &time);
    else
        donna_node_get (ed->node, FALSE, data->property, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET)
    {
        g_warning ("ColumnType 'time': Failed to get property value "
                "in order to set initial editing value");
        return;
    }
    else if (data->which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (ed->node);
            g_value_unset (&value);
            g_warning ("ColumnType 'time': Failed to get property value "
                    "in order to set initial editing value");
            return;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    s = donna_print_time (time, "%Y-%m-%d %H:%M:%S", &data->options);
    gtk_entry_set_text (ed->entry, s);
    g_free (s);
}

static gboolean
ct_time_can_edit (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node,
                  GError            **error)
{
    struct tv_col_data *data = _data;
    const gchar *prop;

    if (data->which == WHICH_MTIME)
        prop = "mtime";
    else if (data->which == WHICH_ATIME)
        prop = "atime";
    else if (data->which == WHICH_CTIME)
        prop = "ctime";
    else
        prop = data->property;

    return DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            prop, node, error);
}

static gboolean
ct_time_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderers,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    GPtrArray *arr;
    struct editing_data *ed;
    GtkWindow *win;
    GtkWidget *w;
    GtkGrid *grid;
    GtkBox *box;
    PangoAttrList *attr_list;
    gint row;
    gchar *s;
    gchar *ss;

    if (!ct_time_can_edit (ct, data, node, error))
        return FALSE;

    /* get selected nodes (if any) */
    arr = donna_tree_view_get_selected_nodes (treeview, NULL);

    ed = g_new0 (struct editing_data, 1);
    ed->data = data;
    ed->app  = ((DonnaColumnTypeTime *) ct)->priv->app;
    ed->tree = treeview;
    ed->node = node;

    if (!arr || (arr->len == 1 && node == arr->pdata[0]))
    {
        if (arr)
            g_ptr_array_unref (arr);

        ed->sid = g_signal_connect (renderers[0],
                "editing-started",
                (GCallback) editing_started_cb, ed);

        g_object_set (renderers[0], "editable", TRUE, NULL);
        if (!renderer_edit (renderers[0], re_data))
        {
            g_signal_handler_disconnect (renderers[0], ed->sid);
            g_free (ed);
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                    "ColumnType 'time': Failed to put renderer in edit mode");
            return FALSE;
        }
        return TRUE;
    }

    win = donna_column_type_new_floating_window (treeview, !!arr);
    ed->window = w = (GtkWidget *) win;
    g_signal_connect_swapped (ed->window, "key-press-event",
            (GCallback) key_pressed, ed);
    g_signal_connect_swapped (win, "destroy",
            (GCallback) window_destroy_cb, ed);

    w = gtk_grid_new ();
    grid = (GtkGrid *) w;
    g_object_set (w, "column-spacing", 12, NULL);
    gtk_container_add ((GtkContainer *) win, w);

    row = 0;
    ed->arr = arr;

    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "<i>Apply to:</i>");
    gtk_grid_attach (grid, w, 0, row++, 4, 1);

    s = ss = donna_node_get_name (node);
    w = gtk_radio_button_new_with_label (NULL, s);
    gtk_widget_set_tooltip_text (w, "Clicked item");
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    if (arr->len == 1)
        s = donna_node_get_name (arr->pdata[0]);
    else
        s = g_strdup_printf ("%d selected items", arr->len);
    w = gtk_radio_button_new_with_label_from_widget (
            (GtkRadioButton *) w, s);
    gtk_widget_set_tooltip_text (w, (arr->len == 1)
            ? "Selected item" : "Selected items");
    g_free (s);
    ed->rad_sel = (GtkToggleButton *) w;
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_label_new (NULL);
    g_object_set (w, "margin-top", 4, NULL);
    gtk_label_set_markup ((GtkLabel *) w,
            "<b>c</b>time, <b>m</b>time, <b>a</b>time and current <b>v</b>alue relate to:");
    attr_list = pango_attr_list_new ();
    pango_attr_list_insert (attr_list,
            pango_attr_style_new (PANGO_STYLE_ITALIC));
    gtk_label_set_attributes ((GtkLabel *) w, attr_list);
    pango_attr_list_unref (attr_list);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_radio_button_new_with_label (NULL, ss);
    gtk_widget_set_tooltip_text (w, "Clicked item");
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_radio_button_new_with_label_from_widget (
            (GtkRadioButton *) w, "Touched item");
    gtk_widget_set_tooltip_text (w, "The item on which the time is set");
    ed->rad_ref = (GtkToggleButton *) w;
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    g_free (ss);
    g_object_set (w, "margin-bottom", 9, NULL);

    ++row;
    w = gtk_entry_new ();
    ed->entry = (GtkEntry *) w;
    gtk_entry_set_activates_default (ed->entry, TRUE);
    set_entry_icon (ed->entry);
    g_signal_connect (w, "key-press-event", (GCallback) key_press_event_cb, ed);
    g_signal_connect_swapped (w, "activate", (GCallback) apply_cb, ed);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, FALSE);
    box = (GtkBox *) w;
    g_object_set (w, "margin-top", 15, NULL);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    w = gtk_button_new_with_label ("Set time");
    gtk_widget_set_can_default (w, TRUE);
    gtk_window_set_default ((GtkWindow *) ed->window, w);
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) apply_cb, ed);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);
    w = ed->btn_cancel = gtk_button_new_with_label ("Cancel");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_object_set (gtk_button_get_image ((GtkButton *) w),
            "icon-size", GTK_ICON_SIZE_MENU, NULL);
    g_signal_connect_swapped (w, "clicked",
            (GCallback) gtk_widget_destroy, win);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);


    donna_app_set_floating_window (((DonnaColumnTypeTime *) ct)->priv->app, win);
    gtk_widget_show_all (ed->window);
    gtk_widget_grab_focus ((GtkWidget *) ed->entry);
    return TRUE;
}

static gboolean
ct_time_set_value (DonnaColumnType    *ct,
                   gpointer            _data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    struct tv_col_data *data = _data;

    if (*value == '=')
    {
        if (!node_ref)
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "ColumnType 'time': Prefix '=' to set_value given without reference");
            return FALSE;
        }
        else
            ++value;
    }
    else
        node_ref = NULL;

    return set_value (data, value, node_ref, nodes, treeview, error);
}

static GPtrArray *
ct_time_render (DonnaColumnType    *ct,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint64 time;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TIME (ct), NULL);

    if (data->which == WHICH_MTIME)
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (data->which == WHICH_ATIME)
        has = donna_node_get_atime (node, FALSE, &time);
    else if (data->which == WHICH_CTIME)
        has = donna_node_get_ctime (node, FALSE, &time);
    else
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
    else if (data->which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            g_object_set (renderer, "visible", FALSE, NULL);
            return NULL;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    s = donna_print_time (time, data->format, &data->options);
    g_object_set (renderer,
            "visible",      TRUE,
            "text",         s,
            "ellipsize",    PANGO_ELLIPSIZE_END,
            "ellipsize-set",TRUE,
            NULL);
    donna_renderer_set (renderer, "ellipsize-set", NULL);
    g_free (s);
    return NULL;
}

static gboolean
ct_time_set_tooltip (DonnaColumnType    *ct,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint64 time;
    gchar *property;
    gchar *format;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TIME (ct), NULL);

    if (!data->property_tooltip)
        property = data->property;
    else
        property = data->property_tooltip;

    if (!data->format_tooltip)
        format = data->format;
    else
        format = data->format_tooltip;

    if (format[0] == '\0')
        return FALSE;

    if (data->which_tooltip == WHICH_MTIME)
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_ATIME)
        has = donna_node_get_atime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_CTIME)
        has = donna_node_get_ctime (node, FALSE, &time);
    else
        donna_node_get (node, FALSE, property, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    if (data->which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            return FALSE;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    s = donna_print_time (time, format, &data->options);
    gtk_tooltip_set_text (tooltip, s);
    g_free (s);
    return TRUE;
}

static gint
ct_time_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    guint64 time1;
    guint64 time2;

    if (data->which == WHICH_MTIME)
    {
        has1 = donna_node_get_mtime (node1, TRUE, &time1);
        has2 = donna_node_get_mtime (node2, TRUE, &time2);
    }
    else if (data->which == WHICH_ATIME)
    {
        has1 = donna_node_get_atime (node1, TRUE, &time1);
        has2 = donna_node_get_atime (node2, TRUE, &time2);
    }
    else if (data->which == WHICH_CTIME)
    {
        has1 = donna_node_get_ctime (node1, TRUE, &time1);
        has2 = donna_node_get_ctime (node2, TRUE, &time2);
    }
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
        if (has1 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node1);
                has1 = DONNA_NODE_VALUE_ERROR;
            }
            else
                time1 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
        donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
        if (has2 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node2);
                has2 = DONNA_NODE_VALUE_ERROR;
            }
            else
                time2 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            return -1;
        else
            return 0;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
        return 1;

    return (time1 > time2) ? 1 : (time1 < time2) ? -1 : 0;
}

#define error_out(err_code, ...) do {       \
    g_set_error (error, DONNA_FILTER_ERROR, \
            err_code, __VA_ARGS__);         \
    g_free (fd);                            \
    *filter_data = NULL;                    \
    return FALSE;                           \
} while (0)

#define get_date_bit(variable, length)  do {                \
    variable = 0;                                           \
    for (i = 0; i < length; ++i)                            \
    {                                                       \
        if (*filter < '0' || *filter > '9')                 \
            error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,   \
                    "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",\
                    filter);                                \
        variable = variable * 10 + (*filter - '0');         \
        ++filter;                                           \
    }                                                       \
} while (0)

#define check(type, unit)   do {                \
    if (g_date_time_get_##type (dt)             \
            != g_date_time_get_##type (dt2))    \
        goto age_done;                          \
    if (fd->unit_age == UNIT_##unit)            \
    {                                           \
        ret = TRUE;                             \
        goto age_done;                          \
    }                                           \
} while (0)

static gboolean
ct_time_refresh_filter_data (DonnaColumnType    *ct,
                             const gchar        *filter,
                             gpointer           *filter_data,
                             GError            **error)
{
    struct filter_data *fd;
    GDateTime *dt;
    guint64 r = 0;
    guint i;
    gchar *s;

    if (*filter_data)
        fd = *filter_data;
    else
        fd = *filter_data = g_new0 (struct filter_data, 1);

    fd->comp = COMP_EQUAL;

    while (isblank (*filter))
        ++filter;

    /* get unit */
    switch (*filter)
    {
        case UNIT_YEAR:
        case UNIT_MONTH:
        case UNIT_WEEK:
        case UNIT_DAY:
        case UNIT_HOUR:
        case UNIT_MINUTE:
        case UNIT_SECOND:
        case UNIT_DATE:
        case UNIT_DAY_OF_YEAR:
        case UNIT_DAY_OF_WEEK:
        case UNIT_DAY_OF_WEEK_2:
        case UNIT_AGE:
            fd->unit = *filter++;
    }
    if (fd->unit == 0)
        fd->unit = UNIT_DATE;

    while (isblank (*filter))
        ++filter;

    /* get comp */
    if (*filter == '<')
    {
        ++filter;
        if (*filter == '=')
        {
            ++filter;
            fd->comp = COMP_LESSER_EQUAL;
        }
        else
            fd->comp = COMP_LESSER;
    }
    else if (*filter == '>')
    {
        ++filter;
        if (*filter == '=')
        {
            ++filter;
            fd->comp = COMP_GREATER_EQUAL;
        }
        else
            fd->comp = COMP_GREATER;
    }
    else if (*filter == '=')
        ++filter;

    while (isblank (*filter))
        ++filter;

    /* get ref */
    if (fd->unit == UNIT_DATE)
    {
        gint year, month, day, hour, minute;
        gdouble seconds;
        gboolean again = FALSE;

get_date:
        /* must be formatted as such: YYYY-MM-DD[ HH:MM[:SS]] */
        get_date_bit (year, 4);
        if (*filter != '-')
            error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                    "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                    filter);
        get_date_bit (month, 2);
        if (*filter != '-')
            error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                    "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                    filter);
        get_date_bit (day, 2);

        while (isblank (*filter))
            ++filter;

        /* what we can have here is nothing, '-' before another date (RANGE)
         * or, ofc, a time */
        if (*filter >= '0' && *filter <= '9')
        {
            get_date_bit (hour, 2);
            if (*filter != ':')
                error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                        filter);
            get_date_bit (minute, 2);
            if (*filter == ':')
            {
                ++filter;
                get_date_bit (seconds, 2);
            }
        }
        else
        {
            hour = minute = 0;
            seconds = 0.0;
        }

        dt = g_date_time_new_local (year, month, day, hour, minute, seconds);
        if (again)
        {
            r = (guint64) g_date_time_to_unix (dt);
            if (r > fd->ref)
                fd->ref2 = r;
            else
            {
                fd->ref2 = fd->ref;
                fd->ref  = r;
            }
        }
        else
            fd->ref = (guint64) g_date_time_to_unix (dt);
        g_date_time_unref (dt);

        if (!again && fd->comp == COMP_EQUAL && *filter == '-')
        {
            fd->comp = COMP_IN_RANGE;
            again = TRUE;
            ++filter;
            while (isblank (*filter))
                ++filter;
            goto get_date;
        }

        return TRUE;
    }

    /* just a number */
    fd->ref = g_ascii_strtoull (filter, &s, 10);
    filter = (const gchar *) s;

    while (isblank (*filter))
        ++filter;

    /* AGE requires another unit (unit_age) */
    if (fd->unit == UNIT_AGE)
    {
        switch (*filter)
        {
            case UNIT_YEAR:
            case UNIT_MONTH:
            case UNIT_WEEK:
            case UNIT_DAY:
            case UNIT_HOUR:
            case UNIT_MINUTE:
            case UNIT_SECOND:
                fd->unit_age = *filter++;
        }
        if (fd->unit_age == 0)
            fd->unit_age = UNIT_DAY;
    }
    else if (fd->comp == COMP_EQUAL && *filter == '-')
    {
        fd->comp = COMP_IN_RANGE;
        ++filter;
        r = g_ascii_strtoull (filter, &s, 10);
        filter = (const gchar *) s;

        /* we keep them in the specified order for day of week-s, so we
         * support e.g. 6-1 to say from Sat to Mon (Sat, Sun, Mon) */
        if (fd->unit == UNIT_DAY_OF_WEEK || fd->unit == UNIT_DAY_OF_WEEK_2
                || r > fd->ref)
            fd->ref2 = r;
        else
        {
            fd->ref2 = fd->ref;
            fd->ref  = r;
        }
    }

    return TRUE;
}

static gboolean
ct_time_is_filter_match (DonnaColumnType    *ct,
                         gpointer            _data,
                         gpointer            filter_data,
                         DonnaNode          *node)
{
    struct tv_col_data *data = _data;
    struct filter_data *fd = filter_data;
    DonnaNodeHasValue has;
    guint64 time;
    GDateTime *dt;
    guint64 r = 0;

    if (data->which == WHICH_MTIME)
        has = donna_node_get_mtime (node, TRUE, &time);
    else if (data->which == WHICH_ATIME)
        has = donna_node_get_atime (node, TRUE, &time);
    else if (data->which == WHICH_CTIME)
        has = donna_node_get_ctime (node, TRUE, &time);
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node, TRUE, data->property, &has, &value, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node);
                has = DONNA_NODE_VALUE_ERROR;
            }
            else
                time = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
    }

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    switch (fd->unit)
    {
        case UNIT_YEAR:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MONTH:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_WEEK:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_week_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_day_of_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_HOUR:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_hour (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MINUTE:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_minute (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_SECOND:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_second (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DATE:
            r = time;
            break;
        case UNIT_DAY_OF_YEAR:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_day_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK:
            dt = g_date_time_new_from_unix_local ((gint64) time);
            r = (guint64) g_date_time_get_day_of_week (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK_2:
            {
                gchar *s;

                dt = g_date_time_new_from_unix_local ((gint64) time);
                s = g_date_time_format (dt, "%w");
                g_date_time_unref (dt);
                r = g_ascii_strtoull (s, NULL, 10);
                g_free (s);
                break;
            }
        case UNIT_AGE:
            {
                GDateTime *dt2 = NULL;

                dt = g_date_time_new_now_local ();
                if (fd->ref == 0)
                    dt2 = g_date_time_ref (dt);
                else
                    switch (fd->unit_age)
                    {
                        case UNIT_YEAR:
                            dt2 = g_date_time_add_years (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_MONTH:
                            dt2 = g_date_time_add_months (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_WEEK:
                            dt2 = g_date_time_add_weeks (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_DAY:
                            dt2 = g_date_time_add_days (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_HOUR:
                            dt2 = g_date_time_add_hours (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_MINUTE:
                            dt2 = g_date_time_add_minutes (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_SECOND:
                            dt2 = g_date_time_add_seconds (dt, -1 * (gint) fd->ref);
                            break;
                        case UNIT_DATE:
                        case UNIT_DAY_OF_YEAR:
                        case UNIT_DAY_OF_WEEK:
                        case UNIT_DAY_OF_WEEK_2:
                        case UNIT_AGE:
                            /* silence warnings */
                            g_return_val_if_reached (FALSE);
                    }
                r = (guint64) g_date_time_to_unix (dt2);
                g_date_time_unref (dt);
                if (fd->comp != COMP_EQUAL)
                    g_date_time_unref (dt2);

                /* comparisons are reversed: age <= 5m == its time >= t-5m */
                switch (fd->comp)
                {
                    case COMP_LESSER_EQUAL:
                        return time >= r;

                    case COMP_LESSER:
                        return time > r;

                    case COMP_EQUAL:
                        {
                            gboolean ret = FALSE;

                            /* special case:
                             * age = 0d == today
                             * age = 2V == 2 weeks ago, i.e. during that week
                             * etc */
                            dt = g_date_time_new_from_unix_local ((gint64) time);

                            if (fd->unit_age == UNIT_WEEK)
                            {
                                /* week is a special case, as one week can
                                 * spread over two months, even two years. This
                                 * is why here we use week_numbering_year
                                 * instead of year, and do not check month. */
                                check (week_numbering_year, YEAR);
                                check (week_of_year,        WEEK);
                            }
                            else
                            {
                                check (year,         YEAR);
                                check (month,        MONTH);
                                check (day_of_month, DAY);
                                check (hour,         HOUR);
                                check (minute,       MINUTE);
                                check (second,       SECOND);
                            }
age_done:
                            g_date_time_unref (dt);
                            g_date_time_unref (dt2);
                            return ret;
                        }

                    case COMP_GREATER:
                        return time < r;

                    case COMP_GREATER_EQUAL:
                        return time <= r;

                    case COMP_IN_RANGE:
                        /* silence warning */
                        break;
                }
                g_return_val_if_reached (FALSE);
            }
    }

    switch (fd->comp)
    {
        case COMP_LESSER_EQUAL:
            return r <= fd->ref;

        case COMP_LESSER:
            return r < fd->ref;

        case COMP_EQUAL:
            return r == fd->ref;

        case COMP_GREATER:
            return r > fd->ref;

        case COMP_GREATER_EQUAL:
            return r >= fd->ref;

        case COMP_IN_RANGE:
            /* spacial handling for DAY_OF_WEEK cases */
            if (fd->ref > fd->ref2)
                return r >= fd->ref || r <= fd->ref2;
            else
                return r >= fd->ref && r <= fd->ref2;
    }

    g_return_val_if_reached (FALSE);
}

static void
ct_time_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    g_free (filter_data);
}

static DonnaColumnTypeNeed
ct_time_set_option (DonnaColumnType    *ct,
                    const gchar        *col_name,
                    const gchar        *arr_name,
                    const gchar        *tv_name,
                    gboolean            is_tree,
                    gpointer            _data,
                    const gchar        *option,
                    gpointer            value,
                    gboolean            toggle,
                    DonnaColumnOptionSaveLocation save_location,
                    GError            **error)
{
    struct tv_col_data *data = _data;
    gpointer v;

    if (streq (option, "property"))
    {
        v = (value) ? value : &data->property;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/time",
                    &save_location,
                    option, G_TYPE_STRING, &data->property, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->property);
            data->property = g_strdup (* (gchar **) value);

            if (streq (data->property, "mtime"))
                data->which = WHICH_MTIME;
            else if (streq (data->property, "atime"))
                data->which = WHICH_ATIME;
            else if (streq (data->property, "ctime"))
                data->which = WHICH_CTIME;
            else
                data->which = WHICH_OTHER;

            if (!data->property_tooltip)
                data->which_tooltip = data->which;
        }

        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "format"))
    {
        v = (value) ? value : &data->format;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_STRING, &data->format, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->format);
            data->format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "age_span_seconds"))
    {
        gint c;

        c = (gint) data->options.age_span_seconds;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_INT, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->options.age_span_seconds = (guint) * (gint *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "age_fallback_format"))
    {
        gchar *c;

        c = (gchar *) data->options.age_fallback_format;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_STRING, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (c);
            data->options.age_fallback_format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_tooltip"))
    {
        v = (value) ? value : &data->property_tooltip;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/time",
                    &save_location,
                    option, G_TYPE_STRING, &data->property_tooltip, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            gchar *s_value = * (gchar **) value;

            g_free (data->property_tooltip);
            if (streq (s_value, ":property"))
            {
                data->property_tooltip = NULL;
                s_value = data->property;
            }
            else
                data->property_tooltip = g_strdup (s_value);

            if (streq (s_value, "mtime"))
                data->which_tooltip = WHICH_MTIME;
            else if (streq (s_value, "atime"))
                data->which_tooltip = WHICH_ATIME;
            else if (streq (s_value, "ctime"))
                data->which_tooltip = WHICH_CTIME;
            else
                data->which_tooltip = WHICH_OTHER;
        }

        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }
    else if (streq (option, "format_tooltip"))
    {
        v = (value) ? value : &data->format_tooltip;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/time",
                    &save_location,
                    option, G_TYPE_STRING, &data->format_tooltip, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            gchar *s_value = * (gchar **) value;

            g_free (data->format_tooltip);
            if (streq (s_value, ":format"))
                data->format_tooltip = NULL;
            else
                data->format_tooltip = g_strdup (s_value);
        }
        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }
    else if (streq (option, "fluid_time_format"))
    {
        gchar *c;

        c = (gchar *) data->options.fluid_time_format;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_STRING, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (c);
            data->options.fluid_time_format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "fluid_date_format"))
    {
        gchar *c;

        c = (gchar *) data->options.fluid_date_format;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_STRING, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (c);
            data->options.fluid_date_format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "fluid_short_weekday"))
    {
        gboolean c;

        c = data->options.fluid_short_weekday;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "time", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->options.fluid_short_weekday = * (gboolean *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'time': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_time_get_context_alias (DonnaColumnType   *ct,
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
                "ColumnType 'time': Unknown alias '%s'",
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
                "ColumnType 'time': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "format:@", save_location, "<",
                prefix, "format:@", save_location, ":%f,",
                prefix, "format:@", save_location, ":%O,",
                prefix, "format:@", save_location, ":%x %X,",
                prefix, "format:@", save_location, ":%x,",
                prefix, "format:@", save_location, ":%X,",
                prefix, "format:@", save_location, ":%F %T,",
                prefix, "format:@", save_location, ":%F,",
                prefix, "format:@", save_location, ":%T,",
                prefix, "format:@", save_location, ":%d/%m/%Y %T,",
                prefix, "format:@", save_location, ":%d/%m/%Y,-,",
                prefix, "format:@", save_location, ":=>,",
            prefix, "property:@", save_location, "<",
                prefix, "property:@", save_location, ":mtime,",
                prefix, "property:@", save_location, ":atime,",
                prefix, "property:@", save_location, ":ctime,-,",
                prefix, "property:@", save_location, ":custom>,-,",
            prefix, "format_tooltip:@", save_location, "<",
                prefix, "format_tooltip:@", save_location, "::format,-,",
                prefix, "format_tooltip:@", save_location, ":%O,",
                prefix, "format_tooltip:@", save_location, ":%x %X,",
                prefix, "format_tooltip:@", save_location, ":%x,",
                prefix, "format_tooltip:@", save_location, ":%X,",
                prefix, "format_tooltip:@", save_location, ":%F %T,",
                prefix, "format_tooltip:@", save_location, ":%F,",
                prefix, "format_tooltip:@", save_location, ":%T,",
                prefix, "format_tooltip:@", save_location, ":%d/%m/%Y %T,",
                prefix, "format_tooltip:@", save_location, ":%d/%m/%Y,-,",
                prefix, "format_tooltip:@", save_location, ":=>,",
            prefix, "property_tooltip:@", save_location, "<",
                prefix, "property_tooltip:@", save_location, "::property,-,",
                prefix, "property_tooltip:@", save_location, ":mtime,",
                prefix, "property_tooltip:@", save_location, ":atime,",
                prefix, "property_tooltip:@", save_location, ":ctime,-,",
                prefix, "property_tooltip:@", save_location, ":custom>,-,",
            prefix, "age_span_seconds:@", save_location, "<",
                prefix, "age_span_seconds:@", save_location, ":1h,",
                prefix, "age_span_seconds:@", save_location, ":24h,",
                prefix, "age_span_seconds:@", save_location, ":48h,",
                prefix, "age_span_seconds:@", save_location, ":1w,-,",
                prefix, "age_span_seconds:@", save_location, ":=>,",
            prefix, "age_fallback_format:@", save_location, "<",
                prefix, "age_fallback_format:@", save_location, ":%o,",
                prefix, "age_fallback_format:@", save_location, ":%x %X,",
                prefix, "age_fallback_format:@", save_location, ":%x,",
                prefix, "age_fallback_format:@", save_location, ":%X,",
                prefix, "age_fallback_format:@", save_location, ":%F %T,",
                prefix, "age_fallback_format:@", save_location, ":%F,",
                prefix, "age_fallback_format:@", save_location, ":%T,",
                prefix, "age_fallback_format:@", save_location, ":%d/%m/%Y %T,",
                prefix, "age_fallback_format:@", save_location, ":%d/%m/%Y,-,",
                prefix, "age_fallback_format:@", save_location, ":=>,",
            prefix, "fluid_time_format:@", save_location, "<",
                prefix, "fluid_time_format:@", save_location, ":%R,",
                prefix, "fluid_time_format:@", save_location, ":%T,",
                prefix, "fluid_time_format:@", save_location, ":%l:%M %p,",
                prefix, "fluid_time_format:@", save_location, ":%l:%M %P,",
                prefix, "fluid_time_format:@", save_location, ":%X,-,",
                prefix, "fluid_time_format:@", save_location, ":=>,",
            prefix, "fluid_date_format:@", save_location, "<",
                prefix, "fluid_date_format:@", save_location, ":%F,",
                prefix, "fluid_date_format:@", save_location, ":%d/%m/%Y,",
                prefix, "fluid_date_format:@", save_location, ":%x,-,",
                prefix, "fluid_date_format:@", save_location, ":=>,",
            prefix, "fluid_short_weekday:@", save_location,
            NULL);
}

static gboolean
ct_time_get_context_item_info (DonnaColumnType   *ct,
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
    gchar buf[10];
    const gchar *value;
    const gchar *ask_title;
    const gchar *ask_current;
    const gchar *save_location;
    gboolean quote_value = FALSE;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        if (!extra)
        {
            info->name = g_strconcat ("Node Property: ", data->property, NULL);
            info->free_name = TRUE;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = data->property;
        }
        else if (streq (extra, "mtime"))
        {
            info->name = "Modified Time";
            info->desc = "mtime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->which == WHICH_MTIME;
            value = "mtime";
        }
        else if (streq (extra, "atime"))
        {
            info->name = "Accessed Time";
            info->desc = "atime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->which == WHICH_ATIME;
            value = "atime";
        }
        else if (streq (extra, "ctime"))
        {
            info->name = "Status Change Time";
            info->desc = "ctime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->which == WHICH_CTIME;
            value = "ctime";
        }
        else if (streq (extra, "custom"))
        {
            if (data->which == WHICH_MTIME)
                info->name = "Custom: mtime";
            else if (data->which == WHICH_ATIME)
                info->name = "Custom: atime";
            else if (data->which == WHICH_CTIME)
                info->name = "Custom: ctime";
            else /* WHICH_OTHER */
            {
                info->name = g_strconcat ("Custom: ", data->property, NULL);
                info->free_name = TRUE;
            }
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = data->property;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'time': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else if (streq (item, "property_tooltip"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        if (!extra)
        {
            info->name = g_strconcat ("Tooltip: Node Property: ",
                    (data->property_tooltip) ? data->property_tooltip : ":property", NULL);
            value = NULL;
            ask_title = "Enter the name of the property";
            if (data->property_tooltip)
                ask_current = data->property_tooltip;
            else
                ask_current = ":property";
        }
        else if (streq (extra, ":property"))
        {
            info->name = "Same As Column";
            info->desc = "i.e. use option 'property'";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = !data->property_tooltip;
            value = ":property";
        }
        else if (streq (extra, "mtime"))
        {
            info->name = "Modified Time";
            info->desc = "mtime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->property_tooltip && data->which_tooltip == WHICH_MTIME;
            value = "mtime";
        }
        else if (streq (extra, "atime"))
        {
            info->name = "Accessed Time";
            info->desc = "atime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->property_tooltip && data->which_tooltip == WHICH_ATIME;
            value = "atime";
        }
        else if (streq (extra, "ctime"))
        {
            info->name = "Status Change Time";
            info->desc = "ctime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->property_tooltip && data->which_tooltip == WHICH_CTIME;
            value = "ctime";
        }
        else if (streq (extra, "custom"))
        {
            if (data->which == WHICH_MTIME)
                info->name = "Custom: mtime";
            else if (data->which == WHICH_ATIME)
                info->name = "Custom: atime";
            else if (data->which == WHICH_CTIME)
                info->name = "Custom: ctime";
            else /* WHICH_OTHER */
            {
                info->name = g_strconcat ("Custom: ",
                        (data->property_tooltip) ? data->property_tooltip : ":property",
                        NULL);
                info->free_name = TRUE;
            }
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = data->property_tooltip;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'time': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else if (streq (item, "format"))
    {
        gchar *s;
        guint64 now = (guint64) time (NULL);

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now, data->format, &data->options);
            info->name = g_strconcat ("Column: ", s, NULL);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
            g_free (s);
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format);
            s = donna_print_time (now, extra, &data->options);
            info->name = s;
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "format_tooltip"))
    {
        gchar *cur = (data->format_tooltip) ? data->format_tooltip : (gchar *) ":format";
        gchar *s;
        guint64 now = (guint64) time (NULL);

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now,
                    (data->format_tooltip) ? data->format_tooltip : data->format,
                    &data->options);
            info->name = g_strconcat ("Tooltip: ", s, NULL);
            g_free (s);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", cur, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = cur;
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ", cur, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = cur;
        }
        else if (streq (extra, ":format"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = !data->format_tooltip;
            info->name = "Same as Column";
            info->desc = "i.e. use option 'format'";
            value = ":format";
            quote_value = TRUE;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format_tooltip);
            info->name = donna_print_time (now, extra, &data->options);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "age_fallback_format"))
    {
        gchar *s;
        guint64 now = (guint64) time (NULL);

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now, data->options.age_fallback_format,
                    &data->options);
            info->name = g_strconcat ("Fallback: ", s, NULL);
            g_free (s);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ",
                    data->options.age_fallback_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fallback format for the column";
            ask_current = data->options.age_fallback_format;
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ",
                    data->options.age_fallback_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fallback format for the column";
            ask_current = data->options.age_fallback_format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->options.age_fallback_format);
            info->name = donna_print_time (now, extra, &data->options);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "age_span_seconds"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = g_strdup_printf ("Age fallback after: %d seconds",
                    data->options.age_span_seconds);
            info->free_name = TRUE;
            value = NULL;
            ask_title = "Age fallback after how many seconds ?";
            snprintf (buf, 10, "%d", data->options.age_span_seconds);
            ask_current = buf;
        }
        else if (*extra == '=')
        {
            ++extra;
            if (*extra == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra);
                info->free_name = TRUE;
            }

            value = NULL;
            ask_title = "Age fallback after how many seconds ?";
            snprintf (buf, 10, "%d", data->options.age_span_seconds);
            ask_current = buf;
        }
        else if (streq (extra, "1h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->options.age_span_seconds == 3600;
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "3600";
        }
        else if (streq (extra, "24h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->options.age_span_seconds == 86400;
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "86400";
        }
        else if (streq (extra, "48h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->options.age_span_seconds == 172800;
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "172800";
        }
        else if (streq (extra, "1w"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->options.age_span_seconds == 604800;
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "604800";
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'time': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else if (streq (item, "fluid_time_format"))
    {
        gchar *s;
        guint64 now = (guint64) time (NULL);

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now, data->options.fluid_time_format,
                    &data->options);
            info->name = g_strconcat ("Fluid Time Format: ", s, NULL);
            g_free (s);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Time format used within %f: ",
                    data->options.fluid_time_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fluid time format for the column";
            ask_current = data->options.fluid_time_format;
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ",
                    data->options.fluid_time_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fluid time format for the column";
            ask_current = data->options.fluid_time_format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->options.fluid_time_format);
            info->name = donna_print_time (now, extra, &data->options);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "fluid_date_format"))
    {
        gchar *s;
        guint64 now = (guint64) time (NULL);

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now, data->options.fluid_date_format,
                    &data->options);
            info->name = g_strconcat ("Fluid Date Format: ", s, NULL);
            g_free (s);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Date format used within %f: ",
                    data->options.fluid_date_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fluid date format for the column";
            ask_current = data->options.fluid_date_format;
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ",
                    data->options.fluid_date_format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the fluid date format for the column";
            ask_current = data->options.fluid_date_format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->options.fluid_date_format);
            info->name = donna_print_time (now, extra, &data->options);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "fluid_short_weekday"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = data->options.fluid_short_weekday;
        info->name = "Use abbreviated weekday name in fluid format (%f)";
        value = (info->is_active) ? "0" : "1";
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'time': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, quote_value,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
