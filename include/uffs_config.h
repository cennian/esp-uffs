/*
  This file is part of UFFS, the Ultra-low-cost Flash File System.

  Copyright (C) 2005-2009 Ricky Zheng <ricky_gz_zheng@yahoo.co.nz>

  UFFS is free software; you can redistribute it and/or modify it under
  the GNU Library General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any
  later version.

  UFFS is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  or GNU Library General Public License, as applicable, for more details.

  You should have received a copy of the GNU General Public License
  and GNU Library General Public License along with UFFS; if not, write
  to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA  02110-1301, USA.

  As a special exception, if other files instantiate templates or use
  macros or inline functions from this file, or you compile this file
  and link it with other works to produce a work based on this file,
  this file does not by itself cause the resulting work to be covered
  by the GNU General Public License. However the source code for this
  file must still be made available in accordance with section (3) of
  the GNU General Public License v2.

  This exception does not invalidate any other reasons why a work based
  on this file might be covered by the GNU General Public License.
*/

/**
 * \file uffs_config.h
 * \brief basic configuration of uffs
 * \author Ricky Zheng
 */

#ifndef _UFFS_CONFIG_H_
#define _UFFS_CONFIG_H_

#include "sdkconfig.h"

/**
 * \def UFFS_MAX_PAGE_SIZE
 * \note maximum page size UFFS support
 */
#ifdef CONFIG_UFFS_MAX_PAGE_SIZE
#define UFFS_MAX_PAGE_SIZE CONFIG_UFFS_MAX_PAGE_SIZE
#else
#define UFFS_MAX_PAGE_SIZE 4096 /* Default if Kconfig not present */
#endif

/**
 * \def UFFS_MAX_SPARE_SIZE
 */
#define UFFS_MAX_SPARE_SIZE ((UFFS_MAX_PAGE_SIZE / 256) * 16)

/**
 * \def UFFS_MAX_ECC_SIZE
 */
#define UFFS_MAX_ECC_SIZE ((UFFS_MAX_PAGE_SIZE / 256) * 5)

/**
 * \def MAX_CACHED_BLOCK_INFO
 */
#ifdef CONFIG_UFFS_MAX_CACHED_BLOCK_INFO
#define MAX_CACHED_BLOCK_INFO CONFIG_UFFS_MAX_CACHED_BLOCK_INFO
#else
#define MAX_CACHED_BLOCK_INFO 128
#endif

/**
 * \def MAX_PAGE_BUFFERS
 */
#ifdef CONFIG_UFFS_MAX_PAGE_BUFFERS
#define MAX_PAGE_BUFFERS CONFIG_UFFS_MAX_PAGE_BUFFERS
#else
#define MAX_PAGE_BUFFERS 40
#endif

/**
 * \def CLONE_BUFFER_THRESHOLD
 */
#ifdef CONFIG_UFFS_CLONE_BUFFERS_THRESHOLD
#define CLONE_BUFFERS_THRESHOLD CONFIG_UFFS_CLONE_BUFFERS_THRESHOLD
#else
#define CLONE_BUFFERS_THRESHOLD 2
#endif

/**
 * \def MAX_SPARE_BUFFERS
 */
#ifdef CONFIG_UFFS_MAX_SPARE_BUFFERS
#define MAX_SPARE_BUFFERS CONFIG_UFFS_MAX_SPARE_BUFFERS
#else
#define MAX_SPARE_BUFFERS 5
#endif

/**
 * \def CONFIG_MAX_PENDING_BLOCKS
 */
#ifdef CONFIG_UFFS_MAX_PENDING_BLOCKS
#define CONFIG_MAX_PENDING_BLOCKS CONFIG_UFFS_MAX_PENDING_BLOCKS
#else
#define CONFIG_MAX_PENDING_BLOCKS 4
#endif

/**
 * \def MAX_DIRTY_PAGES_IN_A_BLOCK
 */
