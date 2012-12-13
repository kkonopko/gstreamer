/* GStreamer
 * Copyright (C) 2012 Krzysztof Konopko <krzysztof.konopko@gmail.com>
 *
 * gstfilememallocator.c: memory block allocator using a file as a storage
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

/**
 * SECTION:gstfilememallocator
 * @short_description: allocate memory blocks using a file as a storage
 * @see_also: #GstAllocator
 *
TODO: Description
 *
 * Last reviewed on 2012-12-13 (1.0.4)
 */

#include "gstfilememallocator.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstallocator.h"
#include "gstminiobject.h"
#include "gstmemory.h"

#include <errno.h>
#include <stdlib.h>

#if defined(HAVE_MMAP) && defined(HAVE_SYS_TYPES_H) && defined(HAVE_UNISTD_H)

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/* TODO: Handle unsupported platforms */
#define GST_FILEMEMALLOCATOR_SUPPORTED

#endif

GST_DEBUG_CATEGORY_STATIC (gst_filememallocator_debug);
#define GST_CAT_DEFAULT gst_filememallocator_debug

typedef struct _GstFileMemory GstFileMemory;
typedef struct _GstFileMemAllocator GstFileMemAllocator;
typedef struct _GstFileMemAllocatorClass GstFileMemAllocatorClass;

struct _GstFileMemory
{
  GstMemory mem;

  off_t f_offset;
  gpointer data;
};

struct _GstFileMemAllocator
{
  GstAllocator parent;

  long page_size;

  guint64 file_size;
  int fd;
  off_t f_offset_next;
};

struct _GstFileMemAllocatorClass
{
  GstAllocatorClass parent_class;
};

#define DEFAULT_FILE_SIZE (G_GUINT64_CONSTANT (1) << 20)

enum
{
  PROP_0,
  PROP_FILE_SIZE
};

static gsize
align_size (gsize size, gsize alignment)
{
  return (size + alignment - 1) & ~(alignment - 1);
}

static GstMemory *
gst_file_mem_alloc (GstAllocator * alloc,
		gsize size, GstAllocationParams * params)
{
  GstFileMemory *mem;

  GstFileMemAllocator *allocator = (GstFileMemAllocator *)alloc;
  gsize maxsize = align_size(size + params->prefix + params->padding,
                             allocator->page_size);

  GST_DEBUG ("alloc from allocator %p", allocator);

  if (allocator->f_offset_next + maxsize > allocator->file_size) {
    GST_ERROR ("Cannot allocate %u bytes: not enough space", maxsize);
    return NULL;
  }

  mem = g_slice_new (GstFileMemory);

  gst_memory_init (GST_MEMORY_CAST (mem), params->flags, alloc, NULL,
      maxsize, params->align, params->prefix, size);

  mem->f_offset = allocator->f_offset_next =+ maxsize;
  mem->data = NULL;

  return (GstMemory *) mem;
}

static void
gst_file_mem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstFileMemory *mmem = (GstFileMemory *) mem;

  // TODO: Return f_offset to the pool or just drop it?
  // Yeah, drop it on the floor for now.

  g_slice_free (GstFileMemory, mmem);
  GST_DEBUG ("%p: freed", mmem);
}

static int
gst_file_mem_get_map_prot (GstMapFlags flags)
{
  switch (flags) {
  case GST_MAP_READ: return PROT_READ;
  case GST_MAP_WRITE: return PROT_WRITE;
  default:
    g_return_val_if_reached (PROT_NONE);
    break;
  }

  return PROT_NONE;
}

