/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * task-process.h
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

#ifndef __DONNA_TASK_PROCESS_H__
#define __DONNA_TASK_PROCESS_H__

#include "task.h"

G_BEGIN_DECLS

#define DONNA_TYPE_TASK_PROCESS             (donna_task_process_get_type ())
#define DONNA_TASK_PROCESS(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASK_PROCESS, DonnaTaskProcess))
#define DONNA_TASK_PROCESS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TASK_PROCESS, BonnaTaskProcessClass))
#define DONNA_IS_TASK_PROCESS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASK_PROCESS))
#define DONNA_IS_TASK_PROCESS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_TASK_PROCESS))
#define DONNA_TASK_PROCESS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TASK_PROCESS, DonnaTaskProcessClass))

typedef struct _DonnaTaskProcess            DonnaTaskProcess;
typedef struct _DonnaTaskProcessClass       DonnaTaskProcessClass;
typedef struct _DonnaTaskProcessPrivate     DonnaTaskProcessPrivate;

#define DONNA_TASK_PROCESS_ERROR            g_quark_from_static_string ("DonnaTaskProcess-Error")
typedef enum
{
    DONNA_TASK_PROCESS_ERROR_NO_CMDLINE,
    DONNA_TASK_PROCESS_ERROR_READ,
    DONNA_TASK_PROCESS_ERROR_OTHER,
} DonnaTaskProcessError;

typedef enum
{
    DONNA_PIPE_OUTPUT = 0,
    DONNA_PIPE_ERROR
} DonnaPipe;

typedef enum
{
    DONNA_TASK_PROCESS_STDIN_DONE = 0,
    DONNA_TASK_PROCESS_STDIN_WAIT_NONBLOCKING,
    DONNA_TASK_PROCESS_STDIN_FAILED
} DonnaTaskProcessStdin;

typedef void                    (*task_init_fn)     (DonnaTaskProcess   *taskp,
                                                     gpointer            data,
                                                     GError            **error);
typedef gboolean                (*task_pauser_fn)   (DonnaTask          *task,
                                                     GPid                pid,
                                                     gpointer            data);
typedef DonnaTaskProcessStdin   (*task_stdin_fn)    (DonnaTask          *task,
                                                     GPid                pid,
                                                     gint                fd,
                                                     gpointer            data);
typedef DonnaTaskState          (*task_closer_fn)   (DonnaTask          *task,
                                                     gint                rc,
                                                     DonnaTaskState      state,
                                                     gpointer            data);

struct _DonnaTaskProcess
{
    DonnaTask parent;

    DonnaTaskProcessPrivate *priv;
};

struct _DonnaTaskProcessClass
{
    DonnaTaskClass parent;

    /* signals */

    void            (*pipe_data_received)           (DonnaTaskProcess   *taskp,
                                                     DonnaPipe           pipe,
                                                     gsize               len,
                                                     const gchar        *str);
    void            (*pipe_new_line)                (DonnaTaskProcess   *taskp,
                                                     DonnaPipe           pipe,
                                                     const gchar        *line);
    void            (*process_started)              (DonnaTaskProcess   *taskp);
    void            (*process_ended)                (DonnaTaskProcess   *taskp);
};

GType               donna_task_process_get_type     (void) G_GNUC_CONST;

DonnaTask *         donna_task_process_new          (const gchar        *workdir,
                                                     const gchar        *cmdline,
                                                     gboolean            wait,
                                                     task_closer_fn      closer,
                                                     gpointer            closer_data,
                                                     GDestroyNotify      closer_destroy);
DonnaTask *         donna_task_process_new_full     (task_init_fn        init,
                                                     gpointer            data,
                                                     GDestroyNotify      destroy,
                                                     gboolean            wait,
                                                     task_pauser_fn      pauser,
                                                     gpointer            pauser_data,
                                                     GDestroyNotify      pauser_destroy,
                                                     task_stdin_fn       stdin_fn,
                                                     gpointer            stdin_data,
                                                     GDestroyNotify      stdin_destroy,
                                                     task_closer_fn      closer,
                                                     gpointer            closer_data,
                                                     GDestroyNotify      closer_destroy);
gboolean            donna_task_process_set_pauser   (DonnaTaskProcess   *taskp,
                                                     task_pauser_fn      pauser,
                                                     gpointer            data,
                                                     GDestroyNotify      destroy);
gboolean            donna_task_process_set_stdin    (DonnaTaskProcess   *taskp,
                                                     task_stdin_fn       fn,
                                                     gpointer            data,
                                                     GDestroyNotify      destroy);
void                donna_task_process_import_environ (DonnaTaskProcess *taskp,
                                                     DonnaApp           *app);
void                donna_task_process_setenv       (DonnaTaskProcess   *taskp,
                                                     const gchar        *variable,
                                                     const gchar        *value,
                                                     gboolean            overwrite);
void                donna_task_process_unsetenv     (DonnaTaskProcess   *taskp,
                                                     const gchar        *variable);
gboolean            donna_task_process_set_default_closer (
                                                     DonnaTaskProcess   *taskp);
gboolean            donna_task_process_set_workdir_to_curdir (
                                                     DonnaTaskProcess   *taskp,
                                                     DonnaApp           *app);
gboolean            donna_task_process_set_ui_msg   (DonnaTaskProcess   *taskp);

G_END_DECLS

#endif /* __DONNA_TASK_PROCESS_H__ */
