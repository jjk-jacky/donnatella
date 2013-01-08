
typedef struct _FmNode  FmNode;
#define TYPE_FMNODE             (G_TYPE_OBJECT)
#define FMNODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FMNODE, FmNode))
#define FMNODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FMNODE, FmNodeClass))
#define IS_FMNODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FMNODE))
#define IS_FMNODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FMNODE))
#define FMNODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FMNODE, FmNodeClass))

#include <gtk/gtk.h>
#include "fmprovider.h"
#include "fsprovider.h"

struct _FsProviderPrivate
{
    GHashTable  *nodes;
    GRWLock      nodes_lock;
};

static void             fsprovider_finalize             (GObject *object);

/* FmProvider */
static FmNode *         fsprovider_get_node             (FmProvider  *provider,
                                                         const gchar *location,
                                                         gboolean     is_container,
                                                         GError     **error);
static FmNode **        fsprovider_get_content          (FmProvider  *provider,
                                                         FmNode      *node,
                                                         GError     **error);
static FmNode **        fsprovider_get_children         (FmProvider  *provider,
                                                         FmNode      *node,
                                                         GError     **error);
static gboolean         fsprovider_remove_node          (FmProvider  *provider,
                                                         FmNode      *node,
                                                         GError     **error);

static void
fsprovider_provider_init (FmProviderInterface *interface)
{
    interface->get_node = fsprovider_get_node;
    interface->get_content = fsprovider_get_content;
    interface->get_children = fsprovider_get_children;
    interface->remove_node = fsprovider_remove_node;
}

static void
fsprovider_class_init (FsProviderClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    /* TODO: implement property "domain" */
    o_class->finalize = fsprovider_finalize;

    g_type_class_add_private (klass, sizeof (FsProviderPrivate));
}

static void
fsprovider_init (FsProvider *provider)
{
    FsProviderPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            TYPE_FSPROVIDER,
            FsProviderPrivate);
    priv->nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, g_object_unref);
    g_rw_lock_init (&priv->nodes_lock);
}

G_DEFINE_TYPE_WITH_CODE (FsProvider, fsprovider, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (TYPE_FMPROVIDER, fsprovider_provider_init))

static void
fsprovider_finalize (GObject *object)
{
    FsProviderPrivate *priv;

    priv = FSPROVIDER (object)->priv;

    g_hash_table_destroy (priv->nodes);
    g_rw_lock_clear (&priv->nodes_lock);

    /* chain up */
    G_OBJECT_CLASS (fsprovider_parent_class)->finalize (object);
}

static FmNode *
fsprovider_get_node (FmProvider  *provider,
                     const gchar *location,
                     gboolean     container_only,
                     GError     **error)
{
    FsProvider *p = (FsProvider *) provider;
    FsProviderPrivate *priv;
    FmNode *node;

    g_return_val_if_fail (IS_FSPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    priv = p->priv;
    g_rw_lock_reader_lock (&priv->nodes_lock);
    node = g_hash_table_lookup (priv->nodes, location);
    if (node)
    {
        /* add a reference for our caller */
        g_object_ref (node);
        g_rw_lock_reader_unlock (&priv->nodes_lock);
        return node;
    }

}

static FmNode **
fsprovider_get_content (FmProvider  *provider,
                        FmNode      *node,
                        GError     **error)
{
    return NULL;
}

static FmNode **
fsprovider_get_children (FmProvider  *provider,
                         FmNode      *node,
                         GError     **error)
{
}

static gboolean
fsprovider_remove_node (FmProvider  *provider,
                        FmNode      *node,
                        GError     **error)
{
    return FALSE;
}