#ifdef CONFIG_UFFS_MAX_DIRTY_PAGES_IN_A_BLOCK
#define MAX_DIRTY_PAGES_IN_A_BLOCK CONFIG_UFFS_MAX_DIRTY_PAGES_IN_A_BLOCK
#else
#define MAX_DIRTY_PAGES_IN_A_BLOCK 10
#endif

/**
 * \def CONFIG_ENABLE_UFFS_DEBUG_MSG
 */
#ifdef CONFIG_UFFS_ENABLE_DEBUG_MSG
#define CONFIG_ENABLE_UFFS_DEBUG_MSG
#endif

/**
 * \def CONFIG_USE_GLOBAL_FS_LOCK
 */
#ifdef CONFIG_UFFS_USE_GLOBAL_FS_LOCK
#define CONFIG_USE_GLOBAL_FS_LOCK
#endif

/**
 * \def CONFIG_USE_PER_DEVICE_LOCK
 */
#ifdef CONFIG_UFFS_USE_PER_DEVICE_LOCK
#define CONFIG_USE_PER_DEVICE_LOCK
#endif

/**
 * \def CONFIG_USE_STATIC_MEMORY_ALLOCATOR
 */
#ifdef CONFIG_UFFS_USE_SYSTEM_MEMORY_ALLOCATOR
#define CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR 1
#define CONFIG_USE_STATIC_MEMORY_ALLOCATOR 0
#else
#define CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR 0
#define CONFIG_USE_STATIC_MEMORY_ALLOCATOR 1
#endif

/**
 * \def CONFIG_FLUSH_BUF_AFTER_WRITE
 */
// #define CONFIG_FLUSH_BUF_AFTER_WRITE

/**
 * \def CONFIG_UFFS_AUTO_LAYOUT_MTD_COMP
 */
// #define CONFIG_UFFS_AUTO_LAYOUT_USE_MTD_SCHEME

/**
 * \def MAX_OBJECT_HANDLE
 * maximum number of object handle
 */
#define MAX_OBJECT_HANDLE 50
#define FD_SIGNATURE_SHIFT 6

/**
 * \def MAX_DIR_HANDLE
 * maximum number of uffs_DIR
 */
#define MAX_DIR_HANDLE 10

/**
 * \def MINIMUN_ERASED_BLOCK
 */
#define MINIMUN_ERASED_BLOCK 2

/**
 * \def CONFIG_CHANGE_MODIFY_TIME
 */
// #define CONFIG_CHANGE_MODIFY_TIME

/**
 * \def CONFIG_ENABLE_BAD_BLOCK_VERIFY
 */
// #define CONFIG_ENABLE_BAD_BLOCK_VERIFY

/**
 * \def CONFIG_ERASE_BLOCK_BEFORE_MARK_BAD
 */
#define CONFIG_ERASE_BLOCK_BEFORE_MARK_BAD

/**
 * \def CONFIG_PAGE_WRITE_VERIFY
 */
#ifdef CONFIG_UFFS_PAGE_WRITE_VERIFY
#define CONFIG_PAGE_WRITE_VERIFY
#endif

/**
 * \def CONFIG_BAD_BLOCK_POLICY_STRICT
 */
// #define CONFIG_BAD_BLOCK_POLICY_STRICT

/**
 * \def CONFIG_UFFS_REFRESH_BLOCK
 */
#define CONFIG_UFFS_REFRESH_BLOCK

/**
 * \def CONFIG_ENABLE_PAGE_DATA_CRC
 */
// #define CONFIG_ENABLE_PAGE_DATA_CRC

/** micros for calculating buffer sizes */

/**
 *	\def UFFS_BLOCK_INFO_BUFFER_SIZE
 *	\brief calculate memory bytes for block info caches
 */
#define UFFS_BLOCK_INFO_BUFFER_SIZE(n_pages_per_block)                         \
  ((sizeof(uffs_BlockInfo) + sizeof(uffs_PageSpare) * n_pages_per_block) *     \
   MAX_CACHED_BLOCK_INFO)

