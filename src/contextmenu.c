/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * contextmenu.c
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

#include "contextmenu.h"
#include "app.h"
#include "provider-internal.h"
#include "node.h"
#include "util.h"
#include "macros.h"
#include "imagemenuitem.h"  /* DonnaImageMenuItemImageSpecial */
#include "debug.h"

enum type
{
    TYPE_STANDARD = 0,
    TYPE_TRIGGER,
    TYPE_CONTAINER,
    TYPE_EMPTY,
    NB_TYPES
};

enum expr
{
    EXPR_INVALID,
    EXPR_TRUE,
    EXPR_FALSE
};

enum op
{
    OP_AND  = (1 << 0),
    OP_OR   = (1 << 1),
    OP_NOT  = (1 << 2),
};

struct node_internal
{
    DonnaApp    *app;
    DonnaNode   *node_trigger;
    GPtrArray   *intrefs;
    gchar       *fl;
    gboolean     free_fl;
};

/* internal, used from app.c */
gboolean _donna_context_register_extras (DonnaConfig *config, GError **error);

gboolean
_donna_context_register_extras (DonnaConfig *config, GError **error)
{
    DonnaConfigItemExtraListInt it[4];
    gint i;

    i = 0;
    it[i].value     = TYPE_STANDARD;
    it[i].in_file   = "standard";
    it[i].label     = "Standard item";
    ++i;
    it[i].value     = TYPE_TRIGGER;
    it[i].in_file   = "trigger";
    it[i].label     = "Use trigger node";
    ++i;
    it[i].value     = TYPE_CONTAINER;
    it[i].in_file   = "container";
    it[i].label     = "Container item";
    ++i;
    it[i].value     = TYPE_EMPTY;
    it[i].in_file   = "empty";
    it[i].label     = "Empty item (no trigger)";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "context-type",
                    "Type of context item",
                    i, it, error)))
        return FALSE;

    return TRUE;
}

static inline void
free_intrefs (DonnaApp *app, GPtrArray *intrefs)
{
    if (intrefs)
    {
        guint i;

        for (i = 0; i < intrefs->len; ++i)
            donna_app_free_int_ref (app, intrefs->pdata[i]);
        g_ptr_array_unref (intrefs);
    }
}

static void
free_node_internal (struct node_internal *ni)
{
    if (ni->node_trigger)
        g_object_unref (ni->node_trigger);
    free_intrefs (ni->app, ni->intrefs);
    if (ni->free_fl)
        g_free (ni->fl);

    g_slice_free (struct node_internal, ni);
}

static enum expr
evaluate (DonnaContextReference reference, gchar *expr, GError **error)
{
    enum op op = 0;
    enum expr subexpr;
    guint c = 0;
    gchar *e;

    for (;;)
    {
        skip_blank (expr);

        if (strcaseeqn (expr, "not", 3) && (expr[3] == '(' || isblank (expr[3])))
        {
            op |= OP_NOT;
            expr += 3;
            skip_blank (expr);
        }

        if (*expr == '(')
        {
            e = expr + 1;
            for (;;)
            {
                if (*e == '\0')
                {
                    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                            DONNA_CONTEXT_MENU_ERROR_OTHER,
                            "Invalid expression, missing closing parenthesis: %s",
                            expr);
                    return EXPR_INVALID;
                }
                else if (*e == '(')
                    ++c;
                else if (*e == ')')
                {
                    if (c > 0)
                        --c;
                    else
                        break;
                }
                ++e;
            }

            *e = '\0';
            subexpr = evaluate (reference, expr + 1, error);
            *e = ')';
            expr = e + 1;

            if (subexpr == EXPR_INVALID)
                return EXPR_INVALID;
        }
        else
        {
            /* 12 = strlen ("ref_selected") */
            if (strcaseeqn (expr, "ref_selected", 12)
                    && (expr[12] == '\0' || isblank (expr[12])))
            {
                if (reference & DONNA_CONTEXT_REF_SELECTED)
                    subexpr = EXPR_TRUE;
                else
                    subexpr = EXPR_FALSE;

                expr += 12;
            }
            /* 16 = strlen ("ref_not_selected") */
            else if (strcaseeqn (expr, "ref_not_selected", 16)
                    && (expr[16] == '\0' || isblank (expr[16])))
            {
                if (reference & DONNA_CONTEXT_REF_NOT_SELECTED)
                    subexpr = EXPR_TRUE;
                else
                    subexpr = EXPR_FALSE;

                expr += 16;
            }
            /* 3 = strlen ("ref") */
            else if (strcaseeqn (expr, "has_ref", 7)
                    && (expr[7] == '\0' || isblank (expr[7])))
            {
                if (reference & DONNA_CONTEXT_HAS_REF)
                    subexpr = EXPR_TRUE;
                else
                    subexpr = EXPR_FALSE;

                expr += 7;
            }
            /* 9 = strlen ("selection") */
            else if (strcaseeqn (expr, "selection", 9)
                    && (expr[9] == '\0' || isblank (expr[9])))
            {
                if (reference & DONNA_CONTEXT_HAS_SELECTION)
                    subexpr = EXPR_TRUE;
                else
                    subexpr = EXPR_FALSE;

                expr += 9;
            }
            else if (strcaseeqn (expr, "false", 5)
                    && (expr[5] == '\0' || isblank (expr[5])))
            {
                subexpr = EXPR_FALSE;
                expr += 5;
            }
            else if (strcaseeqn (expr, "true", 4)
                    && (expr[4] == '\0' || isblank (expr[4])))
            {
                subexpr = EXPR_TRUE;
                expr += 4;
            }
            else
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_OTHER,
                        "Invalid expression, expected 'REF_SELECTED', "
                        "'REF_NOT_SELECTED', 'HAS_REF' or 'SELECTION': %s",
                        expr);
                return EXPR_INVALID;
            }
        }

        /* reverse it if this was NOT-d */
        if (op & OP_NOT)
            subexpr = (subexpr == EXPR_TRUE) ? EXPR_FALSE : EXPR_TRUE;

        /* case where we already have an answer */
        if ((op & OP_AND) && subexpr == EXPR_FALSE)
            return EXPR_FALSE;
        if ((op & OP_OR) && subexpr == EXPR_TRUE)
            return EXPR_TRUE;

        /* figure out the next op (if any) */
        skip_blank (expr);
        if (*expr == '\0')
            return subexpr;

        if (strcaseeqn (expr, "and", 3) && isblank (expr[3]))
        {
            if (subexpr == EXPR_FALSE)
                return EXPR_FALSE;
            op = OP_AND;
            expr += 3;
        }
        else if (strcaseeqn (expr, "or", 2) && isblank (expr[2]))
        {
            if (subexpr == EXPR_TRUE)
                return EXPR_TRUE;
            op = OP_OR;
            expr += 2;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Invalid expression, expected 'AND' or 'OR': %s",
                    expr);
            return EXPR_INVALID;
        }
    }
}

