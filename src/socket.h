/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * socket.h
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

#ifndef __DONNA_SOCKET_H__
#define __DONNA_SOCKET_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * DonnaSocket:
 *
 * Opaque structure of a socket used for the donnatella protocol
 */
typedef struct _DonnaSocket         DonnaSocket;

/**
 * socket_process_fn:
 * @socket: The #DonnaSocket
 * @message: The message to be processed
 * @data: User data specified on donna_socket_new()
 *
 * Function called when a message is received on @socket (handles buffering
 * input until a full message is received).
 *
 * @message is the actual full message to process; or %NULL when @socket is
 * being closed
 */
typedef void (*socket_process_fn)  (DonnaSocket    *socket,
                                    gchar          *message,
                                    gpointer        data);

DonnaSocket *       donna_socket_new            (gint                fd,
                                                 socket_process_fn   process,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
void                donna_socket_unref          (DonnaSocket        *socket);
DonnaSocket *       donna_socket_ref            (DonnaSocket        *socket);
void                donna_socket_close          (DonnaSocket        *socket);
gboolean            donna_socket_send           (DonnaSocket        *socket,
                                                 const gchar        *message,
                                                 gsize               len);

G_END_DECLS

#endif /* __DONNA_SOCKET_H__ */
