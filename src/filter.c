/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * filter.c
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

#include <string.h>
#include <ctype.h>      /* isblank() */
#include "filter.h"
#include "filter-private.h"
#include "app.h"
#include "columntype.h"
#include "macros.h"
#include "debug.h"

enum
{
    PROP_0,

    PROP_APP,
    PROP_FILTER,
    PROP_COMPILED,
    PROP_ALIAS,
    PROP_NAME,
    PROP_ICON_NAME,

    NB_PROPS
};

enum cond
{
    COND_AND    = 0,
    COND_OR,
};

struct element
{
    /* condition for the element */
    guint8           cond       : 1;
    /* was it prefixed with NOT */
    guint8           is_not     : 1;
    /* is it a block, or a (group of blocks) */
    guint8           is_block   : 1;
    /* pointer to the block, or the first element in the group */
    gpointer         data;
    /* pointer to the next element */
    struct element  *next;
};

struct block
{
    /* name of column */
    gchar           *col_name;
    /* columntype to use */
    DonnaColumnType *ct;
    /* filter */
    gchar           *filter;
    /* "compiled" data */
    gpointer         data;
};

struct _DonnaFilterPrivate
{
    gchar           *filter;
    DonnaApp        *app;
    gulong           option_set_sid;
    gulong           option_deleted_sid;
    struct element  *element;
    GSList          *col_ct_datas;
    gchar           *alias;
    gchar           *name;
    gchar           *icon_name;
};

static GParamSpec *donna_filter_props[NB_PROPS] = { NULL, };

static void     donna_filter_set_property       (GObject            *object,
                                                 guint               prop_id,
                                                 const GValue       *value,
                                                 GParamSpec         *pspec);
static void     donna_filter_get_property       (GObject            *object,
                                                 guint               prop_id,
                                                 GValue             *value,
                                                 GParamSpec         *pspec);
static void     donna_filter_finalize           (GObject            *object);

G_DEFINE_TYPE (DonnaFilter, donna_filter, G_TYPE_OBJECT)

