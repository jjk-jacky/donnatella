
#ifndef __DONNA_CONTEXT_MENU_H__
#define __DONNA_CONTEXT_MENU_H__

#include <gtk/gtk.h>
#include "common.h"

G_BEGIN_DECLS

#define DONNA_CONTEXT_MENU_ERROR        g_quark_from_static_string ("DonnaContextMenu-Error")
typedef enum
{
    DONNA_CONTEXT_MENU_ERROR_NO_SECTIONS,
    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
    DONNA_CONTEXT_MENU_ERROR_INVALID_SYNTAX,
    DONNA_CONTEXT_MENU_ERROR_OTHER,
} DonnaContextMenuError;


typedef enum
{
    DONNA_CONTEXT_REF_SELECTED      = (1 << 0),
    DONNA_CONTEXT_REF_NOT_SELECTED  = (1 << 1),

    DONNA_CONTEXT_HAS_SELECTION     = (1 << 2),

    DONNA_CONTEXT_HAS_REF           = (DONNA_CONTEXT_REF_SELECTED
            | DONNA_CONTEXT_REF_NOT_SELECTED)
} DonnaContextReference;

/* keep in sync with DonnaImageMenuItemImageSpecial in imagemenuitem.h */
typedef enum
{
    DONNA_CONTEXT_ICON_IS_IMAGE = 0,
    DONNA_CONTEXT_ICON_IS_CHECK,
    DONNA_CONTEXT_ICON_IS_RADIO,
} DonnaContextIconSpecial;

typedef void (*context_new_node_fn) (DonnaNode *node, gpointer data);

typedef struct
{
    DonnaNode   *node;

    gchar       *name;
    union {
        gchar       *icon_name;
        GdkPixbuf   *pixbuf;
    };
    union {
        gchar       *icon_name_selected;
        GdkPixbuf   *pixbuf_selected;
    };
    gchar       *desc;
    gchar       *trigger;
    /* container only */
    gchar       *menu;

    /* to allow adding extra/custom properties on the newly-created node */
    context_new_node_fn new_node_fn;
    gpointer            new_node_data;
    GDestroyNotify      new_node_destroy;

    guint        icon_special       : 2; /* DonnaContextIconSpecial */
    guint        icon_is_pixbuf     : 1;
    guint        icon_is_pixbuf_selected : 1;
    guint        is_active          : 1;
    guint        is_inconsistent    : 1;

    guint        is_visible         : 1;
    guint        is_sensitive       : 1;
    guint        is_container       : 1;
    guint        is_menu_bold       : 1;
    /* container only */
    guint        submenus           : 2;

    guint        free_name          : 1;
    guint        free_icon          : 1;
    guint        free_icon_selected : 1;
    guint        free_desc          : 1;
    guint        free_trigger       : 1;
    guint        free_menu          : 1;
} DonnaContextInfo;

typedef GPtrArray * (*get_sel_fn)   (gpointer data, GError **error);

typedef gchar *  (*get_alias_fn)     (const gchar             *alias,
                                      const gchar             *extra,
                                      DonnaContextReference    reference,
                                      const gchar             *conv_flags,
                                      conv_flag_fn             conv_fn,
                                      gpointer                 conv_data,
                                      GError                 **error);
typedef gboolean (*get_item_info_fn) (const gchar             *item,
                                      const gchar             *extra,
                                      DonnaContextReference    reference,
                                      const gchar             *conv_flags,
                                      conv_flag_fn             conv_fn,
                                      gpointer                 conv_data,
                                      DonnaContextInfo        *info,
                                      GError                 **error);

GPtrArray *     donna_context_menu_get_nodes    (DonnaApp               *app,
                                                 gchar                  *items,
                                                 DonnaContextReference   reference,
                                                 const gchar            *source,
                                                 get_alias_fn            get_alias,
                                                 get_item_info_fn        get_item_info,
                                                 const gchar            *conv_flags,
                                                 conv_flag_fn            conv_fn,
                                                 gpointer                conv_data,
                                                 GError                **error);

inline gboolean
donna_context_menu_popup (DonnaApp              *app,
                          gchar                 *items,
                          DonnaContextReference  reference,
                          const gchar           *source,
                          get_alias_fn           get_alias,
                          get_item_info_fn       get_item_info,
                          const gchar           *conv_flags,
                          conv_flag_fn           conv_fn,
                          gpointer               conv_data,
                          const gchar           *menu,
                          GError               **error);

G_END_DECLS

#endif /* __DONNA_CONTEXT_MENU_H__ */
