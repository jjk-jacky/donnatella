/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * history.h
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

#ifndef __DONNA_HISTORY_H__
#define __DONNA_HISTORY_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _DonnaHistory                    DonnaHistory;

#define DONNA_HISTORY_ERROR                     g_quark_from_static_string ("DonnaHistory-Error")
typedef enum
{
    DONNA_HISTORY_ERROR_OUT_OF_RANGE,
    DONNA_HISTORY_ERROR_INVALID_DIRECTION,
    DONNA_HISTORY_ERROR_NOT_EMPTY,
    DONNA_HISTORY_ERROR_OTHER,
} DonnaHistoryError;

typedef enum
{
    DONNA_HISTORY_BACKWARD  = (1 << 0),
    DONNA_HISTORY_FORWARD   = (1 << 1),

    DONNA_HISTORY_BOTH      = DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD
} DonnaHistoryDirection;


DonnaHistory *      donna_history_new           (guint                   max);
void                donna_history_set_max       (DonnaHistory           *history,
                                                 guint                   max);
guint               donna_history_get_max       (DonnaHistory           *history);
gboolean            donna_history_add_items     (DonnaHistory           *history,
                                                 const gchar           **items,
                                                 GError                **error);
gboolean            donna_history_take_items    (DonnaHistory           *history,
                                                 gchar                 **items,
                                                 GError                **error);
void                donna_history_add_item      (DonnaHistory           *history,
                                                 const gchar            *item);
void                donna_history_take_item     (DonnaHistory           *history,
                                                 gchar                  *item);
const gchar *       donna_history_get_item      (DonnaHistory           *history,
                                                 DonnaHistoryDirection   direction,
                                                 guint                   nb,
                                                 GError                **error);
const gchar *       donna_history_move          (DonnaHistory           *history,
                                                 DonnaHistoryDirection   direction,
                                                 guint                   nb,
                                                 GError                **error);
gchar **            donna_history_get_items     (DonnaHistory           *history,
                                                 DonnaHistoryDirection   direction,
                                                 guint                   nb,
                                                 GError                **error);
void                donna_history_clear         (DonnaHistory           *history,
                                                 DonnaHistoryDirection   direction);
void                donna_history_free          (DonnaHistory           *history);

G_END_DECLS

#endif /* __DONNA_HISTORY_H__ */
