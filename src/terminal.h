/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * terminal.h
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

#ifndef __DONNA_TERMINAL_H__
#define __DONNA_TERMINAL_H__

#include <gtk/gtk.h>
#include "common.h"

G_BEGIN_DECLS

#define DONNA_TERMINAL_ERROR                g_quark_from_static_string ("DonnaTerminal-Error")
/**
 * DonnaTerminalError:
 * @DONNA_TERMINAL_ERROR_INVALID_CMDLINE: Invalid command line (make sure to use
 * \%w for the window id to use for embedding)
 * @DONNA_TERMINAL_ERROR_NOT_FOUND: Tab/page not found
 * @DONNA_TERMINAL_ERROR_OTHER: Other error
 */
typedef enum
{
    DONNA_TERMINAL_ERROR_INVALID_CMDLINE,
    DONNA_TERMINAL_ERROR_NOT_FOUND,
    DONNA_TERMINAL_ERROR_OTHER,
} DonnaTerminalError;

typedef struct _DonnaTerminal               DonnaTerminal;
typedef struct _DonnaTerminalPrivate        DonnaTerminalPrivate;
typedef struct _DonnaTerminalClass          DonnaTerminalClass;

#define DONNA_TYPE_TERMINAL                 (donna_terminal_get_type ())
#define DONNA_TERMINAL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TERMINAL, DonnaTerminal))
#define DONNA_TERMINAL_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TERMINAL, DonnaTerminalClass))
#define DONNA_IS_TERMINAL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TERMINAL))
#define DONNA_IS_TERMINAL_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TERMINAL))
#define DONNA_TERMINAL_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TERMINAL, DonnaTerminalClass))

GType   donna_terminal_get_type             (void) G_GNUC_CONST;


/**
 * DonnaTerminalAddTab:
 * @DONNA_TERMINAL_NOTHING: Do nothing (Obviously the first tab will be active)
 * @DONNA_TERMINAL_MAKE_ACTIVE: Make the tab the active tab
 * @DONNA_TERMINAL_FOCUS: Make the tab the active tab and focus it
 *
 * What to do when adding a new terminal tab
 */
typedef enum
{
    DONNA_TERMINAL_NOTHING = 0,
    DONNA_TERMINAL_MAKE_ACTIVE,
    DONNA_TERMINAL_FOCUS
} DonnaTerminalAddTab;

struct _DonnaTerminal
{
    /*< private >*/
    GtkNotebook              notebook;
    DonnaTerminalPrivate    *priv;
};

struct _DonnaTerminalClass
{
    GtkNotebookClass parent_class;

    void        (*tab_added)                (DonnaTerminal      *terminal,
                                             guint               id,
                                             gboolean            is_active);
    void        (*tab_removed)              (DonnaTerminal      *terminal,
                                             guint               id);
    void        (*tab_title_changed)        (DonnaTerminal      *terminal,
                                             guint               id,
                                             const gchar        *new_title);
};

GtkWidget *     donna_terminal_new          (DonnaApp           *app,
                                             const gchar        *name);
const gchar *   donna_terminal_get_name     (DonnaTerminal      *terminal);
guint           donna_terminal_get_active_tab (
                                             DonnaTerminal      *terminal);
gint            donna_terminal_get_active_page (
                                             DonnaTerminal      *terminal);
gboolean        donna_terminal_set_active_tab (
                                             DonnaTerminal      *terminal,
                                             guint               id,
                                             gboolean            no_focus,
                                             GError            **error);
gboolean        donna_terminal_set_active_page (
                                             DonnaTerminal      *terminal,
                                             gint                page,
                                             gboolean            no_focus,
                                             GError            **error);
guint           donna_terminal_get_tab      (DonnaTerminal      *terminal,
                                             gint                page,
                                             GError            **error);
gint            donna_terminal_get_page     (DonnaTerminal      *terminal,
                                             guint               id,
                                             GError            **error);
guint           donna_terminal_add_tab      (DonnaTerminal      *terminal,
                                             const gchar        *cmdline,
                                             const gchar        *term_cmdline,
                                             DonnaTerminalAddTab add_tab,
                                             GError            **error);
gboolean        donna_terminal_remove_tab   (DonnaTerminal      *terminal,
                                             guint               id,
                                             GError            **error);
gboolean        donna_terminal_remove_page  (DonnaTerminal      *terminal,
                                             gint                page,
                                             GError            **error);

G_END_DECLS

#endif /* __DONNA_TERMINAL_H__ */
