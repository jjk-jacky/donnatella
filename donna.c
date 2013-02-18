
#include <gtk/gtk.h>
#include "donna.h"
#include "sharedstring.h"
#include "columntype.h"
#include "node.h"

struct _Donna
{
    DonnaConfig *   config;
    get_provider_fn get_provider;
};
/* shared/global pointer */
struct _Donna *donna;

static struct _DonnaPrivate
{
    GSList  *arrangements;
} priv;

struct argmt
{
    const gchar  *name;
    GPatternSpec *pspec;
};

static DonnaProvider *
donna_get_provider (const gchar *domain)
{
    /* TODO */
    return NULL;
}

void
donna_start_internal_task (DonnaTask  *task)
{
    /* TODO */
}

const gchar *
donna_get_arrangement_for_location (DonnaNode *node)
{
    GSList *l;
    DonnaSharedString *location;
    const gchar *domain;
    const gchar *arr;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* get full location of node */
    donna_node_get (node, FALSE, "domain", &domain, "location", &location, NULL);
    s = g_strdup_printf ("%s:%s", domain, donna_shared_string (location));
    donna_shared_string_unref (location);

    arr = NULL;
    for (l = priv.arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        if (g_pattern_match_string (argmt->pspec, s))
        {
            arr = argmt->name;
            break;
        }
    }
    g_free (s);

    return arr;
}

void
donna_free (void)
{
    GSList *l;

    for (l = priv.arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        g_pattern_spec_free (argmt->pspec);
        g_free (argmt);
    }
}

void
donna_init (int *argc, char **argv[])
{
    GPtrArray *arr = NULL;
    guint i;

    /* GTK */
    gtk_init (argc, argv);
    /* register the new fundamental type SharedString */
    donna_shared_string_register ();

    /* set up the object donna */
    donna->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, NULL);
    donna->get_provider = donna_get_provider;

    /* load the config */
    /* TODO */

    /* compile patterns of arrangements' masks */
    if (!donna_config_list_options (donna->config, "arrangements",
                DONNA_CONFIG_OPTION_TYPE_CATEGORY,
                &arr))
    {
        g_warning ("Unable to load arrangements");
        goto skip_arrangements;
    }

    for (i = 0; i < arr->len; ++i)
    {
        struct argmt *argmt;
        DonnaSharedString *ss;
        gchar buf[255];
        const gchar *s;

        /* ignore "tree" and "list" arrangements */
        s = arr->pdata[i];
        if (s[0] < '0' || s[0] > '9')
            continue;

        snprintf (buf, 255, "arrangements/%s/mask", s);
        if (!donna_config_get_shared_string (donna->config, buf, &ss))
        {
            g_warning ("Arrangement '%s' has no mask set, skipping");
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        /* we don't ref ss, we'll just not unref it */
        argmt->name  = s;
        argmt->pspec = g_pattern_spec_new (donna_shared_string (ss));
        priv.arrangements = g_slist_append (priv.arrangements, argmt);
    }
    g_ptr_array_free (arr, TRUE);

skip_arrangements:
    return;
}
