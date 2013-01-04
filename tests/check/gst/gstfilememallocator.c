/* GStreamer GstFileMemAllocator unit tests
 *
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: Krzysztof Konopko <krzysztof.konopko@youview.com>
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

#include <gst/gstfilememallocator.h>
#include <gst/check/gstcheck.h>

static GstAllocator *
create_allocator (guint64 size)
{
  GstAllocator *alloc = NULL;

  gst_filemem_allocator_init ("FileMem",
      size, "/tmp/gstfilememallocator-XXXXXX");

  alloc = gst_allocator_find ("FileMem");
  fail_unless (NULL != alloc);

  return alloc;
}

static GstAllocator *
create_allocator_defult (void)
{
  return create_allocator (G_GUINT64_CONSTANT (1) << 20);
}

GST_START_TEST (test_get_some_memory)
{
  GstAllocator *alloc = NULL;
  GstMemory *mem = NULL;
  gsize offset = 0, maxsize = 0, size = 0;

  const gsize requested_size = 1 << 10;

  alloc = create_allocator_defult ();

  mem = gst_allocator_alloc (alloc, requested_size, NULL);
  fail_unless (NULL != mem);

  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  assert_equals_int (size, requested_size);
  assert_equals_int (offset, 0);
  fail_unless (maxsize >= requested_size);

  gst_allocator_free (alloc, mem);
  gst_object_unref (alloc);
}

GST_END_TEST;

GST_START_TEST (test_write_read)
{
  GstAllocator *alloc = NULL;
  GstMemory *mem = NULL;
  int test_value = 0xa;
  GstMapInfo info;

  const gsize requested_size = 1 << 10;

  alloc = create_allocator_defult ();

  mem = gst_allocator_alloc (alloc, requested_size, NULL);
  fail_unless (NULL != mem);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  memset (info.data, test_value, info.size);
  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless_equals_int (info.data[0], test_value);
  fail_unless_equals_int (info.data[info.size / 2], test_value);
  fail_unless_equals_int (info.data[info.size - 1], test_value);
  gst_memory_unmap (mem, &info);

  gst_allocator_free (alloc, mem);
  gst_object_unref (alloc);
}

GST_END_TEST;

GST_START_TEST (test_submemory)
{
  GstAllocator *alloc = NULL;
  GstMemory *memory = NULL, *sub = NULL;
  GstMapInfo info, sinfo;

  alloc = create_allocator_defult ();

  memory = gst_allocator_alloc (alloc, 4, NULL);

  /* check sizes, memory starts out empty */
  fail_unless (gst_memory_map (memory, &info, GST_MAP_WRITE));
  fail_unless (info.size == 4, "memory has wrong size");
  fail_unless (info.maxsize >= 4, "memory has wrong size");
  memset (info.data, 0, 4);
  gst_memory_unmap (memory, &info);

  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));

  sub = gst_memory_share (memory, 1, 2);
  fail_if (sub == NULL, "share of memory returned NULL");

  fail_unless (gst_memory_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 2, "submemory has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 2) == 0,
      "submemory contains the wrong data");
  ASSERT_MINI_OBJECT_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, &sinfo);
  gst_memory_unref (sub);

  /* create a submemory of size 0 */
  sub = gst_memory_share (memory, 1, 0);
  fail_if (sub == NULL, "share memory returned NULL");
  fail_unless (gst_memory_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 0, "submemory has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 0) == 0,
      "submemory contains the wrong data");
  ASSERT_MINI_OBJECT_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, &sinfo);
  gst_memory_unref (sub);

  /* test if metadata is copied, not a complete memory copy so only the
   * timestamp and offset fields are copied. */
  sub = gst_memory_share (memory, 0, 1);
  fail_if (sub == NULL, "share of memory returned NULL");
  fail_unless (gst_memory_get_sizes (sub, NULL, NULL) == 1,
      "submemory has wrong size");
  gst_memory_unref (sub);

  /* test if metadata is coppied, a complete memory is copied so all the timing
   * fields should be copied. */
  sub = gst_memory_share (memory, 0, 4);
  fail_if (sub == NULL, "share of memory returned NULL");
  fail_unless (gst_memory_get_sizes (sub, NULL, NULL) == 4,
      "submemory has wrong size");

  /* clean up */
  gst_memory_unref (sub);

  gst_memory_unmap (memory, &info);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_is_span)
{
  GstAllocator *alloc = NULL;
  GstMemory *memory = NULL, *sub1 = NULL, *sub2 = NULL;

  alloc = create_allocator_defult ();

  memory = gst_allocator_alloc (alloc, 4, NULL);

  sub1 = gst_memory_share (memory, 0, 2);
  fail_if (sub1 == NULL, "share of memory returned NULL");

  sub2 = gst_memory_share (memory, 2, 2);
  fail_if (sub2 == NULL, "share of memory returned NULL");

  fail_if (gst_memory_is_span (memory, sub2, NULL) == TRUE,
      "a parent memory can't be span");

  fail_if (gst_memory_is_span (sub1, memory, NULL) == TRUE,
      "a parent memory can't be span");

  fail_if (gst_memory_is_span (sub1, sub2, NULL) == FALSE,
      "two submemorys next to each other should be span");

  /* clean up */
  gst_memory_unref (sub1);
  gst_memory_unref (sub2);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstMapInfo info, sinfo;

  GstAllocator *alloc = NULL;
  GstMemory *memory = NULL, *copy = NULL;

  alloc = create_allocator_defult ();

  memory = gst_allocator_alloc (alloc, 4, NULL);
  ASSERT_MINI_OBJECT_REFCOUNT (memory, "memory", 1);

  copy = gst_memory_copy (memory, 0, -1);
  ASSERT_MINI_OBJECT_REFCOUNT (memory, "memory", 1);
  ASSERT_MINI_OBJECT_REFCOUNT (copy, "copy", 1);
  /* memorys are copied and must point to different memory */
  fail_if (memory == copy);

  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));
  fail_unless (gst_memory_map (copy, &sinfo, GST_MAP_READ));

  /* NOTE that data is refcounted */
  fail_unless (info.size == sinfo.size);

  gst_memory_unmap (copy, &sinfo);
  gst_memory_unmap (memory, &info);

  gst_memory_unref (copy);
  gst_memory_unref (memory);

  memory = gst_allocator_alloc (alloc, 0, NULL);
  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));
  fail_unless (info.size == 0);
  gst_memory_unmap (memory, &info);

  /* copying a 0-sized memory should not crash */
  copy = gst_memory_copy (memory, 0, -1);
  fail_unless (gst_memory_map (copy, &info, GST_MAP_READ));
  fail_unless (info.size == 0);
  gst_memory_unmap (copy, &info);

  gst_memory_unref (copy);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_map)
{
  GstMapInfo info;
  gsize maxalloc;
  gsize size, offset;

  GstAllocator *alloc = NULL;
  GstMemory *mem = NULL;

  alloc = create_allocator_defult ();

  /* one memory block */
  mem = gst_allocator_alloc (alloc, 100, NULL);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* see if simply mapping works */
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);
  fail_unless (info.maxsize == maxalloc);

  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);
  fail_unless (info.maxsize == maxalloc);

  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_map_until_exhausted)
{
  guint64 allocator_total_size = G_GUINT64_CONSTANT (1) << 20;
  GstAllocator *alloc = NULL;
  GstMemory *mem1 = NULL, *mem2 = NULL;

  alloc = create_allocator (allocator_total_size);

  mem1 = gst_allocator_alloc (alloc, allocator_total_size + 1, NULL);
  fail_unless (NULL == mem1);

  mem1 = gst_allocator_alloc (alloc, allocator_total_size / 2 + 1, NULL);
  fail_unless (NULL != mem1);

  mem2 = gst_allocator_alloc (alloc, allocator_total_size / 2, NULL);
  fail_unless (NULL == mem2);

  gst_memory_unref (mem1);
}

