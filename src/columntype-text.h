/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-text.h
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

#ifndef __DONNA_COLUMN_TYPE_TEXT_H__
#define __DONNA_COLUMN_TYPE_TEXT_H__

#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMN_TYPE_TEXT             (donna_column_type_text_get_type ())
#define DONNA_COLUMN_TYPE_TEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMN_TYPE_TEXT, DonnaColumnTypeText))
#define DONNA_COLUMN_TYPE_TEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMN_TYPE_TEXT, DonnaColumnTypeTextClass))
#define DONNA_IS_COLUMN_TYPE_TEXT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMN_TYPE_TEXT))
#define DONNA_IS_COLUMN_TYPE_TEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMN_TYPE_TEXT))
#define DONNA_COLUMN_TYPE_TEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMN_TYPE_TEXT, DonnaColumnTypeTextClass))

typedef struct _DonnaColumnTypeText             DonnaColumnTypeText;
typedef struct _DonnaColumnTypeTextClass        DonnaColumnTypeTextClass;
typedef struct _DonnaColumnTypeTextPrivate      DonnaColumnTypeTextPrivate;

struct _DonnaColumnTypeText
{
    GObject parent;

    DonnaColumnTypeTextPrivate *priv;
};

struct _DonnaColumnTypeTextClass
{
    GObjectClass parent;
};

GType                   donna_column_type_text_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COLUMN_TYPE_TEXT_H__ */