/**
 *	\def UFFS_PAGE_BUFFER_SIZE
 *	\brief calculate memory bytes for page buffers
 */
#define UFFS_PAGE_BUFFER_SIZE(n_page_size)                                     \
  ((sizeof(uffs_Buf) + n_page_size) * MAX_PAGE_BUFFERS)

/**
 *	\def UFFS_TREE_BUFFER_SIZE
 *	\brief calculate memory bytes for tree nodes
 */
#define UFFS_TREE_BUFFER_SIZE(n_blocks) (sizeof(TreeNode) * n_blocks)

#define UFFS_SPARE_BUFFER_SIZE (MAX_SPARE_BUFFERS * UFFS_MAX_SPARE_SIZE)

/**
 *	\def UFFS_STATIC_BUFF_SIZE
 *	\brief calculate total memory usage of uffs system
 */
#define UFFS_STATIC_BUFF_SIZE(n_pages_per_block, n_page_size, n_blocks)        \
  (UFFS_BLOCK_INFO_BUFFER_SIZE(n_pages_per_block) +                            \
   UFFS_PAGE_BUFFER_SIZE(n_page_size) + UFFS_TREE_BUFFER_SIZE(n_blocks) +      \
   UFFS_SPARE_BUFFER_SIZE)

/* config check */
#if (MAX_PAGE_BUFFERS - CLONE_BUFFERS_THRESHOLD) < 3
#error "MAX_PAGE_BUFFERS is too small"
#endif

#if (MAX_DIRTY_PAGES_IN_A_BLOCK < 2)
#error "MAX_DIRTY_PAGES_IN_A_BLOCK should >= 2"
#endif

#if (MAX_PAGE_BUFFERS - CLONE_BUFFERS_THRESHOLD - 1 <                          \
     MAX_DIRTY_PAGES_IN_A_BLOCK)
#error                                                                         \
    "MAX_DIRTY_PAGES_IN_A_BLOCK should < (MAX_PAGE_BUFFERS - CLONE_BUFFERS_THRESHOLD)"
#endif

#if defined(CONFIG_PAGE_WRITE_VERIFY) && (CLONE_BUFFERS_THRESHOLD < 2)
#error                                                                         \
    "CLONE_BUFFERS_THRESHOLD should >= 2 when CONFIG_PAGE_WRITE_VERIFY is enabled."
#endif

#if CONFIG_USE_STATIC_MEMORY_ALLOCATOR + CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR > 1
#error "Please enable ONLY one memory allocator"
#endif

#if CONFIG_USE_STATIC_MEMORY_ALLOCATOR + CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR == 0
#error "Please enable ONE of memory allocators"
#endif

#if defined(CONFIG_USE_GLOBAL_FS_LOCK) && defined(CONFIG_USE_PER_DEVICE_LOCK)
#error                                                                         \
    "enable either CONFIG_USE_GLOBAL_FS_LOCK or CONFIG_USE_PER_DEVICE_LOCK, not both"
#endif

#if (MAX_OBJECT_HANDLE > (1 << FD_SIGNATURE_SHIFT))
#error "Please increase FD_SIGNATURE_SHIFT !"
#endif

#if CONFIG_MAX_PENDING_BLOCKS < 2
#error "Please increase CONFIG_MAX_PENDING_BLOCKS, normally 4"
#endif

#if defined(CONFIG_BAD_BLOCK_POLICY_STRICT) &&                                 \
    defined(CONFIG_UFFS_REFRESH_BLOCK)
#error                                                                         \
    "CONFIG_UFFS_REFRESH_BLOCK conflict with CONFIG_BAD_BLOCK_POLICY_STRICT !"
#endif

#ifdef WIN32
#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4214)
#pragma warning(disable : 4127)
#pragma warning(disable : 4389)
#pragma warning(disable : 4100)
#endif

#endif
