
#include "contextmenu.h"
#include "conf.h"
#include "provider-internal.h"
#include "node.h"
#include "util.h"
#include "macros.h"
#include "debug.h"

enum type
{
    TYPE_STANDARD = 0,
    TYPE_TRIGGER,
    TYPE_CONTAINER,
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



/* helper to parse "extra" field of a section */

static void
free_context_info (DonnaContextInfo *info)
{
    if (info->node)
        g_object_unref (info->node);
    if (info->free_name)
        g_free (info->name);
    if (info->free_icon)
    {
        if (info->icon_is_pixbuf)
            g_object_unref (info->pixbuf);
        else
            g_free (info->icon_name);
    }
    if (info->free_desc)
        g_free (info->desc);
    if (info->free_trigger)
        g_free (info->trigger);
}

static DonnaTaskState
node_internal_cb (DonnaTask *task, DonnaNode *node, struct node_internal *ni);

static DonnaTaskState
container_children_cb (DonnaTask    *task,
                       DonnaNode    *node,
                       DonnaNodeType node_types,
                       gboolean      get_children,
                       GPtrArray    *children);

static GPtrArray *
parse_extra (DonnaApp               *app,
             DonnaProviderInternal  *pi,
             gboolean                in_container,
             const gchar            *section,
             const gchar           **extra,
             get_item_info_fn        get_item_info,
             DonnaContextReference   reference,
             const gchar            *conv_flags,
             conv_flag_fn            conv_fn,
             gpointer                conv_data,
             gpointer                section_data,
             GError                **error)
{
    GPtrArray *nodes;
    const gchar *e;

    nodes = g_ptr_array_new_with_free_func (donna_g_object_unref);
    for (e = *extra; ; ++e)
    {
        if (*e == ';' || *e == '\0' || (in_container && *e == '>'))
        {
            gchar buf[32], *b = buf;
            DonnaContextInfo info = { 0, };
            DonnaNode *node;
            struct node_internal *ni;

            if (e - *extra == 1 && **extra == '-')
            {
                g_ptr_array_add (nodes, NULL);
                goto next;
            }
            else if (e == *extra)
            {
                ++*extra;
                break;
            }

            if (snprintf (buf, 32, "%.*s", (gint) (e - *extra), *extra) >= 32)
                b = g_strdup_printf ("%.*s", (gint) (e - *extra), *extra);

            if (!get_item_info (section, b, reference,
                        conv_flags, conv_fn, conv_data, section_data,
                        &info, error))
            {
                if (b != buf)
                    g_free (b);
                g_ptr_array_unref (nodes);
                return NULL;
            }

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
                ni->fl = donna_app_parse_fl (app,
                        (info.free_trigger) ? info.trigger : g_strdup (info.trigger),
                        conv_flags, conv_fn, conv_data, &ni->intrefs);
                ni->free_fl = TRUE;
                info.trigger = NULL;
                info.free_trigger = FALSE;
            }

            node = donna_provider_internal_new_node (pi, info.name,
                    info.icon_is_pixbuf, (info.icon_is_pixbuf)
                    ? (gconstpointer) info.pixbuf : info.icon_name,
                    info.desc,
                    DONNA_NODE_ITEM, info.is_sensitive,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (internal_fn) node_internal_cb, ni,
                    (GDestroyNotify) free_node_internal,
                    error);
            if (G_UNLIKELY (!node))
            {
                g_prefix_error (error, "Error in section '%s' for item '%s': "
                        "couldn't create node: ", section, b);
                free_context_info (&info);
                free_node_internal (ni);
                if (b != buf)
                    g_free (b);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            if (info.is_menu_bold)
            {
                GError *err = NULL;
                GValue v = G_VALUE_INIT;

                g_value_init (&v, G_TYPE_BOOLEAN);
                g_value_set_boolean (&v, TRUE);
                if (G_UNLIKELY (!donna_node_add_property (node,
                                "menu-is-label-bold",
                                G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true,
                                NULL, &err)))
                {
                    g_warning ("Context-menu: Failed to set label bold for "
                            "item '%s' in section '%s': %s", b, section,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
                g_value_unset (&v);
            }

            g_ptr_array_add (nodes, node);

skip:
            free_context_info (&info);

            if (b != buf)
                g_free (b);

next:
            *extra = e + 1;
            if (*e == '\0')
            {
                if (in_container)
                {
                    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                            DONNA_CONTEXT_MENU_ERROR_OTHER,
                            "Invalid syntax: missing end of contained items ('>')");
                    g_ptr_array_unref (nodes);
                    return NULL;
                }
                else
                    break;
            }
            else if (*e == '>') /* implies in_container */
                break;
        }
        else if (*e == '<')
        {
            GPtrArray *children;
            gchar buf[32], *b = buf;
            DonnaContextInfo info = { 0, };
            DonnaNode *node;
            GValue v = G_VALUE_INIT;
            const gchar *cont_s;
            gint cont_len;

            cont_s = *extra;
            cont_len = e - cont_s;
            *extra = e + 1;

            children = parse_extra (app, pi, TRUE, section, extra,
                    get_item_info, reference, conv_flags, conv_fn, conv_data,
                    section_data, error);
            if (!children)
            {
                g_ptr_array_unref (nodes);
                return NULL;
            }

            if (snprintf (buf, 32, "%.*s", cont_len, cont_s) >= 32)
                b = g_strdup_printf ("%.*s", cont_len, cont_s);

            if (!get_item_info (section, b, reference,
                        conv_flags, conv_fn, conv_data, section_data,
                        &info, error))
            {
                if (b != buf)
                    g_free (b);
                g_ptr_array_unref (children);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            if (info.node)
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_OTHER,
                        "Context-menu: Cannot use given node for item '%s/%s', "
                        "needs info to create a container",
                        section, b);
                free_context_info (&info);
                if (b != buf)
                    g_free (b);
                g_ptr_array_unref (children);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            node = donna_provider_internal_new_node (pi, info.name,
                    info.icon_is_pixbuf, (info.icon_is_pixbuf)
                    ? (gconstpointer) info.pixbuf : info.icon_name,
                    info.desc,
                    DONNA_NODE_CONTAINER,
                    /* ignore info.is_sensitive otherwise we couldn't get the
                     * submenu! */
                    TRUE,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (internal_fn) container_children_cb, children,
                    (GDestroyNotify) g_ptr_array_unref,
                    error);
            if (G_UNLIKELY (!node))
            {
                g_prefix_error (error, "Error in section '%s' for item '%s': "
                        "couldn't create node: ", section, b);
                free_context_info (&info);
                if (b != buf)
                    g_free (b);
                g_ptr_array_unref (children);
                g_ptr_array_unref (nodes);
                return NULL;
            }

            if (info.is_menu_bold)
            {
                GError *err = NULL;
                GValue v = G_VALUE_INIT;

                g_value_init (&v, G_TYPE_BOOLEAN);
                g_value_set_boolean (&v, TRUE);
                if (G_UNLIKELY (!donna_node_add_property (node,
                                "menu-is-label-bold",
                                G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true,
                                NULL, &err)))
                {
                    g_warning ("Context-menu: Failed to set label bold for "
                            "item '%s' in section '%s': %s", b, section,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
                g_value_unset (&v);
            }

            if (info.trigger)
            {
                /* we do the parsing, but ignore intrefs since the trigger is
                 * just a string property, so they'll be cleaned via GC */
                info.trigger = donna_app_parse_fl (app,
                        (info.free_trigger) ? g_strdup (info.trigger) : info.trigger,
                        conv_flags, conv_fn, conv_data, NULL);

                g_value_init (&v, G_TYPE_STRING);
                g_value_take_string (&v, info.trigger);
                info.free_trigger = FALSE;

                if (G_UNLIKELY (!donna_node_add_property (node, "container-trigger",
                                G_TYPE_STRING, &v, (refresher_fn) gtk_true, NULL, error)))
                {
                    g_prefix_error (error, "Error in section '%s' for item '%s': "
                            "Failed to set 'container-trigger': ",
                            section, b);
                    g_value_unset (&v);
                    g_object_unref (node);
                    free_context_info (&info);
                    if (b != buf)
                        g_free (b);
                    g_ptr_array_unref (nodes);
                    g_object_unref (pi);
                    return NULL;
                }
                g_value_unset (&v);
            }

            free_context_info (&info);
            if (b != buf)
                g_free (b);

            g_ptr_array_add (nodes, node);
            e = *extra - 1;
        }
    }

    return nodes;
}

GPtrArray *
donna_context_parse_extra (DonnaApp               *app,
                           const gchar            *section,
                           const gchar            *extra,
                           get_item_info_fn        get_item_info,
                           DonnaContextReference   reference,
                           const gchar            *conv_flags,
                           conv_flag_fn            conv_fn,
                           gpointer                conv_data,
                           gpointer                section_data,
                           GError                **error)
{
    DonnaProviderInternal *pi;
    GPtrArray *nodes;

    pi = (DonnaProviderInternal *) donna_app_get_provider (app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Failed to parse extra: Couldn't get provider 'internal'");
        return NULL;
    }

    nodes = parse_extra (app, pi, FALSE, section, &extra,
            get_item_info, reference, conv_flags, conv_fn, conv_data,
            section_data, error);

    g_object_unref (pi);
    return nodes;
}


/* getting nodes for context menu */

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

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (ni->app, t);
    donna_task_wait_for_it (t);

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
    GPtrArray *arr;
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
            {
                has_children = TRUE;
                break;
            }
            else
                g_ptr_array_add (arr, g_object_ref (children->pdata[i]));
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
                  gchar         *fl,
                  const gchar   *conv_flags,
                  conv_flag_fn   conv_fn,
                  gpointer       conv_data)
{
    DonnaTask *task;
    DonnaNode *node;

    fl = donna_app_parse_fl (app, g_strdup (fl), conv_flags, conv_fn, conv_data, NULL);

    task = donna_app_get_node_task (app, fl);
    if (G_UNLIKELY (!task))
    {
        g_free (fl);
        return NULL;
    }
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (app, task);
    donna_task_wait_for_it (task);
    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        g_object_unref (task);
        g_free (fl);
        return NULL;
    }
    node = g_value_dup_object (donna_task_get_return_value (task));
    g_object_unref (task);
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

struct get_info {
    DonnaApp    *app;
    const gchar *source;
    const gchar *s_c;
    const gchar *s_C;
};

#define ensure_node_trigger()   do {                                        \
    if (!node_trigger)                                                      \
        node_trigger = get_node_trigger (gi->app, info->trigger,            \
                conv_flags, conv_fn, conv_data);                            \
    if (!node_trigger)                                                      \
    {                                                                       \
        g_warning ("Context-menu: Cannot import options from node trigger " \
                "for item 'context_menus/%s/%s/%s': Failed to get node",    \
                gi->source, section, item);                                 \
        import_from_trigger = FALSE;                                        \
    }                                                                       \
} while (0)
static gboolean
context_get_item_info (const gchar             *section,
                       const gchar             *item,
                       DonnaContextReference    reference,
                       const gchar             *conv_flags,
                       conv_flag_fn             conv_fn,
                       gpointer                 conv_data,
                       struct get_info          *gi,
                       DonnaContextInfo        *info,
                       GError                 **error)
{
    DonnaConfig *config = donna_app_peek_config (gi->app);
    DonnaNode *node_trigger = NULL;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;
    GPtrArray *triggers = NULL;
    enum type type;
    gboolean import_from_trigger = FALSE;
    gboolean b;
    gchar *s;

    /* default name/icon for container */
    if (*item == '\0')
    {
        if (!donna_config_get_string (config, &info->name,
                    "context_menus/%s/%s/name", gi->source, section))
            info->name = g_strdup (section);
        info->free_name = TRUE;

        if (donna_config_get_string (config, &s, "context_menus/%s/%s/icon",
                    gi->source, section))
        {
            info->icon_name = s;
            info->free_icon = TRUE;
        }

        return TRUE;
    }

    if (!donna_config_has_category (config, "context_menus/%s/%s/%s",
                gi->source, section, item))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "Unknown item '%s' in user section '%s' for '%s'",
                item, section, gi->source);
        return FALSE;
    }

