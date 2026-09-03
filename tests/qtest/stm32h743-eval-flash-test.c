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
#include "stm32h743-eval-test-utils.h"

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
#define CR_SNB(n) ((uint32_t)(n) << 8)
#define CR_EOPIE     (1u << 16)
#define CR_WRPERRIE  (1u << 17)

#define SR_EOP     (1u << 16)
#define SR_WRPERR  (1u << 17)

#define OPTCR_OPTLOCK  (1u << 0)
#define OPTCR_OPTSTART (1u << 1)

#define FLASH_BANK1_BASE 0x08000000
#define FLASH_BANK2_BASE 0x08100000
#define FLASH_SECTOR_SIZE (128 * 1024)

/* NVIC_IRQn 4: FLASH global interrupt (hw/arm/stm32h7_soc.c FLASH_IRQ) */
#define FLASH_IRQ 4
#define NVIC_ISPR 0xe000e200
#define NVIC_ICPR 0xe000e280

static bool check_nvic_pending(QTestState *qts, unsigned int n)
{
    return qtest_readl(qts, NVIC_ISPR) & (1u << n);
}

/*
 * NVIC pending is a level-triggered latch: once set it persists until
 * explicitly cleared here, even after the peripheral's IRQ line has gone
 * back low -- deasserting the source alone (e.g. clearing SR via CCR)
 * does not auto-clear it.
 */
static void unpend_nvic_irq(QTestState *qts, unsigned int n)
{
    qtest_writel(qts, NVIC_ICPR, 1u << n);
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

static void test_flash_irq_eop(void)
{
    QTestState *qts = start_idle();

    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_PG | CR_EOPIE);
    qtest_writel(qts, FLASH_BANK1_BASE, 0x12345678);

    qtest_writel(qts, FLASH_CR1, CR_EOPIE | CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_EOP, !=, 0);
    g_assert_true(check_nvic_pending(qts, FLASH_IRQ));

    /*
     * Clearing SR.EOP drops the source line, but NVIC pending stays
     * latched until explicitly unpended -- standard ARMv7-M behavior.
     */
    qtest_writel(qts, FLASH_CCR1, SR_EOP);
    g_assert_true(check_nvic_pending(qts, FLASH_IRQ));
    unpend_nvic_irq(qts, FLASH_IRQ);
    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));

    qtest_quit(qts);
}

static void test_flash_irq_error(void)
{
    QTestState *qts = start_idle();

    unlock_opt(qts);
    qtest_writel(qts, FLASH_WPSN_PRG1R, 0xfe);
    qtest_writel(qts, FLASH_OPTCR, OPTCR_OPTSTART);

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_WRPERRIE);

    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));
    qtest_writel(qts, FLASH_CR1, CR_WRPERRIE | CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_WRPERR, !=, 0);
    g_assert_true(check_nvic_pending(qts, FLASH_IRQ));

    qtest_writel(qts, FLASH_CCR1, SR_WRPERR);
    unpend_nvic_irq(qts, FLASH_IRQ);
    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));

    qtest_quit(qts);
}

static void test_flash_irq_masked(void)
{
    QTestState *qts = start_idle();

    unlock_bank(qts, FLASH_KEYR1);
    qtest_writel(qts, FLASH_CR1, CR_PG);
    qtest_writel(qts, FLASH_BANK1_BASE, 0x12345678);

    /* EOPIE left clear: SR.EOP sets, but the shared IRQ stays deasserted */
    qtest_writel(qts, FLASH_CR1, CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR1) & SR_EOP, !=, 0);
    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));

    qtest_quit(qts);
}

static void test_flash_irq_bank_independent_source(void)
{
    QTestState *qts = start_idle();

    unlock_bank(qts, FLASH_KEYR2);
    qtest_writel(qts, FLASH_CR2, CR_PG | CR_EOPIE);
    qtest_writel(qts, FLASH_BANK2_BASE, 0xdeadbeef);

    g_assert_false(check_nvic_pending(qts, FLASH_IRQ));
    qtest_writel(qts, FLASH_CR2, CR_EOPIE | CR_SER | CR_START | CR_SNB(0));
    g_assert_cmpuint(qtest_readl(qts, FLASH_SR2) & SR_EOP, !=, 0);
    g_assert_true(check_nvic_pending(qts, FLASH_IRQ));

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
    qtest_add_func("/stm32h743-eval/flash/irq-eop", test_flash_irq_eop);
    qtest_add_func("/stm32h743-eval/flash/irq-error", test_flash_irq_error);
    qtest_add_func("/stm32h743-eval/flash/irq-masked", test_flash_irq_masked);
    qtest_add_func("/stm32h743-eval/flash/irq-bank-independent-source",
                   test_flash_irq_bank_independent_source);

    return g_test_run();
}
