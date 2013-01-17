/* GStreamer
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: Krzysztof Konopko <krzysztof.konopko@youview.com>
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
 * The default allocator uses virtual memory which might be inconvenient when
 * there's a demand to keep large amount of buffers (e. g. a media ring buffer)
 * while the amount of physical memory is limited (e. g. an embedded system).
 * Given that the disk space is available instead, GstFileMemAllocator offers
 * memory blocks which are mapped to file system blocks in a temporary file.
 *
 * The allocator can be initialized with a call to gst_filemem_allocator_init()
 * (usually in the main context of the application) and then retrieved with
 * gst_allocator_find ().
 * <example>
 * <title>Using file memory allocator</title>
 *   <programlisting>
 *   const guint64 ring_buffer_size = 512 * 1024 * 1024;
 *   const gchar *allocator_file_template = "/tmp/file-mem-alloc-XXXXXX";
 *   GstObject *some_obj;
 *   gst_filemem_allocator_init (ring_buffer_size, allocator_file_template);
 *   ...
 *   some_obj = ...;
 *   g_object_set (some_obj, "allocator-name", GST_ALLOCATOR_FILEMEM, NULL);
 *   ...
 *
 *   gchar *prop_allocator_name = NULL;
 *   guint buffer_size = ...;
 *   GstAllocator *allocator;
 *   GstBuffer *buffer;
 *   ...
 *
 *   allocator = gst_allocator_find (prop_allocator_name);
 *   buffer = gst_buffer_new_allocate (allocator, buffer_size, NULL);
 *   ...
 *   </programlisting>
 * </example>
 *
 * Currently supported only on platforms with mmap() function available.
 *
 * Last reviewed on 2013-01-14 (1.0.5)
 */

#include "gstfilememallocator.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstallocator.h"
#include "gstminiobject.h"
#include "gstmemory.h"

#include <glib/gstdio.h>

#include <errno.h>

#if defined(HAVE_MMAP) && defined(HAVE_SYS_TYPES_H) && defined(HAVE_UNISTD_H)

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_DECL_FALLOC_FL_PUNCH_HOLE
#include <fcntl.h>
#include <linux/falloc.h>
#endif

#define GST_FILEMEMALLOCATOR_SUPPORTED

#endif

#ifdef GST_FILEMEMALLOCATOR_SUPPORTED

GST_DEBUG_CATEGORY_STATIC (gst_file_mem_allocator_debug_category);
#define GST_CAT_DEFAULT gst_file_mem_allocator_debug_category

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
  gchar *temp_template;

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
  PROP_FILE_SIZE,
  PROP_TEMP_TEMPLATE,
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

  GstFileMemAllocator *allocator = (GstFileMemAllocator *) alloc;
  gsize maxsize = align_size (size + params->prefix + params->padding,
      allocator->page_size);

  if (0 == maxsize) {
    maxsize = allocator->page_size;
  }

  GST_DEBUG ("alloc from allocator %p, size %u", allocator, maxsize);

  if (allocator->f_offset_next + maxsize > allocator->file_size) {
    GST_WARNING ("Cannot allocate %u bytes: not enough space", maxsize);
    return NULL;
  }

  /* We do it here for symmetry with gst_file_mem_free(). Note that if
     fallocate() is not supported then we don't do anything about it and let
     mmap() fail if it can't allocate the disk space later on. */
#if defined (HAVE_FALLOCATE)
  if (0 != fallocate (allocator->fd, 0, allocator->f_offset_next, maxsize)) {
    GST_WARNING ("Cannot allocate %u bytes of disk space: %s",
        maxsize, g_strerror (errno));
    return NULL;
  }
#endif

  mem = g_slice_new (GstFileMemory);

  gst_memory_init (GST_MEMORY_CAST (mem), params->flags, alloc, NULL,
      maxsize, params->align, params->prefix, size);

  mem->f_offset = allocator->f_offset_next;
  mem->data = NULL;

  allocator->f_offset_next += maxsize;

  return (GstMemory *) mem;
}

static void
gst_file_mem_free (GstAllocator * alloc, GstMemory * mem)
{
  GstFileMemory *fmem = (GstFileMemory *) mem;

  /* TODO: Return f_offset to the pool or just drop it?
     For now the easiest thing is to not reclaim the block. Different schemes
     can be applied here like fixed block size allocator with simple list
     of blocks available or more advanced if needed. */

#if defined (HAVE_FALLOCATE) && HAVE_DECL_FALLOC_FL_PUNCH_HOLE
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) alloc;

  /* At least try to reclaim the disk space. */
  if (0 != fallocate (allocator->fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
          fmem->f_offset, mem->maxsize)) {
    if (EOPNOTSUPP == errno) {
      GST_WARNING ("Deallocating disk space not supported: %s",
          g_strerror (errno));
    } else {
      GST_ERROR ("Cannot deallocate disk space: %s", g_strerror (errno));
    }

  }
#endif

  g_slice_free (GstFileMemory, fmem);
  GST_DEBUG ("%p: freed", fmem);
}

