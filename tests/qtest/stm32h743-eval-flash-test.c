/*
 * QTest testcase for the STM32H743 dual-bank FLASH controller
 * (hw/misc/stm32h7_flash.c) on the stm32h743-eval machine.
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Flash controller register addresses (hw/misc/stm32h7_flash.c) */
#define FLASH_BASE   0x52002000
#define FLASH_ACR    (FLASH_BASE + 0x00)
#define FLASH_KEYR1  (FLASH_BASE + 0x04)
#define FLASH_OPTKEYR (FLASH_BASE + 0x08)
#define FLASH_CR1    (FLASH_BASE + 0x0c)
#define FLASH_SR1    (FLASH_BASE + 0x10)
#define FLASH_CCR1   (FLASH_BASE + 0x14)
#define FLASH_OPTCR  (FLASH_BASE + 0x18)
#define FLASH_WPSN_CUR1R (FLASH_BASE + 0x38)
#define FLASH_WPSN_PRG1R (FLASH_BASE + 0x3c)

#define FLASH_KEYR2  (FLASH_BASE + 0x104)
#define FLASH_CR2    (FLASH_BASE + 0x10c)
#define FLASH_SR2    (FLASH_BASE + 0x110)

#define FLASH_KEY1 0x45670123
#define FLASH_KEY2 0xcdef89ab
#define FLASH_OPTKEY1 0x08192a3b
#define FLASH_OPTKEY2 0x4c5d6e7f

#define CR_LOCK  (1u << 0)
#define CR_PG    (1u << 1)
#define CR_SER   (1u << 2)
#define CR_BER   (1u << 3)
#define CR_START (1u << 7)
#define CR_SNB(n) ((uint32_t)(n) << 11)

#define SR_EOP     (1u << 4)
#define SR_WRPERR  (1u << 5)

#define OPTCR_OPTLOCK  (1u << 0)
#define OPTCR_OPTSTART (1u << 1)

#define FLASH_BANK1_BASE 0x08000000
#define FLASH_BANK2_BASE 0x08100000
#define FLASH_SECTOR_SIZE (128 * 1024)

/*
 * A trivial bare-metal "vector table + spin loop" payload, identical in
 * shape to the one used by stm32h743-eval-debug-test.c -- gives the CPU
 * something valid to fetch at reset (unprogrammed flash is a guest bug
 * QEMU reports as a fatal CPU lockup, same as real hardware would need
 * an external reset for). Built from stm32h743-eval-debug-test.S (idle
 * variant) with `clang -target arm-none-eabi -mcpu=cortex-m7 -mthumb
 * -nostdlib -static -Wl,-n`, stripped.
 */
static const uint8_t kernel_idle[] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x2c, 0x01, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x05, 0x34, 0x00, 0x20, 0x00, 0x03, 0x00, 0x28, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x40, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x08,
    0x40, 0x00, 0x00, 0x08, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x51, 0xe5, 0x74, 0x64,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0x41, 0x00, 0x00, 0x08,
    0x41, 0x00, 0x00, 0x08, 0x41, 0x00, 0x00, 0x08, 0x41, 0x00, 0x00, 0x08,
    0x41, 0x00, 0x00, 0x08, 0x41, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x00, 0x08, 0x41, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x00, 0x08, 0x41, 0x00, 0x00, 0x08, 0xfe, 0xe7, 0x41, 0x28,
    0x00, 0x00, 0x00, 0x61, 0x65, 0x61, 0x62, 0x69, 0x00, 0x01, 0x1e, 0x00,
    0x00, 0x00, 0x05, 0x63, 0x6f, 0x72, 0x74, 0x65, 0x78, 0x2d, 0x6d, 0x37,
    0x00, 0x06, 0x0d, 0x07, 0x4d, 0x08, 0x00, 0x09, 0x02, 0x0a, 0x08, 0x22,
    0x01, 0x24, 0x01, 0x00, 0x2e, 0x74, 0x65, 0x78, 0x74, 0x00, 0x2e, 0x41,
    0x52, 0x4d, 0x2e, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65,
    0x73, 0x00, 0x2e, 0x69, 0x73, 0x72, 0x5f, 0x76, 0x65, 0x63, 0x74, 0x6f,
    0x72, 0x00, 0x2e, 0x73, 0x68, 0x73, 0x74, 0x72, 0x74, 0x61, 0x62, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x94, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x08,
    0xd4, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xd6, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,
    0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static char *write_tmp_kernel(const uint8_t *data, size_t len)
{
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("qtest-stm32h743-eval-flash-XXXXXX", &path,
                             NULL);
    ssize_t wlen;

    g_assert(fd != -1);
    wlen = write(fd, data, len);
    g_assert(wlen == (ssize_t)len);
    close(fd);

    return g_steal_pointer(&path);
}

static QTestState *start_idle(void)
{
    g_autofree char *kern = write_tmp_kernel(kernel_idle, sizeof(kernel_idle));
    QTestState *qts = qtest_initf("-M stm32h743-eval -kernel %s "
                                  "-display none -serial none", kern);
    unlink(kern);
    return qts;
}

