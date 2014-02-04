/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * taskui.h
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

#ifndef __DONNA_TASKUI_H__
#define __DONNA_TASKUI_H__

G_BEGIN_DECLS

typedef struct _DonnaTaskUi                 DonnaTaskUi; /* dummy typedef */
typedef struct _DonnaTaskUiInterface        DonnaTaskUiInterface;

#define DONNA_TYPE_TASKUI                   (donna_taskui_get_type ())
#define DONNA_TASKUI(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASKUI, DonnaTaskUi))
#define DONNA_IS_TASKUI(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASKUI))
#define DONNA_TASKUI_GET_INTERFACE(obj)     (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_TASKUI, DonnaTaskUiInterface))

GType           donna_taskui_get_type       (void) G_GNUC_CONST;


struct _DonnaTaskUiInterface
{
    GTypeInterface parent;

    /* virtual table */
    void            (*take_title)           (DonnaTaskUi    *taskui,
                                             gchar          *title);
    void            (*show)                 (DonnaTaskUi    *taskui);
};

void            donna_taskui_set_title      (DonnaTaskUi    *taskui,
                                             const gchar    *title);
void            donna_taskui_take_title     (DonnaTaskUi    *taskui,
                                             gchar          *title);
void            donna_taskui_show           (DonnaTaskUi    *taskui);

G_END_DECLS

#endif /* __DONNA_TASKUI_H__ */
