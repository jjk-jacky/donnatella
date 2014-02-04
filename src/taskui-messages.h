/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * taskui-messages.h
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

#ifndef __DONNA_TASKUI_MESSAGES_H__
#define __DONNA_TASKUI_MESSAGES_H__

#include "taskui.h"

G_BEGIN_DECLS

#define DONNA_TYPE_TASKUI_MESSAGES              (donna_task_ui_messages_get_type ())
#define DONNA_TASKUI_MESSAGES(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASKUI_MESSAGES, DonnaTaskUiMessages))
#define DONNA_TASKUI_MESSAGES_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TASKUI_MESSAGES, DonnaTaskUiMessagesClass))
#define DONNA_IS_TASKUI_MESSAGES(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASKUI_MESSAGES))
#define DONNA_IS_TASKUI_MESSAGES_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_TASKUI_MESSAGES))
#define DONNA_TASKUI_MESSAGES_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TASKUI_MESSAGES, DonnaTaskUiMessagesClass))

typedef struct _DonnaTaskUiMessages             DonnaTaskUiMessages;
typedef struct _DonnaTaskUiMessagesClass        DonnaTaskUiMessagesClass;
typedef struct _DonnaTaskUiMessagesPrivate      DonnaTaskUiMessagesPrivate;

struct _DonnaTaskUiMessages
{
    GInitiallyUnowned parent;

    DonnaTaskUiMessagesPrivate *priv;
};

struct _DonnaTaskUiMessagesClass
{
    GInitiallyUnownedClass parent;
};

GType       donna_task_ui_messages_get_type         (void) G_GNUC_CONST;

void        donna_task_ui_messages_add              (DonnaTaskUiMessages    *tui,
                                                     GLogLevelFlags          level,
                                                     const gchar            *message);

G_END_DECLS

#endif /* __DONNA_TASKUI_MESSAGES_H__ */