static void
donna_filter_class_init (DonnaFilterClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property   = donna_filter_set_property;
    o_class->get_property   = donna_filter_get_property;
    o_class->finalize       = donna_filter_finalize;

    donna_filter_props[PROP_APP] =
        g_param_spec_object ("app", "app", "The DonnaApp object",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_filter_props[PROP_FILTER] =
        g_param_spec_string ("filter", "filter", "Filter string",
                NULL,   /* default */
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_filter_props[PROP_COMPILED] =
        g_param_spec_boolean ("compiled", "compiled", "Whether filter is compiled or not",
                FALSE, G_PARAM_READABLE);

    donna_filter_props[PROP_ALIAS] =
        g_param_spec_string ("alias", "alias", "Alias to use the filter",
                NULL,
                G_PARAM_READABLE);

    donna_filter_props[PROP_NAME] =
        g_param_spec_string ("name", "name", "Name/description of the filter",
                NULL,
                G_PARAM_READABLE);

    donna_filter_props[PROP_ICON_NAME] =
        g_param_spec_string ("icon-name", "icon-name", "Icon name associated with the filter",
                NULL,
                G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_filter_props);

    g_type_class_add_private (klass, sizeof (DonnaFilterPrivate));
}

static void
donna_filter_init (DonnaFilter *filter)
{
    filter->priv = G_TYPE_INSTANCE_GET_PRIVATE (filter,
            DONNA_TYPE_FILTER, DonnaFilterPrivate);
}

static void
donna_filter_set_property (GObject            *object,
                           guint               prop_id,
                           const GValue       *value,
                           GParamSpec         *pspec)
{
    DonnaFilterPrivate *priv = ((DonnaFilter *) object)->priv;
    DonnaApp *app;

    switch (prop_id)
    {
        case PROP_APP:
            app = g_value_get_object (value);
            g_return_if_fail (DONNA_IS_APP (app));
            priv->app = g_object_ref (app);
            break;
        case PROP_FILTER:
            priv->filter = g_value_dup_string (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
donna_filter_get_property (GObject            *object,
                           guint               prop_id,
                           GValue             *value,
                           GParamSpec         *pspec)
{
    DonnaFilterPrivate *priv = ((DonnaFilter *) object)->priv;

    switch (prop_id)
    {
        case PROP_APP:
            g_value_set_object (value, priv->app);
            break;
        case PROP_FILTER:
            g_value_set_string (value, priv->filter);
            break;
        case PROP_COMPILED:
            g_value_set_boolean (value, priv->element != NULL);
            break;
        case PROP_ALIAS:
            g_value_set_string (value, priv->alias);
            break;
        case PROP_NAME:
            g_value_set_string (value, priv->name);
            break;
        case PROP_ICON_NAME:
            g_value_set_string (value, priv->icon_name);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
free_block (struct block *block)
{
    if (block->data)
        donna_column_type_free_filter_data (block->ct, block->data);
    g_free (block->filter);
    g_free (block->col_name);
    if (block->ct)
        g_object_unref (block->ct);
    g_slice_free (struct block, block);
}

static void
free_element (struct element *element)
{
    if (!element)
        return;
    for (;;)
    {
        struct element *next;

        next = element->next;
        if (element->data)
        {
            if (element->is_block)
                free_block (element->data);
            else
                free_element (element->data);
        }
        g_slice_free (struct element, element);
        if (!next)
            break;
        else
            element = next;
    }
}

static void
donna_filter_finalize (GObject *object)
{
    DonnaFilterPrivate *priv;
    DonnaConfig *config;

    priv = DONNA_FILTER (object)->priv;
    DONNA_DEBUG (MEMORY, NULL,
            g_debug ("Filter '%s' finalizing", priv->filter));

    config = donna_app_peek_config (priv->app);
    if (priv->option_set_sid > 0)
        g_signal_handler_disconnect (config, priv->option_set_sid);
    if (priv->option_deleted_sid > 0)
        g_signal_handler_disconnect (config, priv->option_deleted_sid);
    g_object_unref (priv->app);
    g_free (priv->filter);
    g_free (priv->alias);
    g_free (priv->name);
    g_free (priv->icon_name);
    free_element (priv->element);

    G_OBJECT_CLASS (donna_filter_parent_class)->finalize (object);
}


/* private API used by provider-filter.c */

gboolean
_donna_filter_has_props (DonnaFilter *filter, guint props)
{
    DonnaFilterPrivate *priv;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    priv = filter->priv;

    if ((props & _DONNA_FILTER_PROP_ALIAS) && priv->alias)
        return TRUE;
    if ((props & _DONNA_FILTER_PROP_NAME) && priv->name)
        return TRUE;
    if ((props & _DONNA_FILTER_PROP_ICON_NAME) && priv->icon_name)
        return TRUE;

    return FALSE;
}

gchar *
_donna_filter_get_key (DonnaFilter *filter)
{
    DonnaFilterPrivate *priv;
    gchar *key;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), NULL);
    priv = filter->priv;

    if (priv->alias)
        key = g_strconcat ("|", priv->alias, NULL);
    else
        key = g_strdup (priv->filter);

    return key;
}

void
_donna_filter_set_alias (DonnaFilter *filter, const gchar *alias, gboolean notify)
{
    DonnaFilterPrivate *priv;
    gchar *s;

    g_return_if_fail (DONNA_IS_FILTER (filter));
    priv = filter->priv;

    s = priv->alias;
    priv->alias = g_strdup (alias);
    g_free (s);

    if (notify)
        g_object_notify_by_pspec ((GObject *) filter, donna_filter_props[PROP_ALIAS]);
}

void
_donna_filter_set_name (DonnaFilter *filter, const gchar *name, gboolean notify)
{
    DonnaFilterPrivate *priv;
    gchar *s;

    g_return_if_fail (DONNA_IS_FILTER (filter));
    priv = filter->priv;

    s = priv->name;
    priv->name = g_strdup (name);
    g_free (s);

    if (notify)
        g_object_notify_by_pspec ((GObject *) filter, donna_filter_props[PROP_NAME]);
}

void
_donna_filter_set_icon_name (DonnaFilter *filter, const gchar *icon_name, gboolean notify)
{
    DonnaFilterPrivate *priv;
    gchar *s;

    g_return_if_fail (DONNA_IS_FILTER (filter));
    priv = filter->priv;

    s = priv->icon_name;
    priv->icon_name = g_strdup (icon_name);
    g_free (s);

    if (notify)
        g_object_notify_by_pspec ((GObject *) filter, donna_filter_props[PROP_ICON_NAME]);
}

static gboolean
element_need_recompile (struct element *element, const gchar *option)
{
    for ( ; element; element = element->next)
    {
        gboolean ret;

        if (element->is_block)
        {
            struct block *block = element->data;
            gchar buf[255], *b = buf;

            if (snprintf (buf, 255, "defaults/lists/columns/%s/type", block->col_name) >= 255)
                b = g_strdup_printf ("defaults/lists/columns/%s/type", block->col_name);
            ret = streq (option, b);
            if (b != buf)
                g_free (b);
        }
        else
            ret = element_need_recompile (element->data, option);

        if (ret)
            return TRUE;
    }

    return FALSE;
}

struct option_data
{
    DonnaFilter *filter;
    gchar *option;
};

static gboolean
real_option_cb (struct option_data *od)
{
    DonnaFilterPrivate *priv = od->filter->priv;

    if (element_need_recompile (priv->element, od->option))
    {
        free_element (priv->element);
        priv->element = NULL;
    }

    g_free (od);
    return G_SOURCE_REMOVE;
}

static void
option_cb (DonnaConfig *config, const gchar *option, DonnaFilter *filter)
{
    struct option_data *od;

    if (!streqn (option, "defaults/lists/columns/",
                strlen ("defaults/lists/columns/")))
        return;

    od = g_new (struct option_data, 1);
    od->filter = filter;
    od->option = g_strdup (option);

    g_main_context_invoke (NULL, (GSourceFunc) real_option_cb, od);
}

static inline DonnaColumnType *
get_ct (DonnaFilter *filter, const gchar *col_name)
{
    DonnaFilterPrivate *priv = filter->priv;
    DonnaConfig *config;
    DonnaColumnType *ct;
    gchar *type = NULL;

    config = donna_app_peek_config (priv->app);

    if (priv->option_set_sid == 0)
        priv->option_set_sid = g_signal_connect (config, "option-set",
                (GCallback) option_cb, filter);
    if (priv->option_deleted_sid == 0)
        priv->option_deleted_sid = g_signal_connect (config, "option-deleted",
                (GCallback) option_cb, filter);

    if (donna_config_get_string (config, NULL, &type,
                "defaults/lists/columns/%s/type", col_name))
        ct = donna_app_get_column_type (priv->app, type);
    else
        /* fallback to its name */
        ct = donna_app_get_column_type (priv->app, col_name);

    g_free (type);
    return ct;
}

static gchar *
get_quoted_string (gchar **str, gboolean get_string)
{
    GString *string;
    gchar *start = *str;
    gchar *end;

    if (get_string)
        string = g_string_new (NULL);

    for (end = ++start; ; ++end)
    {
        if (*end == '\\')
        {
            ++end;
            if (get_string)
                g_string_append_c (string, *end);
            continue;
        }
        if (*end == '"')
            break;
        else if (*end == '\0')
        {
            if (get_string)
                g_string_free (string, TRUE);
            return NULL;
        }
        if (get_string)
            g_string_append_c (string, *end);
    }
    *str = end + 1;
    return (get_string) ? g_string_free (string, FALSE) : *str;
}

static struct block *
parse_block (DonnaFilter *filter, gchar **str, GError **error)
{
    struct block *block;
    gchar *f = *str;
    gchar *s;

    block = g_slice_new0 (struct block);
    skip_blank (f);
    /* get columntype */
    s = strchr (f, ':');
    if (!s)
    {
        /* this means this is just a filter for name */
        block->col_name = g_strdup ("name");
        block->ct = get_ct (filter, block->col_name);
        if (!block->ct)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_COLUMN_TYPE,
                    "Unable to load column type for 'name'");
            free_block (block);
            *str = f;
            return NULL;
        }
    }
    else
    {
        *s = '\0';
        block->col_name = g_strdup (f);
        block->ct = get_ct (filter, block->col_name);
        if (!block->ct)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_COLUMN_TYPE,
                    "Unable to load column type for '%s'",
                    block->col_name);
            *s = ':';
            free_block (block);
            *str = f;
            return NULL;
        }
        *s = ':';
        /* move to the actual filter */
        f = s + 1;
    }

    /* get filter (can only be quoted if column name was specified) */
    if (s && f[0] == '"')
    {
        block->filter = get_quoted_string (&f, TRUE);
        if (!block->filter)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_SYNTAX,
                    "Missing closing quote: %s", f);
            free_block (block);
            *str = f;
            return NULL;
        }
    }
    else
    {
        block->filter = g_strdup (f);
        /* i.e. nothing after */
        f = NULL;
    }

    *str = f;
    return block;
}

