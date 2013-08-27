
#include "contextmenu.h"
#include "conf.h"
#include "provider-internal.h"
#include "node.h"
#include "macros.h"
#include "debug.h"

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

static void
free_arr_nodes (DonnaNode *node)
{
    if (node)
        g_object_unref (node);
}

struct node_trigger
{
    DonnaApp *app;
    GPtrArray *intrefs;
    gchar *fl;
};

static void
free_nt (struct node_trigger *nt)
{
    if (nt->intrefs)
    {
        guint i;

        for (i = 0; i < nt->intrefs->len; ++i)
            donna_app_free_int_ref (nt->app, nt->intrefs->pdata[i]);
        g_ptr_array_unref (nt->intrefs);
    }
    g_free (nt->fl);
    g_slice_free (struct node_trigger, nt);
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
node_trigger_cb (DonnaTask *task, DonnaNode *node, struct node_trigger *nt)
{
    GError *err = NULL;

    if (!donna_app_trigger_fl (nt->app, nt->fl, nt->intrefs, FALSE, &err))
    {
        struct err *e;

        e = g_new (struct err, 1);
        e->app = nt->app;
        e->err = err;
        g_main_context_invoke (NULL, (GSourceFunc) show_error, e);
    }
    else
        /* because trigger_fl handled them for us */
        nt->intrefs = NULL;

    free_nt (nt);
    return DONNA_TASK_DONE;
}

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
    nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) free_arr_nodes);
    section = sections;
    for (;;)
    {
        GPtrArray *arr = NULL;
        guint i;
        gchar *s;
        gchar *end, *e;
        gboolean is_sensitive = TRUE;

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

            arr = get_section_nodes (section, reference, conv_data, &err);
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
                memcpy (nodes->pdata[pos], arr->pdata[0], sizeof (gpointer) * arr->len);
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

        /* process items */
        if (!donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
                    "context_menus/%s/%s", source, section))
            goto next;

        for (i = 0; i < arr->len; ++i)
        {
            GPtrArray *triggers = NULL;
            const gchar *item;
            gchar *name;
            GdkPixbuf *icon = NULL;
            gchar *fl = NULL;
            struct node_trigger *nt;
            DonnaNode *node;

            item = arr->pdata[i];

            /* item name */
            if (!donna_config_get_string (config, &name, "context_menus/%s/%s/%s/name",
                        source, section, item))
            {
                g_warning ("Context-menu: Item in 'context_menus/%s/%s/%s' "
                        "doesn't have a name, skipping",
                        source, section, item);
                continue;
            }

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
                }
                else if (expr == EXPR_FALSE)
                {
                    g_free (s);
                    continue;
                }
                else /* EXPR_TRUE */
                    g_free (s);
            }

            /* find a matching trigger */
            if (donna_config_list_options (config, &triggers, DONNA_CONFIG_OPTION_TYPE_OPTION,
                        "context_menus/%s/%s/%s", source, section, item))
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

            if (!fl && !donna_config_get_string (config, &fl,
                        "context_menus/%s/%s/%s/trigger", source, section, item))
            {
                g_warning ("Context-menu: No trigger found for 'context_menus/%s/%s/%s', "
                        "skipping item", source, section, item);
                g_free (name);
                continue;
            }

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

            if (donna_config_get_string (config, &s, "context_menus/%s/%s/%s/icon",
                        source, section, item))
            {
                icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                        s, /*FIXME*/16, 0, NULL);
                g_free (s);
            }

            nt = g_slice_new (struct node_trigger);
            nt->app = app;
            nt->intrefs = NULL;
            nt->fl = donna_app_parse_fl (app, fl, conv_flags, conv_fn, conv_data,
                    &nt->intrefs);

            node = donna_provider_internal_new_node (pi, name, icon, NULL,
                    (internal_worker_fn) node_trigger_cb, nt,
                    (GDestroyNotify) free_nt, &err);
            if (G_UNLIKELY (!node))
            {
                g_warning ("Context-menu: Failed to create node "
                        "for 'context_menus/%s/%s/%s': %s",
                        source, section, item, err->message);
                g_clear_error (&err);
                g_free (name);
                if (icon)
                    g_object_unref (icon);
                free_nt (nt);
                continue;
            }
            g_free (name);
            if (icon)
                g_object_unref (icon);

            if (!is_sensitive)
            {
                /* TODO */
            }

            g_ptr_array_add (nodes, node);
        }

        g_ptr_array_unref (arr);


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
