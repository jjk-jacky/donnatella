
#include <glib-object.h>
#include <ctype.h>              /* isblank() */
#include "columntype.h"
#include "columntype-time.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "util.h"
#include "macros.h"

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
    DonnaTimeOptions  options_tooltip;
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
static DonnaColumnTypeNeed ct_time_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_time_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_time_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GtkMenu *        ct_time_get_options_menu    (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_time_handle_context      (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
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
static gboolean         ct_time_is_match_filter     (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static void             ct_time_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);

static void
ct_time_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_time_get_name;
    interface->get_renderers            = ct_time_get_renderers;
    interface->refresh_data             = ct_time_refresh_data;
    interface->free_data                = ct_time_free_data;
    interface->get_props                = ct_time_get_props;
    interface->get_options_menu         = ct_time_get_options_menu;
    interface->handle_context           = ct_time_handle_context;
    interface->render                   = ct_time_render;
    interface->set_tooltip              = ct_time_set_tooltip;
    interface->node_cmp                 = ct_time_node_cmp;
    interface->is_match_filter          = ct_time_is_match_filter;
    interface->free_filter_data         = ct_time_free_filter_data;
}

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
    DonnaColumnTypeTimePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_TIME,
            DonnaColumnTypeTimePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeTime, donna_column_type_time,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_time_columntype_init)
        )

static void
ct_time_finalize (GObject *object)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = DONNA_COLUMNTYPE_TIME (object)->priv;
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
        DONNA_COLUMNTYPE_TIME (object)->priv->app = g_value_dup_object (value);
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
        g_value_set_object (value, DONNA_COLUMNTYPE_TIME (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_time_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);
    return "time";
}


static const gchar *
ct_time_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);
    return "t";
}

