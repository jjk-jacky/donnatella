/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * task-helpers.h
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

#ifndef __DONNA_TASK_HELPERS_H__
#define __DONNA_TASK_HELPERS_H__

#include "task.h"

G_BEGIN_DECLS

typedef struct _DonnaTaskHelper             DonnaTaskHelper;

typedef enum
{
    DONNA_TASK_HELPER_RC_SUCCESS = 0,
    DONNA_TASK_HELPER_RC_CANCELLING,
    DONNA_TASK_HELPER_RC_ERROR
} DonnaTaskHelperRc;

typedef void        (*task_helper_ui)       (DonnaTaskHelper    *th,
                                             gpointer            data);

/* base functions for helpers */
void                donna_task_helper_done  (DonnaTaskHelper    *th);
DonnaTaskHelperRc   donna_task_helper       (DonnaTask          *task,
                                             task_helper_ui      show_ui,
                                             task_helper_ui      destroy_ui,
                                             gpointer            data);

/* actual helpers */

typedef enum
{
    DONNA_TASK_HELPER_ASK_RC_CANCELLING =  0,
    DONNA_TASK_HELPER_ASK_RC_ERROR      = -1,
    DONNA_TASK_HELPER_ASK_RC_NO_ANSWER  = -2
} DonnaTaskHelperAskRc;


gint        donna_task_helper_ask           (DonnaTask          *task,
                                             const gchar        *question,
                                             const gchar        *details,
                                             gboolean            details_markup,
                                             gint                btn_default,
                                             const gchar        *btn_label,
                                             ...);

G_END_DECLS

#endif /* __DONNA_TASK_HELPERS_H__ */