static struct element *
parse_element (DonnaFilter *filter, gchar **str, GError **error)
{
    struct element *first_element = NULL;
    struct element *last_element = NULL;
    struct element *element;
    gchar *f = *str;

    for (;;)
    {
        element = g_slice_new0 (struct element);
        if (last_element)
        {
            if ((f[0] == 'a' || f[0] == 'A') && (f[1] == 'n' || f[1] == 'N')
                    && (f[2] == 'd' || f[2] == 'D')
                    && (f[3] == '(' || isblank (f[3])))
            {
                element->cond = COND_AND;
                f += 3;
            }
            else if ((f[0] == 'o' || f[0] == 'O')
                    && (f[1] == 'r' || f[1] == 'R')
                    && (f[2] == '(' || isblank (f[2])))
            {
                element->cond = COND_OR;
                f += 2;
            }
            else
            {
                g_set_error (error, DONNA_FILTER_ERROR,
                        DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Expected 'AND' or 'OR': %s", f);
                free_element (element);
                goto undo;
            }
        }
        else
            /* first element must be AND */
            element->cond = COND_AND;

        skip_blank (f);
        if ((f[0] == 'N' || f[0] == 'n') && (f[1] == 'O' || f[1] == 'o')
                && (f[2] == 'T' || f[2] == 't')
                && (f[3] == '(' || isblank (f[3])))
        {
            element->is_not = TRUE;
            f += 3;
        }

        skip_blank (f);
        if (*f == '(')
        {
            /* remember beginning, as f will move to the end */
            gchar *s = ++f;
            /* parenthesis contained within */
            gint c = 0;

            /* find closing parenthesis */
            for ( ; ; ++f)
            {
                if (*f == '\0')
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Missing closing parenthesis: %s", *str);
                    free_element (element);
                    goto undo;
                }
                else if (*f == '"')
                {
                    /* FALSE: simply move f past the closing parenthesis */
                    if (!get_quoted_string (&f, FALSE))
                    {
                        g_set_error (error, DONNA_FILTER_ERROR,
                                DONNA_FILTER_ERROR_INVALID_SYNTAX,
                                "Missing closing quote: %s", f);
                        free_element (element);
                        goto undo;
                    }
                    /* f was moved after the closing quote, move it back so when
                     * moving forward in the loop we don't skip anything */
                    --f;
                }
                else if (*f == '(')
                    ++c;
                else if (*f == ')')
                {
                    if (c > 0)
                        --c;
                    else
                        break;
                }
            }
            /* parse the string within parenthesis */
            *f = '\0';
            element->data = parse_element (filter, &s, error);
            *f = ')';
            if (s)
                f = s + 1;
            else
                ++f;
        }
        else
        {
            element->is_block = TRUE;
            element->data = parse_block (filter, &f, error);
        }
        if (!element->data)
        {
            free_element (element);
            goto undo;
        }

        if (last_element)
            last_element->next = element;
        else
            first_element = element;
        last_element = element;

        if (!f)
            break;
        skip_blank (f);
        if (*f == '\0')
            break;
    }

    *str = f;
    return first_element;

undo:
    free_element (first_element);
    *str = f;
    return NULL;
}