static void unlock_bank(QTestState *qts, uint32_t keyr)
{
    qtest_writel(qts, keyr, FLASH_KEY1);
    qtest_writel(qts, keyr, FLASH_KEY2);
}

static void unlock_opt(QTestState *qts)
{
    qtest_writel(qts, FLASH_OPTKEYR, FLASH_OPTKEY1);
    qtest_writel(qts, FLASH_OPTKEYR, FLASH_OPTKEY2);
}

static void test_flash_lock_default(void)
{
    QTestState *qts = start_idle();

    /* CR1/CR2 both locked at reset */
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR1) & CR_LOCK, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR2) & CR_LOCK, !=, 0);

    /* Writes to CR while locked are ignored */
    qtest_writel(qts, FLASH_CR1, CR_PG);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR1) & CR_PG, ==, 0);

    qtest_quit(qts);
}

static void test_flash_unlock_sequence(void)
{
    QTestState *qts = start_idle();

    /* Wrong-order/wrong-value writes leave the bank locked */
    qtest_writel(qts, FLASH_KEYR1, 0);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR1) & CR_LOCK, !=, 0);

    qtest_writel(qts, FLASH_KEYR1, FLASH_KEY1);
    qtest_writel(qts, FLASH_KEYR1, 0xdeadbeef);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR1) & CR_LOCK, !=, 0);

    /* Correct KEY1-then-KEY2 sequence unlocks */
    unlock_bank(qts, FLASH_KEYR1);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR1) & CR_LOCK, ==, 0);

    /* Bank 2 unlock is independent of bank 1 */
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR2) & CR_LOCK, !=, 0);
    unlock_bank(qts, FLASH_KEYR2);
    g_assert_cmpuint(qtest_readl(qts, FLASH_CR2) & CR_LOCK, ==, 0);

    qtest_quit(qts);
}

static void test_flash_program_word(void)
{
    QTestState *qts = start_idle();

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_PG);

    qtest_writel(qts, FLASH_BANK1_BASE + 0x1000, 0x11223344);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE + 0x1000), ==,
                     0x11223344);

    qtest_quit(qts);
}

static void test_flash_sector_erase(void)
{
    QTestState *qts = start_idle();

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_PG);
    qtest_writel(qts, FLASH_BANK1_BASE, 0x12345678);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE), ==, 0x12345678);

    qtest_writel(qts, FLASH_CR1, CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_EOP, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE), ==, 0xffffffff);

    /* A second sector is untouched */
    qtest_writel(qts, FLASH_BANK1_BASE + FLASH_SECTOR_SIZE, 0xaabbccdd);
    qtest_writel(qts, FLASH_CR1, CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE + FLASH_SECTOR_SIZE),
                     ==, 0xaabbccdd);

    qtest_quit(qts);
}

static void test_flash_write_protect(void)
{
    QTestState *qts = start_idle();

    unlock_opt(qts);
    /* Protect sector 0 (bit clear = protected); commit PRG -> CUR */
    qtest_writel(qts, FLASH_WPSN_PRG1R, 0xfe);
    qtest_writel(qts, FLASH_OPTCR, OPTCR_OPTSTART);
    g_assert_cmpuint(qtest_readl(qts, FLASH_WPSN_CUR1R), ==, 0xfe);

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_PG);
    qtest_writel(qts, FLASH_BANK1_BASE, 0x55aa55aa);

    qtest_writel(qts, FLASH_CR1, CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_WRPERR, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE), ==, 0x55aa55aa);

    /* CCR clears the error */
    qtest_writel(qts, FLASH_CCR1, SR_WRPERR);
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_WRPERR, ==, 0);

    qtest_quit(qts);
}

static void test_flash_bank2_addressing(void)
{
    QTestState *qts = start_idle();

    unlock_bank(qts, FLASH_KEYR2);
    qtest_writel(qts, FLASH_CR2, CR_PG);
    qtest_writel(qts, FLASH_BANK2_BASE, 0xdeadbeef);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK2_BASE), ==, 0xdeadbeef);

    /* Bank 1 is unaffected */
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK1_BASE), !=, 0xdeadbeef);

    qtest_writel(qts, FLASH_CR2, CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR2) & SR_EOP, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, FLASH_BANK2_BASE), ==, 0xffffffff);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/stm32h743-eval/flash/lock-default",
                   test_flash_lock_default);
    qtest_add_func("/stm32h743-eval/flash/unlock-sequence",
                   test_flash_unlock_sequence);
    qtest_add_func("/stm32h743-eval/flash/program-word",
                   test_flash_program_word);
    qtest_add_func("/stm32h743-eval/flash/sector-erase",
                   test_flash_sector_erase);
    qtest_add_func("/stm32h743-eval/flash/write-protect",
                   test_flash_write_protect);
    qtest_add_func("/stm32h743-eval/flash/bank2-addressing",
                   test_flash_bank2_addressing);

    return g_test_run();
}