static gpointer
gst_file_mem_map (GstFileMemory * mem, gsize maxsize, GstMapFlags flags)
{
  gpointer res;
  int mmap_prot = gst_file_mem_get_map_prot (flags);
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) mem->mem.allocator;

  /* Can't really control the alignment
     Can we do something about data offset (GstAllocationParams)? Is it
     the right place to handle it? */
  res = mmap (NULL, maxsize, mmap_prot, MAP_PRIVATE, allocator->fd,
		  mem->f_offset);

  if (MAP_FAILED == res) {
    GST_ERROR ("mmap() failed: %s", g_strerror (errno));
    return NULL;
  }

  GST_DEBUG ("%p: mapped %p", mem, res);

  return res;
}

static gboolean
gst_file_mem_unmap (GstFileMemory * mem)
{
  if (0 != munmap (mem->data, mem->mem.maxsize)) {
    GST_ERROR ("munmap() failed: %s", g_strerror (errno));
    return FALSE;
  }

  GST_DEBUG ("%p: unmapped", mem);
  return TRUE;
}

static GstFileMemory *
gst_file_mem_share (GstFileMemory * mem, gssize offset, gsize size)
{
  GstFileMemory *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub = g_slice_new (GstFileMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->f_offset = mem->f_offset;
  sub->data = gst_file_mem_map (mem, mem->mem.maxsize, GST_MAP_READ);

  return sub;
}

GType gst_file_mem_allocator_get_type (void);
G_DEFINE_TYPE (GstFileMemAllocator, gst_file_mem_allocator, GST_TYPE_ALLOCATOR);

static void
gst_file_mem_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *)object;

  switch (prop_id) {
    case PROP_FILE_SIZE:
      allocator->file_size = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_file_mem_constructed (GObject * object)
{
  char tmpfile_name[] = "/tmp/gstfilememallocator-XXXXXX";
  GstFileMemAllocator *allocator = (GstFileMemAllocator *)object;

  allocator->fd = mkstemp (tmpfile_name);

  if (-1 == allocator->fd) {
    g_error ("mkstemp() failed: %s", g_strerror (errno));
  }

  if (-1 == unlink (tmpfile_name)) {
    g_error ("unlink() failed: %s", g_strerror (errno));
  }

  // TODO:
  if (-1 == ftruncate64 (allocator->fd, allocator->file_size)) {
    g_error ("ftruncate64() failed: %s", g_strerror (errno));
  }

  G_OBJECT_CLASS (gst_file_mem_allocator_parent_class)->constructed (object);
}

static void
gst_file_mem_dispose (GObject * object)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *)object;
  if (-1 != allocator->fd && -1 == close (allocator->fd)) {
    g_error ("close() failed: %s", g_strerror (errno));
  }

  G_OBJECT_CLASS (gst_file_mem_allocator_parent_class)->dispose (object);
}

static void
gst_file_mem_allocator_class_init (GstFileMemAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  gobject_class->set_property = gst_file_mem_set_property;
  gobject_class->constructed = gst_file_mem_constructed;
  gobject_class->dispose = gst_file_mem_dispose;

  allocator_class->alloc = gst_file_mem_alloc;
  allocator_class->free = gst_file_mem_free;

  g_object_class_install_property (gobject_class, PROP_FILE_SIZE,
    g_param_spec_uint64 ("file-size", "File Size",
      "The size of the file that will be used for a memory pool",
      DEFAULT_FILE_SIZE, G_MAXUINT64, DEFAULT_FILE_SIZE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_file_mem_allocator_init (GstFileMemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "File memory allocator";
  alloc->mem_map = (GstMemoryMapFunction) gst_file_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) gst_file_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) gst_file_mem_share;

  allocator->page_size = sysconf (_SC_PAGESIZE);
  allocator->file_size = DEFAULT_FILE_SIZE;
  allocator->fd = -1;
  allocator->f_offset_next = 0;
}

void
gst_filemem_allocator_init (guint64 size, const gchar * name)
{
  GstFileMemAllocator *allocator =
		  g_object_new (gst_file_mem_allocator_get_type (), "file-size", size,
				  NULL);

  gst_allocator_register (name, GST_ALLOCATOR_CAST (allocator));
}