static gboolean
compile_element (struct element *element, GError **error)
{
    for ( ; element; element = element->next)
    {
        if (element->is_block)
        {
            struct block *block = element->data;

            if (!donna_column_type_refresh_filter_data (block->ct,
                        block->filter, &block->data, error))
            {
                block->data = NULL;
                return FALSE;
            }
        }
        else if (!compile_element (element->data, error))
            return FALSE;
    }

    return TRUE;
}

gboolean
donna_filter_compile (DonnaFilter    *filter,
                      GError        **error)
{
    DonnaFilterPrivate *priv;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    priv = filter->priv;

    /* if needed, compile the filter into elements */
    if (!priv->element)
    {
        gchar *f = priv->filter;
        priv->element = parse_element (filter, &f, error);
        if (!priv->element)
            return FALSE;
    }

    if (!compile_element (priv->element, error))
    {
        free_element (priv->element);
        priv->element = NULL;
        g_prefix_error (error, "Failed to compile filter: ");
        return FALSE;
    }

    return TRUE;
}

gboolean
donna_filter_is_compiled (DonnaFilter    *filter)
{
    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    return filter->priv->element != NULL;
}

static gboolean
_get_ct_data (const gchar   *col_name,
              DonnaNode     *node,
              gpointer      *ctdata,
              DonnaFilter   *filter)
{
    DonnaFilterPrivate *priv = filter->priv;
    struct col_ct_data *ccd;
    GSList *l;

    /* we might already have the col_ct_data for this column */
    for (l = priv->col_ct_datas; l; l = l->next)
    {
        ccd = l->data;
        if (streq (ccd->col_name, col_name))
            break;
    }

    if (!l)
    {
        ccd = _donna_app_get_col_ct_data (priv->app, col_name);
        priv->col_ct_datas = g_slist_append (priv->col_ct_datas, ccd);
    }

    /* we have the list of properties for the column only if said column is
     * RP_ON_DEMAND, i.e. we need to check if we can filter or not */
    if (ccd->props)
    {
        guint i;

        for (i = 0; i < ccd->props->len; ++i)
        {
            DonnaNodeHasProp has;

            has = donna_node_has_property (node, (gchar *) ccd->props->pdata[i]);
            if ((has & DONNA_NODE_PROP_EXISTS) && !(has & DONNA_NODE_PROP_HAS_VALUE))
                return FALSE;
        }
    }

    *ctdata = ccd->ct_data;
    return TRUE;
}

