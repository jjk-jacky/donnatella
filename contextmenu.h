
#ifndef __DONNA_CONTEXT_MENU_H__
#define __DONNA_CONTEXT_MENU_H__

#include <glib-object.h>
#include "app.h"

G_BEGIN_DECLS

#define DONNA_CONTEXT_MENU_ERROR        g_quark_from_static_string ("DonnaContextMenu-Error")
typedef enum
{
    DONNA_CONTEXT_MENU_ERROR_NO_SECTIONS,
    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_SECTION,
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

typedef GPtrArray * (*get_section_nodes_fn) (const gchar            *section,
                                             DonnaContextReference   reference,
                                             gpointer                conv_data,
                                             GError                **error);


GPtrArray *     donna_context_menu_get_nodes_v  (DonnaApp               *app,
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
                                                 va_list                 va_args);
GPtrArray *     donna_context_menu_get_nodes    (DonnaApp               *app,
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
                                                 ...);
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
                          ...);

G_END_DECLS

#endif /* __DONNA_CONTEXT_MENU_H__ */