GST_END_TEST;

GST_START_TEST (test_properties)
{
  guint64 allocator_total_size = G_GUINT64_CONSTANT (1) << 20;
  GstAllocator *alloc = NULL;

  guint64 size = 0;

  alloc = create_allocator (allocator_total_size);

  g_object_get (alloc, "file-size", &size, NULL);

  fail_unless_equals_uint64 (size, allocator_total_size);
}

GST_END_TEST;

GST_START_TEST (test_unmap)
{
  guint64 allocator_total_size = G_GUINT64_CONSTANT (1) << 20;
  guint block_size = 32 * 1024;

  guint64 num_blocks = allocator_total_size / block_size;
  guint num_maps = 2048;

  GstAllocator *alloc = create_allocator (allocator_total_size);

  for (; 0 != num_blocks; --num_blocks) {
    GstMapInfo info;
    guint i;

    GstMemory *mem = gst_allocator_alloc (alloc, block_size, NULL);
    fail_unless (NULL != mem);

    for (i = 0; i < num_maps; ++i) {
      fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
      gst_memory_unmap (mem, &info);
      fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
      gst_memory_unmap (mem, &info);
    }

    gst_memory_unref (mem);
  }
}

GST_END_TEST;

static Suite *
gst_file_mem_allocator_suite (void)
{
  Suite *s = suite_create ("GstFileMemAllocator");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_get_some_memory);
  tcase_add_test (tc_chain, test_write_read);
  tcase_add_test (tc_chain, test_submemory);
  tcase_add_test (tc_chain, test_is_span);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_map);
  tcase_add_test (tc_chain, test_map_until_exhausted);
  tcase_add_test (tc_chain, test_properties);
  tcase_add_test (tc_chain, test_unmap);

  return s;
}

GST_CHECK_MAIN (gst_file_mem_allocator);
