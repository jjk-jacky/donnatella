/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * embedder.h
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

#ifndef __DONNA_EMBEDDER_H__
#define __DONNA_EMBEDDER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _DonnaEmbedder               DonnaEmbedder;
typedef struct _DonnaEmbedderPrivate        DonnaEmbedderPrivate;
typedef struct _DonnaEmbedderClass          DonnaEmbedderClass;

#define DONNA_TYPE_EMBEDDER                 (donna_embedder_get_type ())
#define DONNA_EMBEDDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_EMBEDDER, DonnaEmbedder))
#define DONNA_EMBEDDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_EMBEDDER, DonnaEmbedderClass))
#define DONNA_IS_EMBEDDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_EMBEDDER))
#define DONNA_IS_EMBEDDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_EMBEDDER))
#define DONNA_EMBEDDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_EMBEDDER, DonnaEmbedderClass))

GType   donna_embedder_get_type             (void) G_GNUC_CONST;


struct _DonnaEmbedder
{
    /*< private >*/
    GtkSocket                socket;
    DonnaEmbedderPrivate    *priv;
};

struct _DonnaEmbedderClass
{
    GtkSocketClass parent_class;
};

GtkWidget *     donna_embedder_new              (gboolean        catch_events);
void            donna_embedder_set_catch_events (DonnaEmbedder  *embedder,
                                                 gboolean        catch_events);
gboolean        donna_embedder_get_catch_events (DonnaEmbedder  *embedder);

G_END_DECLS

#endif /* __DONNA_EMBEDDER_H__ */
