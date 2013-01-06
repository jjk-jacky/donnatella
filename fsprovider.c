
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
fsprovider_class_init (FsProviderClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
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
}

static void
fsprovider_provider_init (FmProviderInterface *interface)
{
    interface->get_node = fsprovider_get_node;
    interface->get_content = fsprovider_get_content;
    interface->get_children = fsprovider_get_children;
    interface->remove_node = fsprovider_remove_node;
}

G_DEFINE_TYPE_WITH_CODE (FsProvider, fsprovider, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (TYPE_FMPROVIDER, fsprovider_provider_init))

static void
fsprovider_finalize (GObject *object)
{
    /* chain up */
    G_OBJECT_CLASS (fsprovider_parent_class)->finalize (object);
}

static FmNode *
fsprovider_get_node (FmProvider  *provider,
                     const gchar *location,
                     gboolean     is_container,
                     GError     **error)
{
}

static FmNode **
fsprovider_get_content (FmProvider  *provider,
                        FmNode      *node,
                        GError     **error)
{
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
}