static int
gst_file_mem_get_map_prot (GstMapFlags flags)
{
  switch (flags) {
    case GST_MAP_READ:
      return PROT_READ;

    case GST_MAP_WRITE:
      return PROT_WRITE;

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
     Can we do something about the data offset (GstAllocationParams)? Is it
     the right place to handle it? */
  res = mmap (NULL, maxsize, mmap_prot, MAP_SHARED, allocator->fd,
      mem->f_offset);

  if (MAP_FAILED == res) {
    GST_ERROR ("mmap() failed: %s", g_strerror (errno));
    return NULL;
  }

  GST_DEBUG ("%p: mapped %p", mem, res);

  return mem->data = res;
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
  /* the shared memory is always read only */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->f_offset = mem->f_offset;
  sub->data = gst_file_mem_map (mem, mem->mem.maxsize, GST_MAP_READ);

  return sub;
}

static gboolean
gst_file_mem_is_span (GstFileMemory * mem1, GstFileMemory * mem2,
    gsize * offset)
{

  if (offset) {
    GstFileMemory *parent = (GstFileMemory *) mem1->mem.parent;
    g_return_val_if_fail (NULL != parent, FALSE);

    *offset = mem1->mem.offset - parent->mem.offset;
  }

  /* and memory is contiguous */
  return mem1->f_offset == mem2->f_offset &&
      mem1->mem.offset + mem1->mem.size == mem2->mem.offset;
}

GType gst_file_mem_allocator_get_type (void);
G_DEFINE_TYPE (GstFileMemAllocator, gst_file_mem_allocator, GST_TYPE_ALLOCATOR);

static void
gst_file_mem_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) object;

  switch (prop_id) {
    case PROP_FILE_SIZE:
      allocator->file_size = g_value_get_uint64 (value);
      break;

    case PROP_TEMP_TEMPLATE:
      g_free (allocator->temp_template);
      allocator->temp_template = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_file_mem_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) object;

  switch (prop_id) {
    case PROP_FILE_SIZE:
      g_value_set_uint64 (value, allocator->file_size);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_file_mem_constructed (GObject * object)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) object;

  g_return_if_fail (allocator->temp_template);
  allocator->fd = g_mkstemp (allocator->temp_template);

  if (-1 == allocator->fd) {
    g_error ("g_mkstemp() failed: %s", g_strerror (errno));
  }

  if (-1 == g_unlink (allocator->temp_template)) {
    g_error ("g_unlink() failed: %s", g_strerror (errno));
  }

  if (-1 == ftruncate64 (allocator->fd, allocator->file_size)) {
    g_error ("ftruncate64() failed: %s", g_strerror (errno));
  }

  G_OBJECT_CLASS (gst_file_mem_allocator_parent_class)->constructed (object);
}

static void
gst_file_mem_dispose (GObject * object)
{
  GstFileMemAllocator *allocator = (GstFileMemAllocator *) object;
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
  gobject_class->get_property = gst_file_mem_get_property;

  gobject_class->constructed = gst_file_mem_constructed;
  gobject_class->dispose = gst_file_mem_dispose;

  allocator_class->alloc = gst_file_mem_alloc;
  allocator_class->free = gst_file_mem_free;

  g_object_class_install_property (gobject_class, PROP_FILE_SIZE,
      g_param_spec_uint64 ("file-size", "File Size",
          "The size of the file that will be used for a memory pool",
          DEFAULT_FILE_SIZE, G_MAXUINT64, DEFAULT_FILE_SIZE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEMP_TEMPLATE,
      g_param_spec_string ("temp-template", "File Template",
          "File template for temporary storage, should contain directory "
          "and a prefix filename.",
          NULL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_file_mem_allocator_debug_category,
      "gst_file_mem_allocator", 0, "file memory allocator");
}

static void
gst_file_mem_allocator_init (GstFileMemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_ALLOCATOR_FILEMEM;
  alloc->mem_map = (GstMemoryMapFunction) gst_file_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) gst_file_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) gst_file_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) gst_file_mem_is_span;

  allocator->page_size = sysconf (_SC_PAGESIZE);

  allocator->file_size = DEFAULT_FILE_SIZE;
  allocator->temp_template = NULL;

  allocator->fd = -1;
  allocator->f_offset_next = 0;
}

#endif // GST_FILEMEMALLOCATOR_SUPPORTED

/**
 * gst_filemem_allocator_init:
 * @size: available memory size
 * @temp_template: file template for temporary storage
 *
 * Initialize a file memory allocator which will reserve @size bytes in
 * a temporary file created based on @temp_template. This should be called only
 * once within the application.
 */
void
gst_filemem_allocator_init (guint64 size, const gchar * temp_template)
{
#ifdef GST_FILEMEMALLOCATOR_SUPPORTED

  GstFileMemAllocator *allocator;

  if (gst_allocator_find (GST_ALLOCATOR_FILEMEM)) {
    GST_WARNING ("%s allocator already initialized", GST_ALLOCATOR_FILEMEM);
    return;
  }

  allocator =
      g_object_new (gst_file_mem_allocator_get_type (),
      "file-size", size, "temp-template", temp_template, NULL);

  gst_allocator_register (GST_ALLOCATOR_FILEMEM,
      GST_ALLOCATOR_CAST (allocator));

#else

  GST_ERROR ("%s allocator is not supported on this platform",
      GST_ALLOCATOR_FILEMEM);
#endif // GST_FILEMEMALLOCATOR_SUPPORTED
}
