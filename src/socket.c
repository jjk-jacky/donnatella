/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * socket.c
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

#include "config.h"

#include <glib-unix.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "socket.h"
#include "util.h"

/**
 * @SECTION:socket
 * @Short_description: Wrapper to handle socket connection of the donnatella
 * protocol
 *
 * donnatella is extremely customizable, but not scriptable. This is by design,
 * as there's no point in adding/writing yet another scripting language/parser
 * when so many powerful ones exists.
 *
 * Instead, one can use its favorite scripting (or not) language of choice, and
 * simply communicate with donnatella via its socket. Any process started from
 * donnatella (e.g. via command exec() or domain 'exec') will have an
 * environment variable `DONNATELLA_SOCKET` set to the full filename of the
 * socket for donnatella.
 *
 * #DonnaSocket is a wrapper that handles buffering reading from/writing to a
 * socket, as well as protocol syntax, leaving simply handling of messages to be
 * done.
 *
 * The way communication works via socket is that the first byte received must
 * be a character between '1' and '9' (both included), optionally followed by
 * one or more characters between '0' and '8' (both included), and a colon
 * (':').
 *
 * The number before the colon is the size, in bytes, of the following message.
 * As soon as the specified amount of bytes have been received the message is
 * complete and can be processed (socket_process_fn will be called).
 *
 * Using donna_socket_send() you only specify the actual message, it will
 * automatically be prefixed with its size and colon. Similarly, the message
 * sent to socket_process_fn is only the actual message, excluding the length
 * prefix.
 *
 * Creating the actual socket is up to the caller, specifying the file
 * descriptor to donna_socket_new(). #DonnaSocket is reference counted, but it
 * is NOT multithread safe, and should only be used from the main thread/default
 * main context.
 *
 * A source is added to the default main context to receive data from the
 * socket, and once a full message is received, socket_process_fn is called. If
 * anything invalid is received (e.g. not starting with a valid length prefix)
 * the socket will be closed.
 * To manually close the socket, use donna_socket_close()
 */

struct _DonnaSocket
{
    /* ref count; not atomic since always/only used in main thread */
    guint ref;
    /* socket; -1 on connection broken (e.g. after error) */
    gint fd;
    /* buffer for reading */
    GString *str_in;
    /* buffer for writing */
    GString *str_out;
    /* source id to read */
    guint sid_in;
    /* source id to write */
    guint sid_out;
    /* source id on error/hup */
    guint sid_err;
    /* processing message */
    socket_process_fn process;
    gpointer data;
    GDestroyNotify destroy;
    /* this is used to avoid re-entrancy in socket_received(): Every function on
     * a DonnaSocket must be made from main thread(/context), but if
     * socket->process() was to start a new main loop, it could then process a
     * new source socket_received() (or a socket_incoming() which creates a new
     * one and then process it).
     * A new socket_incoming() is fine since it only adds to str_in, nothing bad
     * will happen. But a new socket_received() would start processing the same
     * data/message again... and we don't want that.
     * We could make it work, but it seems useless complications for rare cases,
     * so let's keep using str_in->str to socket_process_fn and only support
     * processing one message at a time; Besides processing should always be
     * fast, worst case being a new task started from an idle source (to make
     * sure not to block this source, in case a new main loop was started from
     * the task running right away in current thread) */
    gboolean in_received;
};

