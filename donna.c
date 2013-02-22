
#include <gtk/gtk.h>
#include "donna.h"
#include "sharedstring.h"
#include "provider-config.h"
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "macros.h"

enum
{
    COL_TYPE_NAME = 0,
    NB_COL_TYPES
};

static struct
{
    DonnaConfig *config;
    GSList      *arrangements;
    struct col_type
    {
        const gchar           *name;
        column_type_loader_fn  load;
        DonnaColumnType       *ct;
    } column_types[NB_COL_TYPES];
} priv;

struct argmt
{
    DonnaSharedString   *name;
    GPatternSpec        *pspec;
};

static void
run_task (DonnaTask *task)
{
    g_thread_unref (g_thread_new ("run-task", (GThreadFunc) donna_task_run, task));
}

static DonnaSharedString *
get_arrangement (DonnaNode *node)
{
    GSList *l;
    DonnaSharedString *location;
    const gchar *domain;
    DonnaSharedString *arr;
    gchar  buf[255];
    gchar *b = buf;
    gsize  len;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* get full location of node */
    donna_node_get (node, FALSE, "domain", &domain, "location", &location, NULL);
    len = snprintf (buf, 255, "%s:%s", domain, donna_shared_string (location));
    if (len >= 255)
        b = g_strdup_printf ("%s:%s", domain, donna_shared_string (location));
    donna_shared_string_unref (location);

    arr = NULL;
    for (l = priv.arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        if (g_pattern_match_string (argmt->pspec, b))
        {
            arr = donna_shared_string_ref (argmt->name);
            break;
        }
    }
    if (b != buf)
        g_free (b);

    return arr;
}

static DonnaColumnType *
get_column_type (const gchar *type)
{
    gint i;

    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv.column_types[i].name))
        {
            if (!priv.column_types[i].ct)
                priv.column_types[i].ct = priv.column_types[i].load (priv.config);
            break;
        }
    }
    return (i < NB_COL_TYPES) ? g_object_ref (priv.column_types[i].ct) : NULL;
}

void
donna_free (void)
{
    GSList *l;

    for (l = priv.arrangements; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        donna_shared_string_unref (argmt->name);
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

    memset (&priv, 0, sizeof (priv));
    priv.config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, NULL);
    priv.column_types[COL_TYPE_NAME].name = "name";
    priv.column_types[COL_TYPE_NAME].load = donna_column_type_name_new;

    /* load the config */
    /* TODO */

    /* compile patterns of arrangements' masks */
    if (!donna_config_list_options (priv.config, &arr,
                DONNA_CONFIG_OPTION_TYPE_CATEGORY,
                "arrangements"))
    {
        g_warning ("Unable to load arrangements");
        goto skip_arrangements;
    }

    for (i = 0; i < arr->len; ++i)
    {
        struct argmt *argmt;
        DonnaSharedString *ss;
        const gchar *s;

        /* ignore "tree" and "list" arrangements */
        s = arr->pdata[i];
        if (s[0] < '0' || s[0] > '9')
            continue;

        if (!donna_config_get_shared_string (priv.config, &ss,
                    "arrangements/%s/mask",
                    s))
        {
            g_warning ("Arrangement '%s' has no mask set, skipping", s);
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        argmt->name  = donna_shared_string_new_dup (s);
        argmt->pspec = g_pattern_spec_new (donna_shared_string (ss));
        priv.arrangements = g_slist_append (priv.arrangements, argmt);
        donna_shared_string_unref (ss);
    }
    g_ptr_array_free (arr, TRUE);

skip_arrangements:
    return;
}