static gboolean
is_match_element (struct element    *element,
                  DonnaNode         *node,
                  get_ct_data_fn     get_ct_data,
                  gpointer           data)
{
    gboolean match = TRUE;

    for ( ; element; element = element->next)
    {
        if (match && element->cond == COND_OR)
            break;
        if (!match && element->cond == COND_AND)
            break;

        if (element->is_block)
        {
            struct block *block = element->data;
            gpointer ctdata;

            if (get_ct_data (block->col_name, node, &ctdata, data))
                match = donna_column_type_is_filter_match (block->ct,
                        ctdata, block->data, node);
            else
                match = FALSE;
        }
        else
            match = is_match_element (element->data, node, get_ct_data, data);

        if (element->is_not)
            match = !match;
    }
    return match;
}

gboolean
donna_filter_is_match (DonnaFilter    *filter,
                       DonnaNode      *node,
                       DonnaTreeView  *treeview)
{
    DonnaFilterPrivate *priv;
    get_ct_data_fn get_ct_data;
    gpointer data;
    gboolean match;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (!treeview || DONNA_IS_TREE_VIEW (treeview), FALSE);
    priv = filter->priv;

    /* if needed, compile the filter into elements */
    if (!priv->element)
    {
        GError *err = NULL;

        if (!donna_filter_compile (filter, &err))
        {
            g_warning ("Filter wasn't compiled, and compilation failed: %s",
                    err->message);
            g_clear_error (&err);
            return FALSE;
        }
    }

    if (treeview)
    {
        get_ct_data = (get_ct_data_fn) _donna_tree_view_get_ct_data;
        data = treeview;
    }
    else
    {
        get_ct_data = (get_ct_data_fn) _get_ct_data;
        data = filter;
    }

    /* see if node matches the filter */
    match = is_match_element (priv->element, node, get_ct_data, data);

    /* if not going through a treeview, we might have col_ct_data to unref */
    if (priv->col_ct_datas)
    {
        GSList *l;

        for (l = priv->col_ct_datas; l; l = l ->next)
            _donna_app_unref_col_ct_data (priv->app, l->data);
        g_slist_free (priv->col_ct_datas);
        priv->col_ct_datas = NULL;
    }

    return match;
}

/* this is needed for filter_toggle_ref_cb() in provider-filter.c where we need
 * to get the filter string, but can't use g_object_get() as it would take a ref
 * on it, thus triggering the toggle_ref and enterring an infinite recursion...
 * */
gchar *
donna_filter_get_filter (DonnaFilter *filter)
{
    g_return_val_if_fail (DONNA_IS_FILTER (filter), NULL);
    return g_strdup (filter->priv->filter);
}

DonnaApp *
_donna_filter_peek_app (DonnaFilter *filter)
{
    g_return_val_if_fail (DONNA_IS_FILTER (filter), NULL);
    return filter->priv->app;
}