struct err
{
    DonnaApp *app;
    GError *err;
};

static gboolean
show_error (struct err *e)
{
    donna_app_show_error (e->app, e->err, "Failed to trigger node");
    g_clear_error (&e->err);
    g_free (e);
    return FALSE;
}

static void
trigger_node_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (app, donna_task_get_error (task),
                "Failed to trigger node");
}

static DonnaTaskState
node_internal_cb (DonnaTask *task, DonnaNode *node, struct node_internal *ni)
{
    GError *err = NULL;
    DonnaTask *trigger_task;

    if (ni->node_trigger)
    {
        trigger_task = donna_node_trigger_task (ni->node_trigger, &err);
        if (!trigger_task)
        {
            gchar *fl = donna_node_get_full_location (ni->node_trigger);
            g_prefix_error (&err, "Cannot trigger node: "
                    "Failed to get trigger task for '%s'", fl);
            g_free (fl);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        donna_task_set_callback (trigger_task, (task_callback_fn) trigger_node_cb,
                ni->app, NULL);
        donna_app_run_task (ni->app, trigger_task);
    }
    else
    {
        if (!donna_app_trigger_fl (ni->app, ni->fl, ni->intrefs, FALSE, &err))
        {
            struct err *e;

            e = g_new (struct err, 1);
            e->app = ni->app;
            e->err = err;
            g_main_context_invoke (NULL, (GSourceFunc) show_error, e);
        }
        else
            /* because trigger_fl handled them for us */
            ni->intrefs = NULL;
    }

    free_node_internal (ni);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
node_children_cb (DonnaTask             *task,
                  DonnaNode             *node,
                  DonnaNodeType          node_types,
                  gboolean               get_children,
                  struct node_internal  *ni)
{
    GError *err = NULL;
    DonnaTask *t;
    DonnaTaskState state;
    GValue *value;

    if (get_children)
        t = donna_node_get_children_task (ni->node_trigger, node_types, &err);
    else
        t = donna_node_has_children_task (ni->node_trigger, node_types, &err);
    if (G_UNLIKELY (!t))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (ni->app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Failed to run %s_children task: ",
                (get_children) ? "get" : "has");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        if (state == DONNA_TASK_FAILED)
            donna_task_take_error (task, g_error_copy (donna_task_get_error (t)));
        g_object_unref (t);
        return state;
    }

    value = donna_task_grab_return_value (task);
    if (get_children)
    {
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_set_boxed (value, g_value_get_boxed (donna_task_get_return_value (t)));
    }
    else
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, g_value_get_boolean (donna_task_get_return_value (t)));
    }
    donna_task_release_return_value (task);

    g_object_unref (t);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
container_children_cb (DonnaTask    *task,
                       DonnaNode    *node,
                       DonnaNodeType node_types,
                       gboolean      get_children,
                       GPtrArray    *children)
{
    GValue *value;
    gboolean has_children = FALSE;
    GPtrArray *arr = NULL;
    guint i;

    if (children->len == 0
            || ((node_types & (DONNA_NODE_ITEM | DONNA_NODE_CONTAINER))
                == (DONNA_NODE_ITEM | DONNA_NODE_CONTAINER)))
    {
        value = donna_task_grab_return_value (task);
        if (get_children)
        {
            g_value_init (value, G_TYPE_PTR_ARRAY);
            g_value_set_boxed (value, children);
        }
        else
        {
            g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, children->len > 0);
        }
        donna_task_release_return_value (task);

        return DONNA_TASK_DONE;
    }

    if (get_children)
        arr = g_ptr_array_new_with_free_func (g_object_unref);
    for (i = 0; i < children->len; ++i)
    {
        if (donna_node_get_node_type (children->pdata[i]) & node_types)
        {
            if (get_children)
                g_ptr_array_add (arr, g_object_ref (children->pdata[i]));
            else
            {
                has_children = TRUE;
                break;
            }
        }
    }

    value = donna_task_grab_return_value (task);
    if (get_children)
    {
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_set_boxed (value, arr);
    }
    else
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, has_children);
    }
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaNode *
get_node_trigger (DonnaApp      *app,
                  const gchar   *_fl,
                  DonnaContext  *context,
                  GError       **error)
{
    DonnaNode *node;
    gchar *fl;

    fl = donna_app_parse_fl (app, (gchar *) _fl, FALSE, context, NULL);
    node = donna_app_get_node (app, fl, FALSE, error);
    if (!node)
    {
        g_free (fl);
        return NULL;
    }

    g_free (fl);
    return node;
}

static gchar *
parse_Cc (gchar *_sce, const gchar *s_C, const gchar *s_c)
{
    GString *str = NULL;
    gchar *sce = _sce;
    gchar *s = sce;

    while ((s = strchr (s, '%')))
    {
        if (s[1] == 'C' || s[1] == 'c')
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, sce, s - sce);

            g_string_append (str, (s[1] == 'C') ? s_C : s_c);

            s += 2;
            sce = s;
        }
        else
            ++s;
    }

    if (!str)
        return sce;

    g_string_append (str, sce);
    g_free (_sce);
    return g_string_free (str, FALSE);
}

