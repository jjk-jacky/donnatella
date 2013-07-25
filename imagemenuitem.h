
#ifndef __DONNA_IMAGE_MENU_ITEM_H__
#define __DONNA_IMAGE_MENU_ITEM_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _DonnaImageMenuItem              DonnaImageMenuItem;
typedef struct _DonnaImageMenuItemPrivate       DonnaImageMenuItemPrivate;
typedef struct _DonnaImageMenuItemClass         DonnaImageMenuItemClass;

#define DONNA_TYPE_IMAGE_MENU_ITEM              (donna_image_menu_item_get_type ())
#define DONNA_IMAGE_MENU_ITEM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_IMAGE_MENU_ITEM, DonnaImageMenuItem))
#define DONNA_IMAGE_MENU_ITEM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_IMAGE_MENU_ITEM, DonnaImageMenuItemClass))
#define DONNA_IS_IMAGE_MENU_ITEM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_IMAGE_MENU_ITEM))
#define DONNA_IS_IMAGE_MENU_ITEM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_IMAGE_MENU_ITEM))
#define DONNA_IMAGE_MENU_ITEM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_IMAGE_MENU_ITEM, DonnaImageMenuItemClass))

GType   donna_image_menu_item_get_type          (void) G_GNUC_CONST;

struct _DonnaImageMenuItem
{
    /*< private >*/
    GtkImageMenuItem              item;
    DonnaImageMenuItemPrivate    *priv;
};

struct _DonnaImageMenuItemClass
{
    GtkImageMenuItemClass parent_class;

    void        (*load_submenu)                 (DonnaImageMenuItem     *item,
                                                 gboolean                from_click);
};

GtkWidget * donna_image_menu_item_new_with_label    (const gchar        *label);
void        donna_image_menu_item_set_is_combined   (DonnaImageMenuItem *item,
                                                     gboolean            combined);
gboolean    donna_image_menu_item_get_is_combined   (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_loading_submenu (
                                                     DonnaImageMenuItem   *item,
                                                     const gchar          *label);

G_END_DECLS

#endif /* __DONNA_IMAGE_MENU_ITEM_H__ */