static gboolean
socket_received (DonnaSocket *socket)
{
    gchar c;
    gchar *s;
    gchar *e;
    guint64 len;

    if (G_UNLIKELY (socket->in_received))
        return G_SOURCE_REMOVE;
    socket->in_received = TRUE;

    if (G_UNLIKELY (!socket->str_in || socket->str_in->len == 0))
    {
        socket->in_received = FALSE;
        return G_SOURCE_REMOVE;
    }

    if (*socket->str_in->str < '1' || *socket->str_in->str > '9')
    {
        g_warning ("Socket %d: invalid data received, closing connection",
                socket->fd);
        donna_socket_close (socket);
        socket->in_received = FALSE;
        return G_SOURCE_REMOVE;
    }
    for (s = socket->str_in->str + 1; *s >= '0' && *s <= '9'; ++s)
        ;
    if (*s != ':')
    {
        g_warning ("Socket %d: invalid data received, closing connection",
                socket->fd);
        donna_socket_close (socket);
        socket->in_received = FALSE;
        return G_SOURCE_REMOVE;
    }

    len = g_ascii_strtoull (socket->str_in->str, &e, 10);
    if (G_UNLIKELY (len == 0 || e != s))
    {
        g_warning ("Socket %d: invalid size, closing connection",
                socket->fd);
        donna_socket_close (socket);
        socket->in_received = FALSE;
        return G_SOURCE_REMOVE;
    }

    /* beginning of message */
    ++s;
    /* is the full message there yet? */
    if (socket->str_in->len < (gsize) (s - socket->str_in->str) + len)
    {
        socket->in_received = FALSE;
        return G_SOURCE_REMOVE;
    }
    /* end of message (char after the last char in message, ok since GString
     * always add a NUL-terminating byte) */
    e = s + len;

    c = *e;
    if (*e != '\0')
        /* already more data in buffer, let's NUL-terminate the message for
         * processing */
        *e = '\0';

    socket->process (socket, s, socket->data);

    /* don't overwrite if it was NUL, because more data could have been added to
     * the buffer (if socket->process() started a new main loop, etc) */
    if (c != '\0')
        *e = c;

    g_string_erase (socket->str_in, 0, (gssize) (e - socket->str_in->str));

    socket->in_received = FALSE;
    return (socket->str_in->len > 0) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static gboolean
socket_incoming (gint fd, GIOCondition condition, DonnaSocket *socket)
{
    gssize got;
    gsize len;

    if (G_UNLIKELY (g_source_is_destroyed (g_main_current_source ())))
        return G_SOURCE_REMOVE;

    if (!socket->str_in)
        socket->str_in = g_string_sized_new (1024);

    /* allocated_len always has 1 more char for terminating NUL */
    len = socket->str_in->allocated_len - 1 - socket->str_in->len;
    if (len == 0)
    {
        len = 1024;
        g_string_set_size (socket->str_in, socket->str_in->len + len);
        g_string_truncate (socket->str_in, socket->str_in->len - len);
    }

again:
    got = read (socket->fd, socket->str_in->str + socket->str_in->len, len);
    if (got < 0)
    {
        if (errno == EINTR)
            goto again;
        else if (errno == EAGAIN)
            return G_SOURCE_CONTINUE;

        socket->sid_in = 0;
        donna_socket_close (socket);
        return G_SOURCE_REMOVE;
    }
    /* update GString */
    socket->str_in->len += (gsize) got;
    /* GString are always NUL-terminated (w/ extra char allocated for it) */
    socket->str_in->str[socket->str_in->len] = '\0';

    /* this will (try to) process str_in */
    g_idle_add_full (G_PRIORITY_DEFAULT, (GSourceFunc) socket_received,
            donna_socket_ref (socket), (GDestroyNotify) donna_socket_unref);

    return G_SOURCE_CONTINUE;
}

static gboolean
socket_close (gint fd, GIOCondition condition, DonnaSocket *socket)
{
    socket->sid_err = 0;
    donna_socket_close (socket);
    return G_SOURCE_REMOVE;
}

/**
 * donna_socket_new:
 * @fd: File descriptor of the socket to use
 * @process: Function called to process received messages, and when socket is
 * closed
 * @data: (allow-none): User-data for @process
 * @destroy: (allow-none): Function to free @data when the #DonnaSocket is
 * free-d
 *
 * Returns a #DonnaSocket wrapper to communicate with donnatella, either from
 * server (donna) or client (script) side.
 *
 * @fd must be a connected socket. A source (in default main context) will be
 * added to process incoming data and close it on error. @process is called
 * when a message was received, or (with %NULL as message) when the socket is
 * closed.
 *
 * Returns: (transfer full): A new #DonnaSocket for @fd. Use
 * donna_socket_unref() when done.
 */
DonnaSocket *
donna_socket_new (gint               fd,
                  socket_process_fn  process,
                  gpointer           data,
                  GDestroyNotify     destroy)
{
    DonnaSocket *socket;

    socket = g_slice_new0 (DonnaSocket);
    socket->fd      = fd;
    socket->ref     = 1;
    socket->process = process;
    socket->data    = data;
    socket->destroy = destroy;

    socket->sid_in = g_unix_fd_add_full (G_PRIORITY_DEFAULT,
            socket->fd, G_IO_IN,
            (GUnixFDSourceFunc) socket_incoming,
            donna_socket_ref (socket),
            (GDestroyNotify) donna_socket_unref);

    socket->sid_err = g_unix_fd_add_full (G_PRIORITY_DEFAULT,
            socket->fd, G_IO_ERR | G_IO_HUP,
            (GUnixFDSourceFunc) socket_close,
            donna_socket_ref (socket),
            (GDestroyNotify) donna_socket_unref);

    return socket;
}

/**
 * donna_socket_unref:
 * @socket: A #DonnaSocket
 *
 * Removes a reference from @socket. If it was the last, the socket is closed
 * (if it wasn't already) and memory is freed.
 *
 * Note that if the socket wasn't closed, socket_process_fn will not be called.
 */
void
donna_socket_unref (DonnaSocket    *socket)
{
    if (--socket->ref > 0)
        return;

    if (socket->fd >= 0)
        close (socket->fd);
    if (socket->str_in)
        g_string_free (socket->str_in, TRUE);
    if (socket->str_out)
        g_string_free (socket->str_out, TRUE);
    if (G_UNLIKELY (socket->sid_in > 0))
        g_source_remove (socket->sid_in);
    if (G_UNLIKELY (socket->sid_out > 0))
        g_source_remove (socket->sid_out);
    if (G_UNLIKELY (socket->sid_err > 0))
        g_source_remove (socket->sid_err);
    if (socket->destroy && socket->data)
        socket->destroy (socket->data);
    g_slice_free (DonnaSocket, socket);
}

/**
 * donna_socket_ref:
 * @socket: A #DonnaSocket
 *
 * Adds a reference to @socket
 *
 * Note that #DonnaSocket is NOT multithread safe, and should only be used from
 * the main thread/default main context.
 *
 * Returns: @socket
 */
DonnaSocket *
donna_socket_ref (DonnaSocket    *socket)
{
    ++socket->ref;
    return socket;
}

/**
 * donna_socket_close:
 * @socket: A #DonnaSocket
 *
 * Closes the socket behind @socket
 *
 * Note that the socket is closed right away and any pending output (e.g.
 * buffered message that couldn't yet be sent) will not be send as a result.
 */
void
donna_socket_close (DonnaSocket    *socket)
{
    /* we need to set sid_* to 0 BEFORE we call g_source_remove() in case it
     * will trigger donna_socket_unref() and remove the last reference on
     * socket, as that would then cause an error when trying to remove a
     * non-existing source. */
    guint id;

    if (socket->fd >= 0)
    {
        close (socket->fd);
        socket->process (socket, NULL, socket->data);
        socket->fd = -1;
    }
    if (socket->sid_in > 0)
    {
        id = socket->sid_in;
        socket->sid_in = 0;
        g_source_remove (id);
    }
    if (socket->sid_out > 0)
    {
        id = socket->sid_out;
        socket->sid_out = 0;
        g_source_remove (id);
    }
    if (socket->sid_err > 0)
    {
        id = socket->sid_err;
        socket->sid_err = 0;
        g_source_remove (id);
    }
}

static gssize
_write (gint fd, const gchar *data, gsize len)
{
    gssize written;

again:
    written = write (fd, data, len);
    if (written < 0)
    {
        if (errno == EINTR)
            goto again;
        else if (errno == EAGAIN)
            return 0;
        else
        {
            gint _errno = errno;

            g_warning ("Failed to write to socket %d: %s",
                    fd, g_strerror (_errno));
            return -1;
        }
    }

    return written;
}

static gboolean
socket_out (gint fd, GIOCondition condition, DonnaSocket *socket)
{
    gssize written;

    if (G_UNLIKELY (g_source_is_destroyed (g_main_current_source ())
                || socket->fd == -1))
        return G_SOURCE_REMOVE;

    written = _write (socket->fd, socket->str_out->str, socket->str_out->len);
    if (written == 0)
        return G_SOURCE_CONTINUE;
    else if (written < 0)
    {
        socket->sid_out = 0;
        donna_socket_close (socket);
        return G_SOURCE_REMOVE;
    }

    if ((gsize) written < socket->str_out->len)
    {
        g_string_erase (socket->str_out, 0, written);
        return G_SOURCE_CONTINUE;
    }

    g_string_truncate (socket->str_out, 0);

    socket->sid_out = 0;
    return G_SOURCE_REMOVE;
}

/**
 * donna_socket_send:
 * @socket: A #DonnaSocket
 * @message: The message to send
 * @len: The length of @message. Set to (gsize)-1 to have it calculated,
 * assuming @message is a NUL-terminated string
 *
 * Sends @message via @socket, automatically adding the length prefix (as per
 * protocol) and buffering/waiting the write if needed
 *
 * Returns: %TRUE on success, else %FALSE. Note that success doesn't mean the
 * message was actually written (completely), if the socket isn't writable then
 * it would simply have been buffered, waiting for the socket to be writable to
 * send it, as to not block
 */
gboolean
donna_socket_send (DonnaSocket    *socket,
                   const gchar    *message,
                   gsize           len)
{
    gssize written;
    gchar buf[16], *b = buf;
    gsize l;

    if (socket->fd == -1)
        return FALSE;

    if (socket->str_out && socket->str_out->len > 0)
    {
        g_string_append (socket->str_out, message);
        return TRUE;
    }

    if (len == (gsize) -1)
        len = strlen (message);

    /* first write the size of the message and colon separator */
    l = (gsize) g_snprintf (buf, 16, "%" G_GSIZE_FORMAT ":", len);
    if (G_UNLIKELY (l >= 16))
        b = g_strdup_printf ("%" G_GSIZE_FORMAT ":", len);
    written = _write (socket->fd, b, l);
    if (written < 0)
    {
        donna_socket_close (socket);
        if (G_UNLIKELY (b != buf))
            g_free (b);
        return FALSE;
    }
    else if ((gsize) written < l)
    {
        l -= (gsize) written;
        if (!socket->str_out)
            socket->str_out = g_string_sized_new (l + len);
        g_string_append (socket->str_out, b + l);
        /* to have the whole message added to buffer */
        written = 0;
    }
    else /* written == l */
    {
        /* now write the actual message */
        written = _write (socket->fd, message, len);
        if (written < 0)
        {
            donna_socket_close (socket);
            if (G_UNLIKELY (b != buf))
                g_free (b);
            return FALSE;
        }
        else if ((gsize) written == len)
        {
            if (G_UNLIKELY (b != buf))
                g_free (b);
            return TRUE;
        }
    }
    if (G_UNLIKELY (b != buf))
        g_free (b);

    len -= (gsize) written;
    if (!socket->str_out)
        socket->str_out = g_string_sized_new (len);
    g_string_append (socket->str_out, message + len);

    socket->sid_out = g_unix_fd_add_full (G_PRIORITY_DEFAULT,
            socket->fd, G_IO_OUT,
            (GUnixFDSourceFunc) socket_out,
            donna_socket_ref (socket),
            (GDestroyNotify) donna_socket_unref);

    return TRUE;
}
