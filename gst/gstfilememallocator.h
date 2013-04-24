/* GStreamer
 * Copyright (C) 2012 Krzysztof Konopko <krzysztof.konopko@gmail.com>
 *
 * gstfilememallocator.h: Header for memory allocation using a file as a storage
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_FILEMEMALLOCATOR_H__
#define __GST_FILEMEMALLOCATOR_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

G_BEGIN_DECLS

/* an allocator that uses mmap()-ed file chunks */
void gst_filemem_allocator_init (guint64 size, const gchar * name);

G_END_DECLS

#endif /* __GST_FILEMEMALLOCATOR_H__ */
