/* GStreamer
 *
 * unit test for GstFileMemAllocator
 *
 * Copyright (C) <2012> Krzysztof Konopko <krzysztof.konopko@gmail.com>
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

GST_START_TEST (test_get_some_memory)
{
  GstAllocator *alloc = NULL;
  GstMemory *mem = NULL;
  gsize offset = 0, maxsize = 0, size = 0;

  const gchar *aname = "FileMem";
  const guint64 allocator_total_size = G_GUINT64_CONSTANT (1) << 20;
  const gsize requested_size = 1 << 10;

  gst_filemem_allocator_init (allocator_total_size, aname);

  alloc = gst_allocator_find (aname);
  fail_unless (NULL != alloc);

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

GST_START_TEST (test_submemory)
{
  GstAllocator *alloc = NULL;
  GstMemory *memory = NULL, *sub = NULL;
  GstMapInfo info, sinfo;

  gst_filemem_allocator_init (G_GUINT64_CONSTANT (1) << 20, "FileMem");

  alloc = gst_allocator_find ("FileMem");
  fail_unless (NULL != alloc);

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

  gst_filemem_allocator_init (G_GUINT64_CONSTANT (1) << 20, "FileMem");

  alloc = gst_allocator_find ("FileMem");
  fail_unless (NULL != alloc);

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

static Suite *
gst_file_mem_allocator_suite (void)
{
  Suite *s = suite_create ("GstFileMemAllocator");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_get_some_memory);
  tcase_add_test (tc_chain, test_submemory);
  tcase_add_test (tc_chain, test_is_span);

  return s;
}

GST_CHECK_MAIN (gst_file_mem_allocator);
