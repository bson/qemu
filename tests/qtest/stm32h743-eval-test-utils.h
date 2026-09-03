/*
 * Shared qtest helpers for the stm32h743-eval machine
 * (stm32h743-eval-debug-test.c, stm32h743-eval-flash-test.c).
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TESTS_QTEST_STM32H743_EVAL_TEST_UTILS_H
#define TESTS_QTEST_STM32H743_EVAL_TEST_UTILS_H

#include "libqtest.h"

/*
 * A trivial bare-metal "vector table + spin loop" payload, giving the CPU
 * something valid to fetch at reset (flash is unprogrammed otherwise,
 * which is a guest bug that QEMU reports as a fatal CPU lockup, same as
 * real hardware would need an external reset for). Built from
 * stm32h743-eval-debug-test.S (idle variant) with `clang -target
 * arm-none-eabi -mcpu=cortex-m7 -mthumb -nostdlib -static -Wl,-n`,
 * stripped.
 */
extern const uint8_t kernel_idle[];
extern const size_t kernel_idle_len;

/* Writes data/len to a fresh temp file; caller g_free()s the returned path. */
char *write_tmp_kernel(const uint8_t *data, size_t len);

/* Boots stm32h743-eval with the idle payload above. */
QTestState *start_idle(void);

#endif