    if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/is_visible",
                gi->source, section, item))
    {
        enum expr expr;

        expr = evaluate (reference, s, error);
        if (expr == EXPR_INVALID)
        {
            g_prefix_error (error, "Failed to evaluate 'is_visible' "
                    "for item 'context_menus/%s/%s/%s': ",
                    gi->source, section, item);
            g_free (s);
            return FALSE;
        }
        info->is_visible = expr == EXPR_TRUE;
        g_free (s);
    }
    else
        info->is_visible = TRUE;

    if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/is_sensitive",
                gi->source, section, item))
    {
        enum expr expr;

        expr = evaluate (reference, s, error);
        if (expr == EXPR_INVALID)
        {
            g_prefix_error (error, "Failed to evaluate 'is_sensitive' "
                    "for item 'context_menus/%s/%s/%s': ",
                    gi->source, section, item);
            g_free (s);
            return FALSE;
        }
        info->is_sensitive = expr == EXPR_TRUE;
        g_free (s);
    }
    else
        info->is_sensitive = TRUE;

    /* find the (matching) trigger */
    if (donna_config_list_options (config, &triggers,
                DONNA_CONFIG_OPTION_TYPE_OPTION, "context_menus/%s/%s/%s",
                gi->source, section, item))
    {
        guint j;

        for (j = 0; j < triggers->len; ++j)
        {
            GError *err = NULL;
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
            if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/%s",
                        gi->source, section, item, t))
            {
                enum expr expr;

                expr = evaluate (reference, s, &err);
                if (expr == EXPR_INVALID)
                {
                    g_warning ("Context-menu: Skipping trigger declaration, "
                            "invalid expression in 'context_menus/%s/%s/%s/%s': %s",
                            gi->source, section, item, t,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                    g_free (s);
                    continue;
                }
                else if (expr == EXPR_TRUE)
                {
                    /* get the actual trigger */
                    if (!donna_config_get_string (config, &info->trigger,
                                "context_menus/%s/%s/%s/%.*s",
                                gi->source, section, item, (gint) (len - 5), t))
                    {
                        g_warning ("Context-menu: Trigger option missing: "
                                "'context_menus/%s/%s/%s/%s' -- Skipping trigger",
                                gi->source, section, item, t);
                        g_free (s);
                        continue;
                    }
                    g_free (s);
                    break;
                }
                else /* EXPR_FALSE */
                    g_free (s);
            }
        }
        g_ptr_array_unref (triggers);
    }

    /* last chance: the default "trigger" */
    if (!info->trigger && !donna_config_get_string (config, &info->trigger,
                "context_menus/%s/%s/%s/trigger", gi->source, section, item))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "No trigger found for 'context_menus/%s/%s/%s'",
                gi->source, section, item);
        return FALSE;
    }

    /* parse %C/%c in the trigger */
    info->trigger = parse_Cc (info->trigger, gi->s_C, gi->s_c);
    info->free_trigger = TRUE;

    /* type of item */
    if (donna_config_get_int (config, (gint *) &type,
                "context_menus/%s/%s/%s/type", gi->source, section, item))
        type = CLAMP (type, TYPE_STANDARD, NB_TYPES - 1);
    else
        type = TYPE_STANDARD;

    if (type == TYPE_TRIGGER)
    {
        info->node = get_node_trigger (gi->app, info->trigger,
                conv_flags, conv_fn, conv_data);
        g_free (info->trigger);
        info->trigger = NULL;
        info->free_trigger = FALSE;

        if (!info->node)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Failed to get node for item 'context_menus/%s/%s/%s'",
                    gi->source, section, item);
            return FALSE;
        }

        return TRUE;
    }

    /* shall we import non-specified stuff from node trigger? */
    donna_config_get_boolean (config, &import_from_trigger,
            "context_menus/%s/%s/%s/import_from_trigger",
            gi->source, section, item);

    /* is_sensitive is a bit special, in that we only import from trigger if
     * TRUE (since if FALSE, that takes precedence, but if TRUE the value from
     * trigger does) */
    if (info->is_sensitive && import_from_trigger)
    {
        ensure_node_trigger ();
        if (import_from_trigger)
        {
            donna_node_get (node_trigger, FALSE, "menu-is-sensitive",
                    &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                    info->is_sensitive = g_value_get_boolean (&v);
                g_value_unset (&v);
            }
        }
    }

    /* name */
    if (!donna_config_get_string (config, &info->name, "context_menus/%s/%s/%s/name",
                gi->source, section, item))
    {
        if (import_from_trigger)
        {
            ensure_node_trigger ();
            if (import_from_trigger)
                info->name = donna_node_get_name (node_trigger);
        }
        else
            info->name = g_strdup (info->trigger);
    }

    /* parse %C/%c in the name */
    info->name = parse_Cc (info->name, gi->s_C, gi->s_c);
    info->free_name = TRUE;

    /* icon */
    if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/icon",
            gi->source, section, item))
    {
        info->icon_name = s;
        info->free_icon = TRUE;
    }
    else if (import_from_trigger)
    {
        ensure_node_trigger ();
        if (import_from_trigger)
        {
            donna_node_get (node_trigger, FALSE, "icon", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                info->icon_is_pixbuf = TRUE;
                info->pixbuf = g_value_dup_object (&v);
                info->free_icon = TRUE;
                g_value_unset (&v);
            }
        }
    }

    if (donna_config_get_boolean (config, &b,
                "context_menus/%s/%s/%s/menu_is_label_bold",
                gi->source, section, item))
        info->is_menu_bold = b;
    else
    {
        if (import_from_trigger)
        {
            ensure_node_trigger ();
            if (import_from_trigger)
            {
                donna_node_get (node_trigger, FALSE, "menu-is-label-bold",
                        &has, &v, NULL);
                if (has == DONNA_NODE_VALUE_SET)
                {
                    info->is_menu_bold = g_value_get_boolean (&v);
                    g_value_unset (&v);
                }
            }
        }
    }

    if (type == TYPE_CONTAINER)
    {
        DonnaProviderInternal *pi;
        DonnaNode *node;
        struct node_internal *ni;

        if (!node_trigger)
            node_trigger = get_node_trigger (gi->app, info->trigger,
                    conv_flags, conv_fn, conv_data);
        if (!node_trigger)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Failed to get node for item '%s' ('context_menus/%s/%s/%s')",
                    info->name, gi->source, section, item);
            free_context_info (info);
            return FALSE;
        }
        else if (donna_node_get_node_type (node_trigger) != DONNA_NODE_CONTAINER)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Node for item '%s' ('context_menus/%s/%s/%s') isn't a container",
                    info->name, gi->source, section, item);
            g_object_unref (node_trigger);
            free_context_info (info);
            return FALSE;
        }
        g_free (info->trigger);
        info->trigger = NULL;
        info->free_trigger = FALSE;

        /* special case: we need to create the node */
        ni = g_slice_new0 (struct node_internal);
        ni->app = gi->app;
        ni->node_trigger = g_object_ref (node_trigger);
        ni->fl = info->trigger;

        pi = (DonnaProviderInternal *) donna_app_get_provider (gi->app, "internal");
        if (G_UNLIKELY (!pi))
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Failed to create node for 'context_menus/%s/%s/%s': "
                    "Couldn't get provider 'internal'",
                    gi->source, section, item);
            free_node_internal (ni);
            free_context_info (info);
            if (node_trigger)
                g_object_unref (node_trigger);
            return FALSE;
        }

        node = donna_provider_internal_new_node (pi, info->name,
                info->icon_is_pixbuf,
                (info->icon_is_pixbuf) ? info->pixbuf : (gconstpointer) info->icon_name,
                NULL, DONNA_NODE_CONTAINER, info->is_sensitive,
                DONNA_TASK_VISIBILITY_INTERNAL,
                (internal_fn) node_children_cb, ni,
                (GDestroyNotify) free_node_internal, error);
        if (G_UNLIKELY (!node))
        {
            g_prefix_error (error, "Failed to create node for 'context_menus/%s/%s/%s': ",
                    gi->source, section, item);
            g_object_unref (pi);
            free_node_internal (ni);
            free_context_info (info);
            if (node_trigger)
                g_object_unref (node_trigger);
            return FALSE;
        }

        g_object_unref (pi);

        free_context_info (info);
        memset (info, 0, sizeof (*info));
        info->node = node;

        if (info->is_menu_bold)
        {
            GError *err = NULL;

            g_value_init (&v, G_TYPE_BOOLEAN);
            g_value_set_boolean (&v, TRUE);
            if (G_UNLIKELY (!donna_node_add_property (info->node, "menu-is-label-bold",
                            G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true, NULL, &err)))
            {
                g_warning ("Context-menu: Failed to set label bold for item "
                        "'context_menus/%s/%s/%s': %s",
                        gi->source, section, item,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            g_value_unset (&v);
        }
    }

    if (node_trigger)
        g_object_unref (node_trigger);

    return TRUE;
}
#undef ensure_node_trigger

