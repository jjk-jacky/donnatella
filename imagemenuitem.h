
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

/* keep in sync with DonnaContextIconSpecial in contextmenu.h */
typedef enum
{
    DONNA_IMAGE_MENU_ITEM_IS_IMAGE = 0,
    DONNA_IMAGE_MENU_ITEM_IS_CHECK,
    DONNA_IMAGE_MENU_ITEM_IS_RADIO,
    _DONNA_IMAGE_MENU_ITEM_NB_IMAGE_SPECIAL
} DonnaImageMenuItemImageSpecial;

struct _DonnaImageMenuItem
{
    /*< private >*/
    GtkMenuItem                   item;
    DonnaImageMenuItemPrivate    *priv;
};

struct _DonnaImageMenuItemClass
{
    GtkMenuItemClass parent_class;

    void        (*load_submenu)                 (DonnaImageMenuItem     *item,
                                                 gboolean                from_click);
};

GtkWidget * donna_image_menu_item_new_with_label    (const gchar        *label);
void        donna_image_menu_item_set_image         (DonnaImageMenuItem *item,
                                                     GtkWidget          *image);
GtkWidget * donna_image_menu_item_get_image         (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_image_selected(DonnaImageMenuItem *item,
                                                     GtkWidget          *image);
GtkWidget * donna_image_menu_item_get_image_selected(DonnaImageMenuItem *item);
void        donna_image_menu_item_set_image_special (DonnaImageMenuItem *item,
                                                     DonnaImageMenuItemImageSpecial image);
DonnaImageMenuItemImageSpecial donna_image_menu_item_get_image_special (
                                                     DonnaImageMenuItem *item);
void        donna_image_menu_item_set_is_active     (DonnaImageMenuItem *item,
                                                     gboolean            is_active);
gboolean    donna_image_menu_item_get_is_active     (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_is_inconsistent (DonnaImageMenuItem *item,
                                                     gboolean            is_inconsistent);
gboolean    donna_image_menu_item_get_is_inconsistent (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_is_combined   (DonnaImageMenuItem *item,
                                                     gboolean            is_combined);
gboolean    donna_image_menu_item_get_is_combined   (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_is_combined_sensitive (
                                                     DonnaImageMenuItem *item,
                                                     gboolean            is_combined_sensitive);
gboolean    donna_image_menu_item_get_is_combined_sensitive (
                                                     DonnaImageMenuItem *item);
void        donna_image_menu_item_set_label_bold    (DonnaImageMenuItem *item,
                                                     gboolean            is_bold);
gboolean    donna_image_menu_item_get_label_bold    (DonnaImageMenuItem *item);
void        donna_image_menu_item_set_loading_submenu (
                                                     DonnaImageMenuItem   *item,
                                                     const gchar          *label);

G_END_DECLS

#endif /* __DONNA_IMAGE_MENU_ITEM_H__ */