static void
free_context_info (DonnaContextInfo *info)
{
    if (info->node)
        g_object_unref (info->node);
    if (info->free_name)
        g_free ((gchar *) info->name);
    if (info->free_icon)
    {
        if (info->icon_is_gicon)
            g_object_unref (info->icon);
        else
            g_free ((gchar *) info->icon_name);
    }
    if (info->free_icon_selected)
    {
        if (info->icon_is_gicon_selected)
            g_object_unref (info->icon_selected);
        else
            g_free ((gchar *) info->icon_name_selected);
    }
    if (info->free_desc)
        g_free ((gchar *) info->desc);
    if (info->free_trigger)
        g_free ((gchar *) info->trigger);
    if (info->free_menu)
        g_free ((gchar *) info->menu);

    if (info->new_node_destroy && info->new_node_data)
        info->new_node_destroy (info->new_node_data);
}

static inline gchar *
get_user_alias (const gchar *alias,
                const gchar *extra,
                DonnaApp    *app,
                const gchar *source,
                GError     **error)
{
    GError *err = NULL;
    gchar *s;

    if (!donna_config_get_string (donna_app_peek_config (app), &err, &s,
                "context_menus/%s/aliases/%s", source, alias))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "Unknown user alias '%s' for '%s': %s",
                alias, source, (err) ? err->message : "no error message");
        g_clear_error (&err);
        return NULL;
    }

    if (extra)
    {
        gchar *ss;
        ss = strchr (extra, '=');
        if (ss)
        {
            *ss = '\0';
            s = parse_Cc (s, extra, ss + 1);
        }
        else
            s = parse_Cc (s, extra, "");
    }
    else
        s = parse_Cc (s, "", "");

    return s;
}

enum
{
    /* DEFAULT is anything that can be determined based on their existence in
     * info. E.g. if image-selected is set, there will be a name/pixbuf, if not
     * it'll be NULL. */
    IMPORT_DEFAULT              = (1 << 0),
    IMPORT_ICON_SPECIAL         = (1 << 1),
    IMPORT_IS_ACTIVE            = (1 << 2),
    IMPORT_IS_INCONSISTENT      = (1 << 3),
    IMPORT_IS_SENSITIVE         = (1 << 4),
    IMPORT_IS_LABEL_BOLD        = (1 << 5),
    IMPORT_SUBEMNUS             = (1 << 6),
    IMPORT_ALL                  = IMPORT_DEFAULT | IMPORT_ICON_SPECIAL
        | IMPORT_IS_ACTIVE | IMPORT_IS_INCONSISTENT | IMPORT_IS_SENSITIVE
        | IMPORT_IS_LABEL_BOLD | IMPORT_SUBEMNUS
};

static void
import_info_from_node (DonnaNode *node, guint import, DonnaContextInfo *info)
{
    GValue v = G_VALUE_INIT;
    DonnaNodeHasValue has;

    info->is_container = donna_node_get_node_type (node) == DONNA_NODE_CONTAINER;

    if ((import & IMPORT_DEFAULT) && !info->name)
    {
        info->name = donna_node_get_name (node);
        info->free_name = TRUE;
    }

    if ((import & IMPORT_DEFAULT) && !info->icon
            && donna_node_get_icon (node, FALSE, &info->icon) == DONNA_NODE_VALUE_SET)
    {
        info->icon_is_gicon = TRUE;
        info->free_icon = TRUE;
    }

    if ((import & IMPORT_DEFAULT) && !info->desc
            && donna_node_get_desc (node, FALSE,
                (gchar **) &info->desc) == DONNA_NODE_VALUE_SET)
        info->free_desc = TRUE;

    if (import & IMPORT_ICON_SPECIAL)
    {
        donna_node_get (node, FALSE, "menu-image-special", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            guint icon_special = g_value_get_uint (&v);
            info->icon_special = MIN (icon_special, 2);
            g_value_unset (&v);
        }
    }

    if (import & IMPORT_IS_ACTIVE)
    {
        donna_node_get (node, FALSE, "menu-is-active", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->is_active = g_value_get_boolean (&v);
            g_value_unset (&v);
        }
    }

    if (import & IMPORT_IS_INCONSISTENT)
    {
        donna_node_get (node, FALSE, "menu-is-inconsistent", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->is_inconsistent = g_value_get_boolean (&v);
            g_value_unset (&v);
        }
    }

    if ((import & IMPORT_DEFAULT) && !info->icon_selected)
    {
        donna_node_get (node, FALSE, "menu-image-selected", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->icon_selected = g_value_dup_object (&v);
            g_value_unset (&v);
            info->icon_is_gicon_selected = TRUE;
            info->free_icon_selected = TRUE;
        }
    }

    if (import & IMPORT_IS_SENSITIVE)
    {
        donna_node_get (node, FALSE, "menu-is-sensitive", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->is_sensitive = g_value_get_boolean (&v);
            g_value_unset (&v);
        }
    }

    if (import & IMPORT_IS_LABEL_BOLD)
    {
        donna_node_get (node, FALSE, "menu-is-label-bold", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->is_menu_bold = g_value_get_boolean (&v);
            g_value_unset (&v);
        }
    }

    if (import & IMPORT_SUBEMNUS)
    {
        donna_node_get (node, FALSE, "menu-submenus", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            guint submenus = g_value_get_uint (&v);
            info->submenus = MIN (submenus, 3);
            g_value_unset (&v);
        }
    }

    if ((import & IMPORT_DEFAULT) && !info->menu)
    {
        donna_node_get (node, FALSE, "menu-menu", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->menu = g_value_dup_string (&v);
            info->free_menu = TRUE;
            g_value_unset (&v);
        }
    }
}

