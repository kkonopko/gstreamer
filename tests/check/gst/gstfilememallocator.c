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

static Suite *
gst_file_mem_allocator_suite (void)
{
  Suite *s = suite_create ("GstFileMemAllocator");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_get_some_memory);

  return s;
}

GST_CHECK_MAIN (gst_file_mem_allocator);
