/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * conf.h
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

#ifndef __DONNA_CONFIG_H__
#define __DONNA_CONFIG_H__

#include "provider-config.h"

/* empty because we haven't made an interface DonnaConfig (that provider-config
 * would also implement), but it might happen some day. Until then, all required
 * API is on provider-config.h but when needing to use the config, one should
 * include this very conf.h */

#endif /* __DONNA_CONFIG_H__ */