static gboolean
get_user_item_info (const gchar             *item,
                    const gchar             *extra,
                    DonnaContextReference    reference,
                    DonnaApp                *app,
                    const gchar             *source,
                    DonnaContext            *context,
                    DonnaContextInfo        *info,
                    GError                 **error)
{
    GError *err = NULL;
    DonnaConfig *config = donna_app_peek_config (app);
    DonnaNode *node_trigger = NULL;
    GPtrArray *triggers = NULL;
    enum type type;
    gboolean import_from_trigger = FALSE;
    guint import = 0;
    gboolean b;
    const gchar *s_c = NULL;
    const gchar *s_C = NULL;
    gchar *s;

    if (!donna_config_has_category (config, &err, "context_menus/%s/%s",
                source, item))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "Unknown user item '%s' for '%s': %s",
                item, source, (err) ? err->message : "no error message");
        g_clear_error (&err);
        return FALSE;
    }

    if (donna_config_get_string (config, NULL, &s,
                "context_menus/%s/%s/is_visible",
                source, item))
    {
        enum expr expr;

        expr = evaluate (reference, s, error);
        if (expr == EXPR_INVALID)
        {
            g_prefix_error (error,
                    "Failed to evaluate 'context_menus/%s/%s/is_visible': ",
                    source, item);
            g_free (s);
            return FALSE;
        }
        info->is_visible = expr == EXPR_TRUE;
        g_free (s);
    }
    else
        info->is_visible = TRUE;

    if (donna_config_get_string (config, NULL, &s,
                "context_menus/%s/%s/is_sensitive",
                source, item))
    {
        enum expr expr;

        expr = evaluate (reference, s, error);
        if (expr == EXPR_INVALID)
        {
            g_prefix_error (error,
                    "Failed to evaluate 'context_menus/%s/%s/is_sensitive': ",
                    source, item);
            g_free (s);
            return FALSE;
        }
        info->is_sensitive = expr == EXPR_TRUE;
        g_free (s);
    }
    else
        info->is_sensitive = TRUE;

    /* there can be a var=name specified after a ':' */
    if (extra)
    {
        s_C = extra;
        s = strchr (s_C, '=');
        if (s)
        {
            *s = '\0';
            s_c = s + 1;
        }
    }

    /* type of item */
    if (donna_config_get_int (config, NULL, (gint *) &type,
                "context_menus/%s/%s/type", source, item))
        type = CLAMP (type, TYPE_STANDARD, NB_TYPES - 1);
    else
        type = TYPE_STANDARD;

    if (type == TYPE_CONTAINER)
        info->is_container = TRUE;

    /* shall we import non-specified stuff from node trigger? */
    if (type != TYPE_EMPTY)
        donna_config_get_boolean (config, NULL, &import_from_trigger,
                "context_menus/%s/%s/import_from_trigger",
                source, item);

    /* find the (matching) trigger */
    if (donna_config_list_options (config, &triggers,
                DONNA_CONFIG_OPTION_TYPE_OPTION, "context_menus/%s/%s",
                source, item))
    {
        guint j;

        for (j = 0; j < triggers->len; ++j)
        {
            gchar *t = triggers->pdata[j];
            gsize len;

            /* must start with "trigger", ignoring "trigger" itself */
            if (!streqn ("trigger", t, 7) || t[7] == '\0')
                continue;
            len = strlen (t);
            /* 13 == strlen ("trigger") + strlen ("_when") + 1 */
            if (len < 13 || !streq (t + len - 5, "_when"))
                continue;
            /* get the triggerXXX_when expr to see if it's a match */
            if (donna_config_get_string (config, NULL, &s,
                        "context_menus/%s/%s/%s",
                        source, item, t))
            {
                enum expr expr;

                expr = evaluate (reference, s, &err);
                if (expr == EXPR_INVALID)
                {
                    g_warning ("Context-menu: Skipping trigger declaration, "
                            "invalid expression in 'context_menus/%s/%s/%s': %s",
                            source, item, t,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                    g_free (s);
                    continue;
                }
                else if (expr == EXPR_TRUE)
                {
                    /* get the actual trigger */
                    if (!donna_config_get_string (config, NULL,
                                (gchar **) &info->trigger,
                                "context_menus/%s/%s/%.*s",
                                source, item, (gint) (len - 5), t))
                    {
                        g_warning ("Context-menu: Trigger option missing: "
                                "'context_menus/%s/%s/%s' -- Skipping trigger",
                                source, item, t);
                        g_free (s);
                        continue;
                    }
                    g_free (s);

                    /* try to get the name under the same suffix */
                    if (donna_config_get_string (config, NULL,
                                (gchar **) &info->name,
                                "context_menus/%s/%s/name%.*s",
                                source, item, (gint) (len - 7 - 5), t + 7))
                        info->free_name = TRUE;

                    /* try to get the icon under the same suffix */
                    if (donna_config_get_string (config, NULL,
                                (gchar **) &info->icon_name,
                                "context_menus/%s/%s/icon%.*s",
                                source, item, (gint) (len - 7 - 5), t + 7))
                        info->free_icon = TRUE;

                    /* try to get the icon selected under the same suffix */
                    if (donna_config_get_string (config, NULL,
                                (gchar **) &info->icon_name_selected,
                                "context_menus/%s/%s/icon_selected%.*s",
                                source, item, (gint) (len - 7 - 5), t + 7))
                        info->free_icon_selected = TRUE;

                    break;
                }
                else /* EXPR_FALSE */
                    g_free (s);
            }
        }
        g_ptr_array_unref (triggers);
    }

    /* last chance: the default "trigger" */
    if (!info->trigger && !donna_config_get_string (config, NULL,
                (gchar **) &info->trigger,
                "context_menus/%s/%s/trigger", source, item))
    {
        if ((!(info->is_visible && info->is_sensitive) && !import_from_trigger)
            || type == TYPE_EMPTY)
        {
            /* not visible & sensitive, and don't import info from node trigger;
             * or TYPE_EMPTY (i.e. w/out trigger) then it's ok to not have a
             * trigger */
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "No trigger found for 'context_menus/%s/%s'",
                    source, item);
            return FALSE;
        }
    }

    if (info->trigger)
    {
        /* parse %C/%c in the trigger */
        info->trigger = parse_Cc ((gchar *) info->trigger, s_C, s_c);
        info->free_trigger = TRUE;
    }

    if (type == TYPE_TRIGGER)
    {
        info->node = get_node_trigger (app, info->trigger, context, error);
        g_free ((gchar *) info->trigger);
        info->trigger = NULL;
        info->free_trigger = FALSE;

        if (!info->node)
        {
            g_prefix_error (error,
                    "Failed to get node for item 'context_menus/%s/%s': ",
                    source, item);
            return FALSE;
        }

        return TRUE;
    }

    /* is_sensitive is a bit special, in that we only import from trigger if
     * TRUE (since if FALSE, that takes precedence, but if TRUE the value from
     * trigger does) */
    if (info->is_sensitive && import_from_trigger)
        import |= IMPORT_IS_SENSITIVE;

    /* name */
    if (!info->name)
    {
        if (donna_config_get_string (config, NULL, (gchar **) &info->name,
                    "context_menus/%s/%s/name", source, item))
            info->free_name = TRUE;
        else if (import_from_trigger)
            import |= IMPORT_DEFAULT;
    }

    /* icon */
    if (!info->icon_name)
    {
        if (donna_config_get_string (config, NULL, (gchar **) &info->icon_name,
                    "context_menus/%s/%s/icon", source, item))
            info->free_icon = TRUE;
        else if (import_from_trigger)
            import |= IMPORT_DEFAULT;
    }

    /* icon selected */
    if (!info->icon_name_selected)
    {
        if (donna_config_get_string (config, NULL, (gchar **) &info->icon_name_selected,
                    "context_menus/%s/%s/icon_selected", source, item))
            info->free_icon_selected = TRUE;
        else if (import_from_trigger)
            import |= IMPORT_DEFAULT;
    }

    if (donna_config_get_boolean (config, NULL, &b,
                "context_menus/%s/%s/menu_is_label_bold",
                source, item))
        info->is_menu_bold = b;
    else if (import_from_trigger)
        import |= IMPORT_IS_LABEL_BOLD;

    /* TYPE_EMPTY means we force to submenus ENABLED, since it's meant to define
     * the parent of a submenu */
    if (type == TYPE_EMPTY)
        info->submenus = DONNA_ENABLED_TYPE_ENABLED;
    else
    {
        gint submenus;

        if (donna_config_get_int (config, NULL, &submenus,
                    "context_menus/%s/%s/submenus",
                    source, item))
            info->submenus = (guint) CLAMP (submenus, 0, 3);
        else if (import_from_trigger)
            import |= IMPORT_SUBEMNUS;
    }

    if (donna_config_get_string (config, NULL, (gchar **) &info->menu,
                "context_menus/%s/%s/menu", source, item))
        info->free_menu = TRUE;
    else if (import_from_trigger)
        import |= IMPORT_DEFAULT;

    /* do we need to import anything from node_trigger? */
    if (import > 0)
    {
        if (!node_trigger)
            node_trigger = get_node_trigger (app, info->trigger, context, &err);
        if (!node_trigger)
        {
            g_warning ("Context-menu: Cannot import options from node trigger "
                    "for item 'context_menus/%s/%s': Failed to get node: %s",
                    source, item, (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        else
            import_info_from_node (node_trigger, import, info);
    }

    /* ensure there is always a name */
    if (!info->name)
    {
        info->name = g_strdup (item);
        info->free_name = TRUE;
    }
    else
        /* parse %C/%c in the name */
        info->name = parse_Cc ((gchar *) info->name, s_C, s_c);

    if (node_trigger)
        g_object_unref (node_trigger);

    return TRUE;
}

static void
load_menu_properties_to_node (DonnaContextInfo  *info,
                              DonnaNode         *node,
                              DonnaApp          *app,
                              const gchar       *item)
{
    GError *err = NULL;
    GValue v = G_VALUE_INIT;

    if (info->icon_special == DONNA_CONTEXT_ICON_IS_IMAGE)
    {
        if ((info->icon_is_gicon_selected && info->icon_selected)
                || info->icon_name_selected)
        {
            g_value_init (&v, G_TYPE_ICON);
            if (info->icon_is_gicon_selected)
            {
                if (info->free_icon_selected)
                {
                    g_value_take_object (&v, info->icon_selected);
                    info->free_icon_selected = FALSE;
                }
                else
                    g_value_set_object (&v, info->icon_selected);
            }
            else
                g_value_take_object (&v, g_themed_icon_new (info->icon_name_selected));

            if (G_UNLIKELY (!donna_node_add_property (node,
                            "menu-image-selected",
                            G_TYPE_ICON,
                            &v,
                            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                            (refresher_fn) gtk_true,
                            NULL,
                            NULL, NULL,
                            &err)))
            {
                g_warning ("Context-menu: Failed to set image selected "
                        "for item '%s': %s",
                        item,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
                g_value_unset (&v);
            }
            g_value_unset (&v);
        }
    }
    else
    {
        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, info->icon_special);
        if (G_UNLIKELY (!donna_node_add_property (node,
                        "menu-image-special",
                        G_TYPE_UINT, &v,
                        DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                        (refresher_fn) gtk_true,
                        NULL,
                        NULL, NULL,
                        &err)))
        {
            g_warning ("Context-menu: Failed to set image-special "
                    "for item '%s': %s",
                    item,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        g_value_unset (&v);

        if (info->is_active)
        {
            g_value_init (&v, G_TYPE_BOOLEAN);
            g_value_set_boolean (&v, info->is_active);
            if (G_UNLIKELY (!donna_node_add_property (node,
                            "menu-is-active",
                            G_TYPE_BOOLEAN, &v,
                            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                            (refresher_fn) gtk_true,
                            NULL,
                            NULL, NULL,
                            &err)))
            {
                g_warning ("Context-menu: Failed to set is-active "
                        "for item '%s': %s",
                        item,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            g_value_unset (&v);
        }

        if (info->is_inconsistent)
        {
            g_value_init (&v, G_TYPE_BOOLEAN);
            g_value_set_boolean (&v, info->is_inconsistent);
            if (G_UNLIKELY (!donna_node_add_property (node,
                            "menu-is-active",
                            G_TYPE_BOOLEAN, &v,
                            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                            (refresher_fn) gtk_true,
                            NULL,
                            NULL, NULL,
                            &err)))
            {
                g_warning ("Context-menu: Failed to set is-inconsistent "
                        "for item '%s': %s",
                        item,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            g_value_unset (&v);
        }
    }

    if (info->is_menu_bold)
    {
        g_value_init (&v, G_TYPE_BOOLEAN);
        g_value_set_boolean (&v, TRUE);
        if (G_UNLIKELY (!donna_node_add_property (node,
                        "menu-is-label-bold",
                        G_TYPE_BOOLEAN, &v,
                        DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                        (refresher_fn) gtk_true,
                        NULL,
                        NULL, NULL,
                        &err)))
        {
            g_warning ("Context-menu: Failed to set label bold "
                    "for item '%s': %s",
                    item,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        g_value_unset (&v);
    }

    if (info->submenus > 0)
    {
        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, MIN ((guint) info->submenus, 3));
        if (G_UNLIKELY (!donna_node_add_property (node,
                        "menu-submenus",
                        G_TYPE_UINT, &v,
                        DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                        (refresher_fn) gtk_true,
                        NULL,
                        NULL, NULL,
                        &err)))
        {
            g_warning ("Context-menu: Failed to set submenus type "
                    "for item '%s': %s",
                    item,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        g_value_unset (&v);
    }

    if (info->menu)
    {
        g_value_init (&v, G_TYPE_STRING);
        if (info->free_menu)
        {
            g_value_take_string (&v, (gchar *) info->menu);
            info->free_menu = FALSE;
        }
        else
            g_value_set_static_string (&v, info->menu);
        if (G_UNLIKELY (!donna_node_add_property (node,
                        "menu-menu",
                        G_TYPE_STRING, &v,
                        DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                        (refresher_fn) gtk_true,
                        NULL,
                        NULL, NULL,
                        &err)))
        {
            g_warning ("Context-menu: Failed to set menu definition "
                    "for item '%s': %s",
                    item,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        g_value_unset (&v);
    }
}

struct gcem
{
    DonnaProviderInternal *pi;
    gchar *errmsg;
};

static void
free_gcem (struct gcem *gcem)
{
    g_object_unref (gcem->pi);
    g_free (gcem->errmsg);
    g_free (gcem);
}

static DonnaTaskState
get_children_errmsg (DonnaTask      *task,
                     DonnaNode      *node,
                     DonnaNodeType   node_types,
                     gboolean        get_children,
                     struct gcem    *gcem)
{
    GValue *value;

    value = donna_task_grab_return_value (task);
    if (get_children)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_with_free_func (g_object_unref);
        if (node_types & DONNA_NODE_ITEM)
            g_ptr_array_add (arr, donna_provider_internal_new_node (gcem->pi,
                    gcem->errmsg,
                    FALSE, NULL, /* no icon */
                    NULL, /* desc */
                    DONNA_NODE_ITEM,
                    FALSE, /* sensitive */
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (internal_fn) gtk_false, NULL, NULL,
                    NULL));

        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_take_boxed (value, arr);
    }
    else
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, node_types & DONNA_NODE_ITEM);
    }
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

enum parse
{
    PARSE_DEFAULT       = 0,
    PARSE_IS_INTERNAL   = (1 << 0),
    PARSE_IN_CONTAINER  = (1 << 1),
    PARSE_IS_ALIAS      = (1 << 2),
};

static GPtrArray *
parse_items (DonnaApp               *app,
             DonnaProviderInternal  *pi,
             enum parse              cur_parse,
             gchar                 **_items,
             get_alias_fn            get_alias,
             get_item_info_fn        get_item_info,
             DonnaContextReference   reference,
             const gchar            *source,
             DonnaContext           *context,
             GError                **error)
{
    GPtrArray *nodes;
    gchar *items = *_items;

    nodes = g_ptr_array_new_with_free_func (donna_g_object_unref);
    for (;;)
    {
        GError *err = NULL;
        DonnaContextInfo info = { NULL, };
        DonnaNode *node;
        enum parse parse = PARSE_DEFAULT;
        struct node_internal *ni;
        gboolean r;
        gchar *extra;
        gchar *end, *e;
        gchar c_end;
        GValue v = G_VALUE_INIT;

        if (cur_parse & PARSE_IN_CONTAINER)
            parse |= PARSE_IN_CONTAINER;

        /* ltrim */
        skip_blank (items);

        if (*items == ':')
        {
            /* internal item? */
            ++items;
            parse |= PARSE_IS_INTERNAL;
        }
        else if (*items == '!')
        {
            /* internal alias? */
            ++items;
            parse |= PARSE_IS_INTERNAL | PARSE_IS_ALIAS;
        }
        else if (*items == '@')
        {
            /* user alias? (no recursion allowed) */
            if (parse & PARSE_IS_ALIAS)
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_INVALID_SYNTAX,
                        "Cannot use user alias (@prefixed) from an alias");
                g_ptr_array_unref (nodes);
                return NULL;
            }
            ++items;
            parse |= PARSE_IS_ALIAS;
        }

        /* locate the end of the item name */
        for (end = items;
                *end != ',' && *end != '\0' && *end != '<' && *end != ':'
                && (!(parse & PARSE_IN_CONTAINER) || *end != '>');
                ++end)
            ;
        /* so we know "where" we are */
        c_end = *end;

        /* rtrim */
        for (e = end - 1; e > items && isblank (*e); --e)
            ;
        *++e = '\0';

        /* get extra (if any) */
        if (c_end == ':')
        {
            /* extras can be quoted */
            if (end[1] == '"')
            {
                guint i = 0;

                extra = end + 2;
                for (end = extra; ; ++end)
                {
                    if (end[i] == '\\')
                    {
                        *end = end[++i];
                        continue;
                    }
                    *end = end[i];
                    if (*end == '"')
                        break;
                    else if (*end == '\0')
                    {
                        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                                DONNA_CONTEXT_MENU_ERROR_INVALID_SYNTAX,
                                "Missing ending quote of extras");
                        g_ptr_array_unref (nodes);
                        return NULL;
                    }
                }
                *end = '\0';
                end += i;
                if (*end != ',' && *end != '\0' && *end != '<'
                        && (!(parse & PARSE_IN_CONTAINER) || *end != '>'))
                {
                    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                            DONNA_CONTEXT_MENU_ERROR_INVALID_SYNTAX,
                            "Invalid character after quoted extras, "
                            "expected EOL, ',', '<' or (if in submenu) '>'");
                    g_ptr_array_unref (nodes);
                    return NULL;
                }
                c_end = *end;
            }
            else
            {
                extra = end + 1;
                for (end = extra;
                        *end != ',' && *end != '\0' && *end != '<'
                        && (!(parse & PARSE_IN_CONTAINER) || *end != '>');
                        ++end)
                    ;
                c_end = *end;
                *end = '\0';
            }

            if (*extra == '\0')
                extra = NULL;
        }
        else
            extra = NULL;

        /* special case: separator */
        if (streq (items, "-"))
        {
            g_ptr_array_add (nodes, NULL);
            goto next;
        }

        /* get item info */
        if (parse & PARSE_IS_ALIAS)
        {
            GPtrArray *arr;
            gchar *alias;
            gchar *s;

            if (parse & PARSE_IS_INTERNAL)
                alias = get_alias (items, extra, reference, context, error);
            else
                alias = get_user_alias (items, extra, app, source, error);
            if (!alias)
            {
                g_prefix_error (error, "Failed resolving alias '%s': ",
                        items);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            /* alias can be an empty string, in which case we don't need to free
             * it, and it just means nothing (i.e. not an error) */
            if (*alias != '\0')
            {
                s = alias;
                arr = parse_items (app, pi, cur_parse | PARSE_IS_ALIAS, &s,
                        get_alias, get_item_info, reference, source, context, error);
                if (!arr)
                {
                    g_free (alias);
                    g_ptr_array_unref (nodes);
                    return NULL;
                }
                g_free (alias);

                if (arr->len > 0)
                {
                    guint len = nodes->len;

                    g_ptr_array_set_size (nodes, (gint) (len + arr->len));
                    memcpy (&nodes->pdata[len], &arr->pdata[0],
                            sizeof (gpointer) * arr->len);
                    g_ptr_array_set_free_func (arr, NULL);
                }

                g_ptr_array_unref (arr);
            }
            goto next;
        }
        else if (parse & PARSE_IS_INTERNAL)
            r = get_item_info (items, extra, reference, context, &info, error);
        else
            r = get_user_item_info (items, extra, reference, app, source,
                    context, &info, error);
        if (!r)
        {
            g_ptr_array_unref (nodes);
            return NULL;
        }

        /* create node */
        if (c_end == '<') /* submenu, i.e. make a container node */
        {
            GPtrArray *children;

            ++end;
            children = parse_items (app, pi, cur_parse | PARSE_IN_CONTAINER, &end,
                    get_alias, get_item_info, reference, source, context, error);
            if (!children)
            {
                g_prefix_error (error, "Failed to get children for item '%s': ",
                        items);
                free_context_info (&info);
                g_ptr_array_unref (nodes);
                return NULL;
            }
            c_end = *++end;

            if (info.node)
            {
                DonnaNode *n;

                /* we only have the node to use, only we can't use it since we
                 * need to create a container. So we import everything from it
                 * */

                n = info.node;
                memset (&info, 0, sizeof (info));
                import_info_from_node (n, IMPORT_ALL, &info);
                g_object_unref (n);
            }

            node = donna_provider_internal_new_node (pi, info.name,
                    info.icon_is_gicon, (info.icon_is_gicon)
                    ? (gconstpointer) info.icon : info.icon_name,
                    info.desc,
                    DONNA_NODE_CONTAINER,
                    /* ignore info.is_sensitive otherwise we couldn't get the
                     * submenu; We use menu-is-combined-sensitive instead */
                    TRUE,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (internal_fn) container_children_cb, children,
                    (GDestroyNotify) g_ptr_array_unref,
                    error);
            if (G_UNLIKELY (!node))
            {
                g_prefix_error (error, "Error for item '%s': "
                        "couldn't create node: ", items);
                g_ptr_array_unref (children);
                free_context_info (&info);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            /* set sensitivity for the item part only (when combine) */
            if (!info.is_sensitive)
            {
                g_value_init (&v, G_TYPE_BOOLEAN);
                g_value_set_boolean (&v, FALSE);
                if (G_UNLIKELY (!donna_node_add_property (node,
                                "menu-is-combined-sensitive",
                                G_TYPE_BOOLEAN, &v,
                                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                                (refresher_fn) gtk_true,
                                NULL,
                                NULL, NULL,
                                &err)))
                {
                    g_warning ("Context-menu: Failed to set item sensitivity "
                            "for item '%s': %s",
                            items,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
                g_value_unset (&v);
            }

            /* if is_container it can't be triggered, so we'll force submenus to
             * ENABLED */
            if (info.is_container)
                info.submenus = 1;

            load_menu_properties_to_node (&info, node, app, items);

            if (!info.is_container)
            {
                if (info.trigger)
                {
                    /* we do the parsing, but ignore intrefs since the trigger is
                     * just a string property, so they'll be cleaned via GC */
                    info.trigger = donna_app_parse_fl (app,
                            (gchar *) info.trigger, info.free_trigger,
                            context, NULL);

                    g_value_init (&v, G_TYPE_STRING);
                    g_value_take_string (&v, (gchar *) info.trigger);
                    info.free_trigger = FALSE;

                    if (G_UNLIKELY (!donna_node_add_property (node,
                                    "container-trigger",
                                    G_TYPE_STRING, &v,
                                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                                    (refresher_fn) gtk_true,
                                    NULL,
                                    NULL, NULL,
                                    error)))
                    {
                        g_prefix_error (error, "Error for item '%s': "
                                "Failed to set 'container-trigger': ",
                                items);
                        g_value_unset (&v);
                        g_object_unref (node);
                        free_context_info (&info);
                        g_ptr_array_unref (nodes);
                        return NULL;
                    }
                    g_value_unset (&v);
                }
                else if (info.node)
                {
                    g_value_init (&v, DONNA_TYPE_NODE);
                    g_value_set_object (&v, info.node);
                    if (G_UNLIKELY (!donna_node_add_property (node,
                                    "container-trigger",
                                    DONNA_TYPE_NODE, &v,
                                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                                    (refresher_fn) gtk_true,
                                    NULL,
                                    NULL, NULL,
                                    error)))
                    {
                        g_prefix_error (error, "Error for item '%s': "
                                "Failed to set 'container-trigger': ",
                                items);
                        g_value_unset (&v);
                        g_object_unref (node);
                        free_context_info (&info);
                        g_ptr_array_unref (nodes);
                        return NULL;
                    }
                    g_value_unset (&v);
                }
            }

            if (info.new_node_fn)
            {
                info.new_node_fn (node, info.new_node_data);
                info.new_node_destroy = NULL;
            }
        }
        else /* not a submenu */
        {
            struct gcem *gcem = NULL;

            if (info.node)
            {
                g_ptr_array_add (nodes, info.node);
                info.node = NULL;
                goto skip;
            }

            if (!info.is_visible)
                goto skip;

            ni = g_slice_new0 (struct node_internal);
            ni->app = app;
            if (info.trigger)
            {
                if (info.is_container)
                {
                    ni->node_trigger = donna_app_get_node (app, info.trigger, TRUE, &err);
                    if (!ni->node_trigger)
                    {
                        gcem = g_new (struct gcem, 1);
                        gcem->pi = g_object_ref (pi);

                        if (err)
                            gcem->errmsg = g_strdup (err->message);
                        else
                            gcem->errmsg = g_strdup_printf (
                                    "Failed to get children for item '%s' "
                                    "(no error message)",
                                    items);
                        g_clear_error (&err);
                    }
                    else if (donna_node_get_node_type (ni->node_trigger)
                            != DONNA_NODE_CONTAINER)
                    {
                        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                                DONNA_CONTEXT_MENU_ERROR_OTHER,
                                "Node for trigger of item '%s' isn't a container"
                                " (while item is flagged is-container)",
                                items);
                        free_context_info (&info);
                        free_node_internal (ni);
                        g_ptr_array_unref (nodes);
                        return NULL;
                    }
                }
                else
                {
                    ni->fl = donna_app_parse_fl (app,
                            (gchar *) info.trigger, info.free_trigger,
                            context, &ni->intrefs);
                    ni->free_fl = TRUE;
                }
                info.trigger = NULL;
                info.free_trigger = FALSE;
            }

            node = donna_provider_internal_new_node (pi,
                    info.name,
                    info.icon_is_gicon,
                    (info.icon_is_gicon) ? (gconstpointer) info.icon : info.icon_name,
                    info.desc,
                    (info.is_container) ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
                    info.is_sensitive,
                    (info.is_container) ? DONNA_TASK_VISIBILITY_INTERNAL : DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (gcem) ? (internal_fn) get_children_errmsg : (info.is_container) ? (internal_fn) node_children_cb : (internal_fn) node_internal_cb,
                    (gcem) ? (gpointer) gcem : (gpointer) ni,
                    (gcem) ? (GDestroyNotify) free_gcem : (GDestroyNotify) free_node_internal,
                    error);
            if (G_UNLIKELY (!node))
            {
                g_prefix_error (error, "Error for item '%s': "
                        "couldn't create node: ", items);
                free_context_info (&info);
                free_node_internal (ni);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            load_menu_properties_to_node (&info, node, app, items);

            if (info.new_node_fn)
            {
                info.new_node_fn (node, info.new_node_data);
                info.new_node_destroy = NULL;
            }
        }

        g_ptr_array_add (nodes, node);
skip:
        free_context_info (&info);

next:
        /* EOF, or last item of the submenu (only if PARSE_IN_CONTAINER) */
        if (c_end == '\0' || c_end == '>')
        {
            *_items = end;
            break;
        }
        items = end + 1;
    }

    return nodes;
}

GPtrArray *
donna_context_menu_get_nodes (DonnaApp               *app,
                              gchar                  *items,
                              DonnaContextReference   reference,
                              const gchar            *source,
                              get_alias_fn            get_alias,
                              get_item_info_fn        get_item_info,
                              DonnaContext           *context,
                              GError                **error)
{
    DonnaProviderInternal *pi;
    GPtrArray *nodes;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (items != NULL, NULL);
    g_return_val_if_fail (source != NULL, NULL);

    pi = (DonnaProviderInternal *) donna_app_get_provider (app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Failed to get nodes: Couldn't get provider 'internal'");
        return NULL;
    }

    nodes = parse_items (app, pi, PARSE_DEFAULT, &items, get_alias, get_item_info,
            reference, source, context, error);

    g_object_unref (pi);
    return nodes;
}

inline gboolean
donna_context_menu_popup (DonnaApp              *app,
                          gchar                 *items,
                          DonnaContextReference  reference,
                          const gchar           *source,
                          get_alias_fn           get_alias,
                          get_item_info_fn       get_item_info,
                          DonnaContext          *context,
                          const gchar           *menu,
                          GError               **error)
{
    GPtrArray *nodes;

    nodes = donna_context_menu_get_nodes (app, items, reference, source,
            get_alias, get_item_info, context, error);
    if (!nodes)
        return FALSE;

    return donna_app_show_menu (app, nodes, menu, error);
}
