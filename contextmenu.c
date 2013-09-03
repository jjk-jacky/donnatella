
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

struct node_internal
{
    DonnaApp    *app;
    DonnaNode   *node_trigger;
    GPtrArray   *intrefs;
    gchar       *fl;
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
    g_free (ni->fl);

    g_slice_free (struct node_internal, ni);
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

static DonnaTaskState
node_internal_cb (DonnaTask *task, DonnaNode *node, struct node_internal *ni)
{
    GError *err = NULL;

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
get_node_trigger (DonnaApp *app, const gchar *fl)
{
    DonnaTask *task;
    DonnaNode *node;

    task = donna_app_get_node_task (app, fl);
    if (G_UNLIKELY (!task))
        return NULL;
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (app, task);
    donna_task_wait_for_it (task);
    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        g_object_unref (task);
        return NULL;
    }
    node = g_value_dup_object (donna_task_get_return_value (task));
    g_object_unref (task);
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

#define ensure_node_trigger()   do {                                        \
    if (!node_trigger)                                                      \
        node_trigger = get_node_trigger (app, fl);                          \
    if (!node_trigger)                                                      \
    {                                                                       \
        g_warning ("Context-menu: Cannot import options from node trigger " \
                "for item 'context_menus/%s/%s/%s': Failed to get node",    \
                source, section, item);                                     \
        import_from_trigger = FALSE;                                        \
    }                                                                       \
} while (0)
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
    DonnaProviderInternal *pi;
    GPtrArray *nodes;
    gchar *root;
    gchar *sections;
    gchar *section;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (_sections != NULL || root_fmt != NULL, NULL);
    g_return_val_if_fail (source != NULL, NULL);