GPtrArray *
donna_context_menu_get_nodes_v (DonnaApp               *app,
                                GError                **error,
                                gchar                  *_sections,
                                DonnaContextReference   reference,
                                const gchar            *source,
                                get_section_nodes_fn    get_section_nodes,
                                const gchar            *conv_flags,
                                conv_flag_fn            conv_fn,
                                gpointer                conv_data,
                                const gchar            *def_root,
                                const gchar            *root_fmt,
                                va_list                 va_args)
{
    GError *err = NULL;
    DonnaConfig *config;
    GPtrArray *nodes;
    gchar *root;
    gchar *sections;
    gchar *section;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (_sections != NULL || root_fmt != NULL, NULL);
    g_return_val_if_fail (source != NULL, NULL);

    config = donna_app_peek_config (app);

    if (root_fmt)
        root = g_strdup_vprintf (root_fmt, va_args);
    else
        root = NULL;

    if (_sections)
        sections = _sections;
    else
    {
        /* try the _ref/_no_ref setting */
        if (!donna_config_get_string (config, &sections, "%s/context_menu_%s",
                    root,
                    (reference & DONNA_CONTEXT_HAS_REF) ? "ref" : "no_ref"))
        {
            /* then try _ref/_no_ref defaults (if possible)*/
            if (!def_root || !donna_config_get_string (config, &sections,
                        "%s/context_menu_%s",
                        def_root,
                        (reference & DONNA_CONTEXT_HAS_REF) ? "ref" : "no_ref"))
            {
                /* alright, try simple "context_menu" then */
                if (!donna_config_get_string (config, &sections, "%s/context_menu", root))
                {
                    /* last chance, the "context_menu" default */
                    if (!def_root || !donna_config_get_string (config, &sections,
                                "%s/context_menu", def_root))
                    {
                        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                                DONNA_CONTEXT_MENU_ERROR_NO_SECTIONS,
                                "Cannot get nodes for context menu: no sections defined");
                        g_free (root);
                        return NULL;
                    }
                }
            }
        }
    }

    /* because there might be NULL as separators */
    nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) donna_g_object_unref);
    section = sections;
    for (;;)
    {
        GPtrArray *arr = NULL;
        struct get_info gi = { app, source, "", "" };
        gchar *items = NULL;
        gchar *s;
        gchar *end, *e;
        gboolean free_items = FALSE;

        skip_blank (section);
        end = strchr (section, ',');
        if (end)
        {
            *end = '\0';
            for (e = end - 1; e > section && isblank (*e); --e)
                ;
            if (++e != end)
                *e = '\0';
            else
                e = NULL;
        }
        else
            e = NULL;

        /* internal section? */
        if (*section == ':')
        {
            ++section;

            if (!get_section_nodes)
            {
                g_warning ("Context-menu: Invalid section ':%s'; "
                        "no internal sections supported", section);
                goto next;
            }

            s = strchr (section, ':');
            if (s)
                *s++ = '\0';

            arr = get_section_nodes (section, s, reference,
                    conv_flags, conv_fn, conv_data, &err);
            if (!arr)
            {
                g_warning ("Context-menu: Invalid section ':%s': %s",
                        section, (err) ? err->message: "(no error message)");
                g_clear_error (&err);
            }
            goto next;
        }
        else if (streq (section, "-"))
        {
            g_ptr_array_add (nodes, NULL);
            goto next;
        }

        /* user section */

        /* there can be a var=name specified after a ':' */
        s = strchr (section, ':');
        if (s)
        {
            /* and items can be specifed after a '/' */
            items = strchr (s + 1, '/');
            if (items)
                *items++ = '\0';

            *s = '\0';
            gi.s_C = s + 1;
            s = strchr (gi.s_C, '=');
            if (s)
            {
                *s = '\0';
                gi.s_c = s + 1;
            }
        }

        if (!donna_config_has_category (config, "context_menus/%s/%s",
                    source, section))
        {
            g_warning ("Context-menu: Invalid user-section '%s'", section);
            goto next;
        }

        /* first off, determine if it is "visible", i.e. should be included */
        if (donna_config_get_string (config, &s, "context_menus/%s/%s/is_visible",
                    source, section))
        {
            enum expr expr;

            expr = evaluate (reference, s, &err);
            if (expr == EXPR_FALSE)
            {
                g_free (s);
                goto next;
            }
            else if (expr == EXPR_INVALID)
            {
                g_warning ("Context-menu: Failed to evaluate 'is_visible' for "
                        "section '%s': %s -- Skipping", section,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
                g_free (s);
                goto next;
            }
            g_free (s);
        }

        /* if not specified, get default items */
        if (!items)
        {
            if (!donna_config_get_string (config, &items,
                    "context_menus/%s/%s/items", source, section))
            {
                g_warning ("Context-menu: Failed to get default items for section "
                        "'context_menus/%s/%s' -- Skipping",
                        source, section);
                goto next;
            }
            free_items = TRUE;
        }

        arr = donna_context_parse_extra (app, section, items,
                (get_item_info_fn) context_get_item_info,
                reference, conv_flags, conv_fn, conv_data, &gi, &err);
        if (!arr)
        {
            g_warning ("Context-menu: Failed to get items for section '%s': %s",
                    section, (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }

        if (free_items)
            g_free (items);

next:
        if (arr)
        {
            if (arr->len > 0)
            {
                guint pos = nodes->len;
                g_ptr_array_set_size (nodes, nodes->len + arr->len);
                memcpy (&nodes->pdata[pos], &arr->pdata[0], sizeof (gpointer) * arr->len);
                g_ptr_array_set_free_func (arr, NULL);
            }
            g_ptr_array_unref (arr);
        }

        if (e)
            *e = ' ';
        if (!end)
            break;
        *end = ',';
        section = end + 1;
    }

    if (sections != _sections)
        g_free (sections);
    g_free (root);
    return nodes;
}

GPtrArray *
donna_context_menu_get_nodes (DonnaApp               *app,
                              GError                **error,
                              gchar                  *sections,
                              DonnaContextReference   reference,
                              const gchar            *source,
                              get_section_nodes_fn    get_section_nodes,
                              const gchar            *conv_flags,
                              conv_flag_fn            conv_fn,
                              gpointer                conv_data,
                              const gchar            *def_root,
                              const gchar            *root_fmt,
                              ...)
{
    GPtrArray *nodes;
    va_list va_args;

    va_start (va_args, root_fmt);
    nodes = donna_context_menu_get_nodes_v (app, error, sections, reference,
            source, get_section_nodes, conv_flags, conv_fn, conv_data,
            def_root, root_fmt, va_args);
    va_end (va_args);
    return nodes;
}

inline gboolean
donna_context_menu_popup (DonnaApp              *app,
                          GError               **error,
                          gchar                 *sections,
                          DonnaContextReference  reference,
                          const gchar           *source,
                          get_section_nodes_fn   get_section_nodes,
                          const gchar           *conv_flags,
                          conv_flag_fn           conv_fn,
                          gpointer               conv_data,
                          const gchar           *menu,
                          const gchar           *def_root,
                          const gchar           *root_fmt,
                          ...)
{
    GPtrArray *nodes;
    va_list va_args;

    va_start (va_args, root_fmt);
    nodes = donna_context_menu_get_nodes_v (app, error, sections, reference,
            source, get_section_nodes, conv_flags, conv_fn, conv_data,
            def_root, root_fmt, va_args);
    va_end (va_args);
    if (!nodes)
        return FALSE;

    return donna_app_show_menu (app, nodes, menu, error);
}
