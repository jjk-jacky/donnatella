/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * contextmenu.h
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#ifndef __DONNA_CONTEXT_MENU_H__
#define __DONNA_CONTEXT_MENU_H__

#include <gtk/gtk.h>
#include "common.h"
#include "context.h"

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
    DonnaNode              *node;

    const gchar            *name;
    union {
        const gchar        *icon_name;
        GIcon              *icon;
    };
    union {
        const gchar        *icon_name_selected;
        GIcon              *icon_selected;
    };
    const gchar            *desc;
    const gchar            *trigger;
    /* container only */
    const gchar            *menu;

    /* to allow adding extra/custom properties on the newly-created node */
    context_new_node_fn     new_node_fn;
    gpointer                new_node_data;
    GDestroyNotify          new_node_destroy;

    DonnaContextIconSpecial icon_special;
    gboolean                icon_is_gicon;
    gboolean                icon_is_gicon_selected;
    gboolean                is_active;
    gboolean                is_inconsistent;

    gboolean                is_visible;
    gboolean                is_sensitive;
    gboolean                is_container;
    gboolean                is_menu_bold;
    /* container only */
    DonnaEnabledTypes       submenus;

    gboolean                free_name;
    gboolean                free_icon;
    gboolean                free_icon_selected;
    gboolean                free_desc;
    gboolean                free_trigger;
    gboolean                free_menu;
} DonnaContextInfo;

typedef GPtrArray * (*get_sel_fn)   (gpointer data, GError **error);

typedef gchar *  (*get_alias_fn)     (const gchar             *alias,
                                      const gchar             *extra,
                                      DonnaContextReference    reference,
                                      DonnaContext            *context,
                                      GError                 **error);
typedef gboolean (*get_item_info_fn) (const gchar             *item,
                                      const gchar             *extra,
                                      DonnaContextReference    reference,
                                      DonnaContext            *context,
                                      DonnaContextInfo        *info,
                                      GError                 **error);

GPtrArray *     donna_context_menu_get_nodes    (DonnaApp               *app,
                                                 gchar                  *items,
                                                 DonnaContextReference   reference,
                                                 const gchar            *source,
                                                 get_alias_fn            get_alias,
                                                 get_item_info_fn        get_item_info,
                                                 DonnaContext           *context,
                                                 GError                **error);

inline gboolean
donna_context_menu_popup (DonnaApp              *app,
                          gchar                 *items,
                          DonnaContextReference  reference,
                          const gchar           *source,
                          get_alias_fn           get_alias,
                          get_item_info_fn       get_item_info,
                          DonnaContext          *context,
                          const gchar           *menu,
                          GError               **error);

G_END_DECLS

#endif /* __DONNA_CONTEXT_MENU_H__ */
