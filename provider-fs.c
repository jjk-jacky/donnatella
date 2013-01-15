
#include <glib-object.h>
#include <string.h>                 /* strrchr() */
#include "provider-fs.h"
#include "provider-base.h"
#include "provider.h"
#include "node.h"
#include "task.h"

struct _DonnaProviderFsPrivate
{
};

static void             provider_fs_finalize        (GObject *object);

/* DonnaProviderBase */
static DonnaTaskState   provider_fs_new_node        (DonnaProvider  *provider,
                                                     DonnaTask      *task,
                                                     const gchar    *location);
static DonnaTaskState   provider_fs_get_content     (DonnaProvider  *provider,
                                                     DonnaTask      *task,
                                                     DonnaNode      *node);
static DonnaTaskState   provider_fs_get_children    (DonnaProvider  *provider,
                                                     DonnaTask      *task,
                                                     DonnaNode      *node);
static DonnaTaskState   provider_fs_remove_node     (DonnaProvider  *provider,
                                                     DonnaTask      *task,
                                                     DonnaNode      *node);


static void
donna_provider_fs_class_init (DonnaProviderFsClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node = provider_fs_new_node;
    pb_class->get_children = provider_fs_get_children;

    o_class = (GObjectClass *) klass;
    o_class->finalize = provider_fs_finalize;

//    g_type_class_add_private (klass, sizeof (DonnaProviderFsPrivate));
}

static void
donna_provider_fs_init (DonnaProviderFs *provider)
{
    return;
    DonnaProviderFsPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_FS,
            DonnaProviderFsPrivate);
}

G_DEFINE_TYPE (DonnaProviderFs, donna_provider_fs, DONNA_TYPE_PROVIDER_BASE)

static void
provider_fs_finalize (GObject *object)
{
//    DonnaProviderFsPrivate *priv;

//    priv = DONNA_PROVIDER_FS (object)->priv;

    /* chain up */
    G_OBJECT_CLASS (donna_provider_fs_parent_class)->finalize (object);
}

static gboolean
location_get (DonnaTask *task, DonnaNode *node)
{
    /* TODO: implement this */
    return TRUE;
}

static gboolean
name_get (DonnaTask *task, DonnaNode *node)
{
    /* TODO: implement this */
    return TRUE;
}

static gboolean
is_container_get (DonnaTask *task, DonnaNode *node)
{
    /* TODO: implement this */
    return TRUE;
}

static gboolean
has_children_get (DonnaTask *task, DonnaNode *node)
{
    GError      *err = NULL;
    GDir        *dir;
    const gchar *location;
    const gchar *name;
    gchar        buf[1024];
    GValue       value = G_VALUE_INIT;

    donna_node_get (node, "location", &location, NULL);
    dir = g_dir_open (location, 0, &err);
    if (err)
    {
        donna_task_take_error (task, err);
        return FALSE;
    }

    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, FALSE);
    while ((name = g_dir_read_name (dir)))
    {
        gchar *s;

        if (donna_task_is_cancelling (task))
        {
            g_dir_close (dir);
            return FALSE;
        }

        s = NULL;
        if (g_snprintf (buf, 1024, "%s/%s", location, name) >= 1024)
            s = g_strdup_printf ("%s/%s", location, name);
        if (g_file_test ((s) ? s : buf, G_FILE_TEST_IS_DIR))
        {
            g_value_set_boolean (&value, TRUE);
            if (s)
                g_free (s);
            break;
        }
        if (s)
            g_free (s);
    }
    g_dir_close (dir);
    donna_node_set_property_value (node, "has_children", &value);
    return TRUE;
}

static DonnaTaskState
provider_fs_new_node (DonnaProvider  *provider,
                      DonnaTask      *task,
                      const gchar    *location)
{
    DonnaNode   *node;
    GValue      *value;
    const gchar *name;
    gboolean     is_container;

    if (!g_file_test (location, G_FILE_TEST_EXISTS))
        /* FIXME: set task error */
        return DONNA_TASK_FAILED;

    is_container = g_file_test (location, G_FILE_TEST_IS_DIR);

    /* FIXME: g_display_name or something... */
    name = strrchr (location, '/');
    /* unless this is root ("/") we go past the / for display */
    if (name[1] != '\0')
        ++name;

    node = donna_node_new (provider, location, location_get, NULL,
            name, name_get, NULL,
            is_container, is_container_get, NULL,
            has_children_get, NULL);

    DONNA_PROVIDER_BASE_GET_CLASS (provider)->add_node_to_cache (
            DONNA_PROVIDER_BASE (provider), node);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    /* take_object to not increment the ref count, as it was already done for
     * this task when adding it to the cache (add_node_to_cache) */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_fs_get_content (DonnaProvider  *provider,
                         DonnaTask      *task,
                         DonnaNode      *node)
{
}

static DonnaTaskState
provider_fs_get_children (DonnaProvider  *provider,
                          DonnaTask      *task,
                          DonnaNode      *node)
{
}

static DonnaTaskState
provider_fs_remove_node (DonnaProvider  *provider,
                         DonnaTask      *task,
                         DonnaNode      *node)
{
}

DonnaProviderFs *
donna_provider_fs_new (void)
{
    return DONNA_PROVIDER_FS (g_object_new (DONNA_TYPE_PROVIDER_FS,
                "domain", "fs",
                NULL));
}