static DonnaColumnTypeNeed
ct_time_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      gpointer           *_data)
{
    DonnaColumnTypeTime *cttime = DONNA_COLUMNTYPE_TIME (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    guint sec;

    config = donna_app_peek_config (cttime->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "property", "mtime");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;

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

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "time", "format", "%O");
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    sec = (guint) donna_config_get_int_column (config, tv_name, col_name, arr_name,
            "time", "age_span_seconds", 7*24*3600);
    if (data->options.age_span_seconds != sec)
    {
        data->options.age_span_seconds = sec;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "time", "age_fallback_fmt", "%F %T");
    if (!streq (data->options.age_fallback_fmt, s))
    {
        g_free ((gchar *) data->options.age_fallback_fmt);
        data->options.age_fallback_fmt = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "property_tooltip", "mtime");
    if (!streq (data->property_tooltip, s))
    {
        g_free (data->property_tooltip);
        data->property_tooltip = s;

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

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "format_tooltip", "%c");
    if (!streq (data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        data->format_tooltip = s;
    }
    else
        g_free (s);

    sec = (guint) donna_config_get_int_column (config, tv_name, col_name, arr_name,
            NULL, "age_span_seconds_tooltip", 7*24*3600);
    if (data->options_tooltip.age_span_seconds != sec)
    {
        data->options_tooltip.age_span_seconds = sec;
    }

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "age_fallback_fmt_tooltip", "%F %T");
    if (!streq (data->options_tooltip.age_fallback_fmt, s))
    {
        g_free ((gchar *) data->options_tooltip.age_fallback_fmt);
        data->options_tooltip.age_fallback_fmt = s;
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
    g_free ((gchar *) data->options.age_fallback_fmt);
    g_free (data->property_tooltip);
    g_free (data->format_tooltip);
    g_free ((gchar *) data->options_tooltip.age_fallback_fmt);
    g_free (data);
}

static GPtrArray *
ct_time_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

static GtkMenu *
ct_time_get_options_menu (DonnaColumnType    *ct,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_time_handle_context (DonnaColumnType    *ct,
                        gpointer            data,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
    /* FIXME */
    return FALSE;
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

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

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
    g_object_set (renderer, "visible", TRUE, "text", s, NULL);
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
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    if (data->format_tooltip[0] == '\0')
        return FALSE;

    if (data->which_tooltip == WHICH_MTIME)
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_ATIME)
        has = donna_node_get_atime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_CTIME)
        has = donna_node_get_ctime (node, FALSE, &time);
    else
        donna_node_get (node, FALSE, data->property_tooltip, &has, &value, NULL);

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

    s = donna_print_time (time, data->format_tooltip, &data->options_tooltip);
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
ct_time_is_match_filter (DonnaColumnType    *ct,
                         const gchar        *filter,
                         gpointer           *filter_data,
                         gpointer            _data,
                         DonnaNode          *node,
                         GError            **error)
{
    struct tv_col_data *data = _data;
    struct filter_data *fd;
    DonnaNodeHasValue has;
    guint64 time;
    GDateTime *dt;
    guint64 r;

    if (G_UNLIKELY (!*filter_data))
    {
        gchar units[] = { UNIT_YEAR, UNIT_MONTH, UNIT_WEEK, UNIT_DAY, UNIT_HOUR,
            UNIT_MINUTE, UNIT_SECOND };
        guint nb_units = sizeof (units) / sizeof (units[0]);
        gchar units_extra[] = { UNIT_DATE, UNIT_DAY_OF_YEAR, UNIT_DAY_OF_WEEK,
            UNIT_DAY_OF_WEEK_2, UNIT_AGE };
        guint nb_units_extra = sizeof (units_extra) / sizeof (units_extra[0]);
        guint i;
        gchar *s;

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
                hour = minute = seconds = 0;

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

            goto compile_done;
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
    }
    else
        fd = *filter_data;

compile_done:

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
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MONTH:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_WEEK:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_week_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_HOUR:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_hour (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MINUTE:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_minute (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_SECOND:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_second (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DATE:
            r = time;
            break;
        case UNIT_DAY_OF_YEAR:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_week (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK_2:
            {
                gchar *s;

                dt = g_date_time_new_from_unix_local (time);
                s = g_date_time_format (dt, "%w");
                g_date_time_unref (dt);
                r = g_ascii_strtoull (s, NULL, 10);
                g_free (s);
                break;
            }
        case UNIT_AGE:
            {
                GDateTime *dt2;

                dt = g_date_time_new_now_local ();
                if (fd->ref == 0)
                    dt2 = g_date_time_ref (dt);
                else
                    switch (fd->unit_age)
                    {
                        case UNIT_YEAR:
                            dt2 = g_date_time_add_years (dt, -1 * fd->ref);
                            break;
                        case UNIT_MONTH:
                            dt2 = g_date_time_add_months (dt, -1 * fd->ref);
                            break;
                        case UNIT_WEEK:
                            dt2 = g_date_time_add_weeks (dt, -1 * fd->ref);
                            break;
                        case UNIT_DAY:
                            dt2 = g_date_time_add_days (dt, -1 * fd->ref);
                            break;
                        case UNIT_HOUR:
                            dt2 = g_date_time_add_hours (dt, -1 * fd->ref);
                            break;
                        case UNIT_MINUTE:
                            dt2 = g_date_time_add_minutes (dt, -1 * fd->ref);
                            break;
                        case UNIT_SECOND:
                            dt2 = g_date_time_add_seconds (dt, -1 * fd->ref);
                            break;
                        case UNIT_DATE:
                        case UNIT_DAY_OF_YEAR:
                        case UNIT_DAY_OF_WEEK:
                        case UNIT_DAY_OF_WEEK_2:
                        case UNIT_AGE:
                            /* silence warnings */
                            g_return_val_if_reached (FALSE);
                    }
                r = g_date_time_to_unix (dt2);
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
                             * age = 2w == 2 weeks ago, i.e. during that week
                             * etc */
                            dt = g_date_time_new_from_unix_local (time);

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