    config = donna_app_peek_config (app);
    pi = (DonnaProviderInternal *) donna_app_get_provider (app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Context-menu: Cannot get nodes for context menu: "
                "failed to get provider 'internal'");
        return NULL;
    }

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
                        g_object_unref (pi);
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
        GPtrArray *children = NULL;
        guint i;
        const gchar *s_c = "";
        const gchar *s_C = "";
        gchar *s;
        gchar *end, *e;
        gboolean is_sensitive = TRUE;
        gboolean b;

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

            arr = get_section_nodes (section, s, reference, conv_data, &err);
            if (!arr)
            {
                g_warning ("Context-menu: Invalid section ':%s': %s",
                        section, err->message);
                g_clear_error (&err);
                goto next;
            }

            if (arr->len > 0)
            {
                guint pos = nodes->len;
                g_ptr_array_set_size (nodes, nodes->len + arr->len);
                memcpy (&nodes->pdata[pos], &arr->pdata[0], sizeof (gpointer) * arr->len);
            }
            g_ptr_array_unref (arr);
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
            *s = '\0';
            s_C = s + 1;
            s = strchr (s_C, '=');
            if (s)
            {
                *s = '\0';
                s_c = s + 1;
            }
        }

        if (!donna_config_has_category (config, "context_menus/%s/%s",
                    source, section))
        {
            g_warning ("Context-menu: Invalid section '%s'", section);
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
                g_warning ("Context-menu: Failed to evaluate 'is_visible' for section '%s': "
                        "%s -- Skipping", section, err->message);
                g_clear_error (&err);
                g_free (s);
                goto next;
            }
            g_free (s);
        }

        /* determine default is_sensitive */
        if (donna_config_get_string (config, &s, "context_menus/%s/%s/is_sensitive",
                    source, section))
        {
            enum expr expr;

            expr = evaluate (reference, s, &err);
            if (expr == EXPR_INVALID)
            {
                g_warning ("Context-menu: Failed to evaluate 'is_sensitive' for section '%s': "
                        "%s -- Ignoring", section, err->message);
                g_clear_error (&err);
            }
            else
                is_sensitive = expr == EXPR_TRUE;
            g_free (s);
        }

        /* should this section be a container? */
        if (donna_config_get_boolean (config, &b, "context_menus/%s/%s/is_container",
                    source, section) && b)
            children = g_ptr_array_new_with_free_func (g_object_unref);

        /* process items */
        if (!donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
                    "context_menus/%s/%s", source, section))
            goto next;

        for (i = 0; i < arr->len; ++i)
        {
            GPtrArray *triggers = NULL;
            const gchar *item;
            enum type type;
            gchar *name;
            gchar *icon = NULL;
            GdkPixbuf *pixbuf = NULL;
            gchar *fl = NULL;
            GPtrArray *intrefs = NULL;
            gboolean import_from_trigger = FALSE;
            DonnaNode *node_trigger = NULL;
            DonnaNodeHasValue has;
            struct node_internal *ni;
            DonnaNode *node;
            GValue v = G_VALUE_INIT;

            item = arr->pdata[i];

            /* make sure it is visible */
            if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/is_visible",
                        source, section, item))
            {
                enum expr expr;

                expr = evaluate (reference, s, &err);
                if (expr == EXPR_INVALID)
                {
                    g_warning ("Context-menu: Failed to evaluate 'is_visible' "
                            "for item 'context_menus/%s/%s/%s': %s -- Ignoring",
                            source, section, item, err->message);
                    g_clear_error (&err);
                    g_free (s);
                }
                else if (expr == EXPR_FALSE)
                {
                    g_free (s);
                    continue;
                }
                else /* EXPR_TRUE */
                    g_free (s);
            }

            /* find the (matching) trigger */
            if (donna_config_list_options (config, &triggers,
                        DONNA_CONFIG_OPTION_TYPE_OPTION, "context_menus/%s/%s/%s",
                        source, section, item))
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
                    if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/%s",
                                source, section, item, t))
                    {
                        enum expr expr;

                        expr = evaluate (reference, s, &err);
                        if (expr == EXPR_INVALID)
                        {
                            g_warning ("Context-menu: skipping trigger declaration, "
                                    "invalid expression in 'context_menus/%s/%s/%s/%s': %s",
                                    source, section, item, t, err->message);
                            g_clear_error (&err);
                            g_free (s);
                            continue;
                        }
                        else if (expr == EXPR_TRUE)
                        {
                            /* get the actual trigger */
                            if (!donna_config_get_string (config, &fl,
                                        "context_menus/%s/%s/%s/%.*s",
                                        source, section, item, (gint) (len - 5), t))
                            {
                                g_warning ("Context-menu: trigger option missing: "
                                        "'context_menus/%s/%s/%s/%s' -- skipping trigger",
                                        source, section, item, t);
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
            if (!fl && !donna_config_get_string (config, &fl,
                        "context_menus/%s/%s/%s/trigger", source, section, item))
            {
                g_warning ("Context-menu: No trigger found for 'context_menus/%s/%s/%s', "
                        "skipping item", source, section, item);
                continue;
            }

            /* parse %C/%c in the trigger */
            fl = parse_Cc (fl, s_C, s_c);

            /* parse the FL -- it doesn't have to be a command, but it can
             * always include variables */
            fl = donna_app_parse_fl (app, fl, conv_flags, conv_fn, conv_data, &intrefs);

            /* type of item */
            if (donna_config_get_int (config, (gint *) &type,
                        "context_menus/%s/%s/%s/type", source, section, item))
                type = CLAMP (type, TYPE_STANDARD, NB_TYPES - 1);
            else
                type = TYPE_STANDARD;

            if (type == TYPE_TRIGGER)
            {
                node = get_node_trigger (app, fl);
                g_free (fl);
                if (!node)
                {
                    free_intrefs (app, intrefs);
                    g_warning ("Context-menu: Failed to get node for item "
                            "'context_menus/%s/%s/%s' -- Skipping",
                            source, section, item);
                }
                else
                    /* add the node trigger into the menu. It means the intrefs
                     * aren't free-d in this case. Usually it's ok, because if
                     * we put the node trigger directly, it's an actual node
                     * (e.g. not a command) so there were no intrefs. If for
                     * some reason there was, they'll just be free-d
                     * automatically by the donna's GC after a while... */
                    g_ptr_array_add ((children) ? children : nodes, node);

                continue;
            }

            /* shall we import non-specified stuff from node trigger? */
            donna_config_get_boolean (config, &import_from_trigger,
                    "context_menus/%s/%s/%s/import_from_trigger", source, section, item);

            /* item-specific "is_sensitive" */
            if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/is_sensitive",
                        source, section, item))
            {
                enum expr expr;

                expr = evaluate (reference, s, &err);
                if (expr == EXPR_INVALID)
                {
                    g_warning ("Context-menu: Failed to evaluate 'is_sensitive' "
                            "for item 'context_menus/%s/%s/%s': %s -- Ignoring",
                            source, section, item, err->message);
                    g_clear_error (&err);
                }
                else
                    is_sensitive = expr == EXPR_TRUE;
                g_free (s);
            }

            /* is_sensitive is a bit special, in that we only import from
             * trigger if TRUE (since if FALSE, that takes precedence, but if
             * TRUE the value from trigger does) */
            if (is_sensitive && import_from_trigger)
            {
                ensure_node_trigger ();
                if (import_from_trigger)
                {
                    donna_node_get (node_trigger, FALSE, "menu-is-sensitive",
                            &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                            is_sensitive = g_value_get_boolean (&v);
                        g_value_unset (&v);
                    }
                }
            }

            /* name */
            if (!donna_config_get_string (config, &name, "context_menus/%s/%s/%s/name",
                        source, section, item))
            {
                if (import_from_trigger)
                {
                    ensure_node_trigger ();
                    if (import_from_trigger)
                        name = donna_node_get_name (node_trigger);
                }
                else
                    name = g_strdup (fl);
            }

            /* parse %C/%c in the name */
            name = parse_Cc (name, s_C, s_c);

            /* icon */
            donna_config_get_string (config, &icon, "context_menus/%s/%s/%s/icon",
                        source, section, item);
            if (!icon && import_from_trigger)
            {
                ensure_node_trigger ();
                if (import_from_trigger)
                {
                    donna_node_get (node_trigger, FALSE, "icon", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        pixbuf = g_value_dup_object (&v);
                        g_value_unset (&v);
                    }
                }
            }

            if (type == TYPE_CONTAINER)
            {
                if (!node_trigger)
                    node_trigger = get_node_trigger (app, fl);
                if (!node_trigger)
                {
                    g_warning ("Context-menu: Failed to get node for item '%s' "
                            "('context_menus/%s/%s/%s') -- Skipping",
                            name, source, section, item);
                    g_free (fl);
                    free_intrefs (app, intrefs);
                    g_free (name);
                    if (pixbuf)
                        g_object_unref (pixbuf);
                    continue;
                }
                else if (donna_node_get_node_type (node_trigger) != DONNA_NODE_CONTAINER)
                {
                    g_warning ("Context-menu: Node for item '%s' "
                            "('context_menus/%s/%s/%s') isn't a container -- Skipping",
                            name, source, section, item);
                    g_free (fl);
                    free_intrefs (app, intrefs);
                    g_free (name);
                    if (pixbuf)
                        g_object_unref (pixbuf);
                    g_object_unref (node_trigger);
                    continue;
                }
                g_free (fl);
                fl = NULL;
            }

            /* let's create the node */
            ni = g_slice_new (struct node_internal);
            ni->app = app;
            ni->intrefs = intrefs;
            if (type == TYPE_CONTAINER)
                ni->node_trigger = g_object_ref (node_trigger);
            else
                ni->node_trigger = NULL;
            ni->fl = fl;

            node = donna_provider_internal_new_node (pi, name,
                    pixbuf != NULL, (pixbuf) ? pixbuf : (gconstpointer) icon, NULL,
                    (type == TYPE_CONTAINER) ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
                    is_sensitive,
                    (type == TYPE_CONTAINER)
                    /* children_cb is internal, starts a subtask.
                     * node_internal_cb in fast, it only triggers another task */
                    ? DONNA_TASK_VISIBILITY_INTERNAL : DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (type == TYPE_CONTAINER)
                    ? (internal_fn) node_children_cb : (internal_fn) node_internal_cb,
                    ni,
                    (GDestroyNotify) free_node_internal, &err);
            if (G_UNLIKELY (!node))
            {
                g_warning ("Context-menu: Failed to create node "
                        "for 'context_menus/%s/%s/%s': %s",
                        source, section, item, err->message);
                g_clear_error (&err);
                g_free (name);
                g_free (icon);
                if (pixbuf)
                    g_object_unref (pixbuf);
                free_node_internal (ni);
                if (node_trigger)
                    g_object_unref (node_trigger);
                continue;
            }
            g_free (name);
            g_free (icon);
            if (pixbuf)
                g_object_unref (pixbuf);

            if (!donna_config_get_boolean (config, &b,
                        "context_menus/%s/%s/%s/menu_is_label_bold",
                        source, section, item))
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
                            b = g_value_get_boolean (&v);
                            g_value_unset (&v);
                        }
                    }
                }
                else
                    b = FALSE;
            }

            if (b)
            {
                g_value_init (&v, G_TYPE_BOOLEAN);
                g_value_set_boolean (&v, TRUE);
                if (G_UNLIKELY (!donna_node_add_property (node, "menu-is-label-bold",
                                G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true, NULL, &err)))
                {
                    g_warning ("Context-menu: Failed to set label bold for item "
                            "'context_menus/%s/%s/%s': %s",
                            source, section, item, err->message);
                    g_clear_error (&err);
                }
                g_value_unset (&v);
            }

            g_ptr_array_add ((children) ? children : nodes, node);

            if (node_trigger)
                g_object_unref (node_trigger);
        }

        g_ptr_array_unref (arr);

        if (children)
        {
            DonnaNode *node;
            gchar *name;
            gchar *icon = NULL;

            if (!donna_config_get_string (config, &name, "context_menus/%s/%s/name",
                        source, section))
                name = g_strdup (section);

            donna_config_get_string (config, &icon, "context_menus/%s/%s/icon",
                        source, section);

            node = donna_provider_internal_new_node (pi, name, FALSE, icon, NULL,
                    DONNA_NODE_CONTAINER, TRUE, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    (internal_fn) container_children_cb,
                    children,
                    (GDestroyNotify) g_ptr_array_unref, &err);
            if (G_UNLIKELY (!node))
            {
                g_warning ("Context-menu: Failed to create node "
                        "for 'context_menus/%s/%s': %s",
                        source, section, err->message);
                g_clear_error (&err);
                g_ptr_array_unref (children);
            }
            else
                g_ptr_array_add (nodes, node);
            g_free (name);
            g_free (icon);
        }

next:
        if (e)
            *e = ' ';
        if (!end)
            break;
        *end = ',';
        section = end + 1;
    }

    if (sections != _sections)
        g_free (sections);
    g_object_unref (pi);
    g_free (root);
    return nodes;
}
#undef ensure_node_trigger

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
