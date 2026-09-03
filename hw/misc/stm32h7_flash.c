/*
 * STM32H743 dual-bank FLASH controller
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the STMicroElectronics RM0433 Reference manual,
 * section 4 "Embedded Flash memory (FLASH)".
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/stm32h7_flash.h"
#include "hw/core/registerfields.h"
#include "hw/core/irq.h"
#include "qemu/log.h"

/*
 * Bank-1 register offsets; each has an identically-shaped bank-2 sibling
 * at the same offset + 0x100 (e.g. FLASH_CR1 at 0x0C, FLASH_CR2 at
 * 0x10C). FLASH_ACR/OPTKEYR/OPTCR/OPTSR_* are shared across both banks
 * and have no +0x100 counterpart.
 */
REG32(FLASH_ACR, 0x00)
REG32(FLASH_KEYR, 0x04)
REG32(FLASH_OPTKEYR, 0x08)
REG32(FLASH_CR, 0x0c)
    FIELD(FLASH_CR, LOCK, 0, 1)
    FIELD(FLASH_CR, PG, 1, 1)
    FIELD(FLASH_CR, SER, 2, 1)
    FIELD(FLASH_CR, BER, 3, 1)
    FIELD(FLASH_CR, PSIZE, 4, 2)
    FIELD(FLASH_CR, FW, 6, 1)
    FIELD(FLASH_CR, START, 7, 1)
    FIELD(FLASH_CR, SNB, 8, 3)
    /*
     * Interrupt-enable bits share the exact bit positions of their
     * FLASH_SR status-bit counterparts below -- real RM0433 layout, not a
     * simplification -- so a plain "SR & CR" is the pending-interrupt mask.
     */
    FIELD(FLASH_CR, EOPIE, 16, 1)
    FIELD(FLASH_CR, WRPERRIE, 17, 1)
    FIELD(FLASH_CR, PGSERRIE, 18, 1)
    FIELD(FLASH_CR, STRBERRIE, 19, 1)
    FIELD(FLASH_CR, INCERRIE, 21, 1)
    FIELD(FLASH_CR, OPERRIE, 22, 1)
    FIELD(FLASH_CR, RDPERRIE, 23, 1)
    FIELD(FLASH_CR, RDSERRIE, 24, 1)
REG32(FLASH_SR, 0x10)
    FIELD(FLASH_SR, BSY, 0, 1)
    FIELD(FLASH_SR, QW, 2, 1)
    FIELD(FLASH_SR, EOP, 16, 1)
    FIELD(FLASH_SR, WRPERR, 17, 1)
    FIELD(FLASH_SR, PGSERR, 18, 1)
    FIELD(FLASH_SR, STRBERR, 19, 1)
    FIELD(FLASH_SR, INCERR, 21, 1)
    FIELD(FLASH_SR, OPERR, 22, 1)
    FIELD(FLASH_SR, RDPERR, 23, 1)
    FIELD(FLASH_SR, RDSERR, 24, 1)
REG32(FLASH_CCR, 0x14)
REG32(FLASH_OPTCR, 0x18)
    FIELD(FLASH_OPTCR, OPTLOCK, 0, 1)
    FIELD(FLASH_OPTCR, OPTSTART, 1, 1)
REG32(FLASH_OPTSR_CUR, 0x1c)
REG32(FLASH_OPTSR_PRG, 0x20)
    /* Same bit position in both OPTSR_CUR and OPTSR_PRG; RM0433 has no
     * SWAP_BANK field in OPTCR itself -- it's staged in OPTSR_PRG and
     * committed to OPTSR_CUR by OPTCR.OPTSTART, like every other option
     * byte. */
    FIELD(FLASH_OPTSR_PRG, SWAP_BANK_OPT, 31, 1)
REG32(FLASH_WPSN_CUR, 0x38)
REG32(FLASH_WPSN_PRG, 0x3c)

/* SR bits clearable by writing 1 to the same bit position in CCR */
#define FLASH_SR_CLEARABLE_MASK \
    (R_FLASH_SR_EOP_MASK | R_FLASH_SR_WRPERR_MASK | R_FLASH_SR_PGSERR_MASK | \
     R_FLASH_SR_STRBERR_MASK | R_FLASH_SR_INCERR_MASK | \
     R_FLASH_SR_OPERR_MASK | R_FLASH_SR_RDPERR_MASK | R_FLASH_SR_RDSERR_MASK)

/*
 * CR interrupt-enable bits, at the same bit positions as their SR
 * counterparts (real RM0433 layout) -- so "SR & CR & this mask" is
 * directly the set of currently-interrupting conditions.
 */
#define FLASH_CR_IE_MASK FLASH_SR_CLEARABLE_MASK

#define FLASH_KEY1 0x45670123
#define FLASH_KEY2 0xcdef89ab
#define FLASH_OPTKEY1 0x08192a3b
#define FLASH_OPTKEY2 0x4c5d6e7f

#define BANK_REG_STRIDE 0x100

MemoryRegion *stm32h7_flash_get_bank(Stm32h7FlashState *s, unsigned slot)
{
    unsigned n = s->swap_bank ? !slot : slot;

    return &s->bank_ram[n];
}

/*
 * Real silicon has a single shared FLASH_IRQn covering both banks, so the
 * line reflects the OR of both banks' currently-interrupting conditions.
 */
static void stm32h7_flash_update_irq(Stm32h7FlashState *s)
{
    uint32_t pending = 0;
    unsigned n;

    for (n = 0; n < STM32H7_FLASH_NUM_BANKS; n++) {
        pending |= s->sr[n] & s->cr[n] & FLASH_CR_IE_MASK;
    }

    qemu_set_irq(s->irq, pending != 0);
}

static void flash_erase_sector(Stm32h7FlashState *s, unsigned n, unsigned snb)
{
    void *ram;

    if (snb >= STM32H7_FLASH_SECTORS_PER_BANK) {
        s->sr[n] |= R_FLASH_SR_OPERR_MASK;
        return;
    }
    if (!((s->wpsn_cur[n] >> snb) & 1)) {
        s->sr[n] |= R_FLASH_SR_WRPERR_MASK;
        return;
    }

    ram = memory_region_get_ram_ptr(&s->bank_ram[n]);
    memset(ram + snb * STM32H7_FLASH_SECTOR_SIZE, 0xff,
           STM32H7_FLASH_SECTOR_SIZE);
    s->sr[n] |= R_FLASH_SR_EOP_MASK;
}

static void flash_erase_bank(Stm32h7FlashState *s, unsigned n)
{
    void *ram;

    if (s->wpsn_cur[n] != 0xff) {
        s->sr[n] |= R_FLASH_SR_WRPERR_MASK;
        return;
    }

    ram = memory_region_get_ram_ptr(&s->bank_ram[n]);
    memset(ram, 0xff, STM32H7_FLASH_BANK_SIZE);
    s->sr[n] |= R_FLASH_SR_EOP_MASK;
}

static void flash_cr_write(Stm32h7FlashState *s, unsigned n, uint32_t value)
{
    if (s->cr[n] & R_FLASH_CR_LOCK_MASK) {
        /* Controller stays locked: writes to CR are ignored */
        return;
    }

    s->cr[n] = value & (R_FLASH_CR_LOCK_MASK | R_FLASH_CR_PG_MASK |
                        R_FLASH_CR_SER_MASK | R_FLASH_CR_BER_MASK |
                        R_FLASH_CR_PSIZE_MASK | R_FLASH_CR_FW_MASK |
                        R_FLASH_CR_SNB_MASK | FLASH_CR_IE_MASK);

    if (value & R_FLASH_CR_START_MASK) {
        unsigned snb = (s->cr[n] & R_FLASH_CR_SNB_MASK) >>
                       R_FLASH_CR_SNB_SHIFT;

        if (s->cr[n] & R_FLASH_CR_BER_MASK) {
            flash_erase_bank(s, n);
        } else if (s->cr[n] & R_FLASH_CR_SER_MASK) {
            flash_erase_sector(s, n, snb);
        }
    }

    /*
     * Covers both a new EOP/error status from the erase above and an IE
     * bit toggle unmasking/masking an already-pending condition.
     */
    stm32h7_flash_update_irq(s);
}

static void flash_keyr_write(Stm32h7FlashState *s, unsigned n, uint32_t value)
{
    if (!(s->cr[n] & R_FLASH_CR_LOCK_MASK)) {
        /* Already unlocked: further key writes are ignored */
        return;
    }

    if (s->key_state[n] == 0) {
        s->key_state[n] = (value == FLASH_KEY1);
    } else {
        if (value == FLASH_KEY2) {
            s->cr[n] &= ~R_FLASH_CR_LOCK_MASK;
        }
        s->key_state[n] = 0;
    }
}

static void flash_optkeyr_write(Stm32h7FlashState *s, uint32_t value)
{
    if (!(s->optcr & R_FLASH_OPTCR_OPTLOCK_MASK)) {
        return;
    }

    if (s->optkey_state == 0) {
        s->optkey_state = (value == FLASH_OPTKEY1);
    } else {
        if (value == FLASH_OPTKEY2) {
            s->optcr &= ~R_FLASH_OPTCR_OPTLOCK_MASK;
        }
        s->optkey_state = 0;
    }
}

static void flash_optcr_write(Stm32h7FlashState *s, uint32_t value)
{
    if (s->optcr & R_FLASH_OPTCR_OPTLOCK_MASK) {
        return;
    }

    /* OPTSTART is a strobe, not stored back */
    s->optcr = value & R_FLASH_OPTCR_OPTLOCK_MASK;

    if (value & R_FLASH_OPTCR_OPTSTART_MASK) {
        /* Option-byte "programming" is immediate: commit PRG to CUR */
        s->optsr_cur = s->optsr_prg;
        s->wpsn_cur[0] = s->wpsn_prg[0];
        s->wpsn_cur[1] = s->wpsn_prg[1];
        s->swap_bank = !!(s->optsr_cur & R_FLASH_OPTSR_PRG_SWAP_BANK_OPT_MASK);
    }
}

static MemTxResult stm32h7_flash_read(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    Stm32h7FlashState *s = opaque;
    unsigned n;

    switch (addr) {
    case A_FLASH_ACR:
        *data = s->acr;
        return MEMTX_OK;
    case A_FLASH_OPTKEYR:
        *data = 0;
        return MEMTX_OK;
    case A_FLASH_OPTCR:
        *data = s->optcr;
        return MEMTX_OK;
    case A_FLASH_OPTSR_CUR:
        *data = s->optsr_cur;
        return MEMTX_OK;
    case A_FLASH_OPTSR_PRG:
        *data = s->optsr_prg;
        return MEMTX_OK;
    }

    for (n = 0; n < STM32H7_FLASH_NUM_BANKS; n++) {
        hwaddr rel = addr - n * BANK_REG_STRIDE;

        switch (rel) {
        case A_FLASH_KEYR:
            *data = 0;
            return MEMTX_OK;
        case A_FLASH_CR:
            *data = s->cr[n];
            return MEMTX_OK;
        case A_FLASH_SR:
            *data = s->sr[n];
            return MEMTX_OK;
        case A_FLASH_CCR:
            *data = 0;
            return MEMTX_OK;
        case A_FLASH_WPSN_CUR:
            *data = s->wpsn_cur[n];
            return MEMTX_OK;
        case A_FLASH_WPSN_PRG:
            *data = s->wpsn_prg[n];
            return MEMTX_OK;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "STM32H7 FLASH: read of unimplemented offset 0x%x\n",
                  (uint32_t)addr);
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult stm32h7_flash_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    Stm32h7FlashState *s = opaque;
    unsigned n;

    switch (addr) {
    case A_FLASH_ACR:
        s->acr = value;
        return MEMTX_OK;
    case A_FLASH_OPTKEYR:
        flash_optkeyr_write(s, value);
        return MEMTX_OK;
    case A_FLASH_OPTCR:
        flash_optcr_write(s, value);
        return MEMTX_OK;
    case A_FLASH_OPTSR_PRG:
        s->optsr_prg = value;
        return MEMTX_OK;
    }

    for (n = 0; n < STM32H7_FLASH_NUM_BANKS; n++) {
        hwaddr rel = addr - n * BANK_REG_STRIDE;

        switch (rel) {
        case A_FLASH_KEYR:
            flash_keyr_write(s, n, value);
            return MEMTX_OK;
        case A_FLASH_CR:
            flash_cr_write(s, n, value);
            return MEMTX_OK;
        case A_FLASH_CCR:
            s->sr[n] &= ~(value & FLASH_SR_CLEARABLE_MASK);
            stm32h7_flash_update_irq(s);
            return MEMTX_OK;
        case A_FLASH_WPSN_PRG:
            s->wpsn_prg[n] = value & 0xff;
            return MEMTX_OK;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "STM32H7 FLASH: write of unimplemented offset 0x%x\n",
                  (uint32_t)addr);
    return MEMTX_OK;
}

static const MemoryRegionOps stm32h7_flash_ops = {
    .read_with_attrs = stm32h7_flash_read,
    .write_with_attrs = stm32h7_flash_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * Only the volatile control/status registers reset here -- optsr_cur/
 * optsr_prg/swap_bank/wpsn_cur/wpsn_prg are option bytes, backed by
 * flash the same way bank_ram[] is: on real silicon they survive an
 * ordinary system reset (that's the entire point of "monitor swap"
 * ending in a reset rather than taking effect immediately -- see
 * gdbserver's monitor_swap()), and this reset_hold runs on every system
 * reset, not just cold power-up. They're given their power-up default
 * (option bytes erased, bank 0 active) once, in stm32h7_flash_init()
 * below, instead.
 */
static void stm32h7_flash_reset_hold(Object *obj, ResetType type)
{
    Stm32h7FlashState *s = STM32H7_FLASH(obj);
    unsigned n;

    s->acr = 0;
    s->optcr = R_FLASH_OPTCR_OPTLOCK_MASK;
    s->optkey_state = 0;

    for (n = 0; n < STM32H7_FLASH_NUM_BANKS; n++) {
        s->cr[n] = R_FLASH_CR_LOCK_MASK;
        s->sr[n] = 0;
        s->key_state[n] = 0;
    }

    stm32h7_flash_update_irq(s);
}

static void stm32h7_flash_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Stm32h7FlashState *s = STM32H7_FLASH(obj);
    unsigned n;

    memory_region_init_io(&s->iomem, obj, &stm32h7_flash_ops,
                          s, "stm32h7-flash", 0x400);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->optsr_cur = 0;
    s->optsr_prg = 0;
    s->swap_bank = false;

    for (n = 0; n < STM32H7_FLASH_NUM_BANKS; n++) {
        g_autofree char *name = g_strdup_printf("stm32h7-flash-bank%u", n);

        memory_region_init_ram(&s->bank_ram[n], obj, name,
                               STM32H7_FLASH_BANK_SIZE, &error_abort);

        /* 0xff: no sectors write-protected, matching erased option bytes */
        s->wpsn_cur[n] = 0xff;
        s->wpsn_prg[n] = 0xff;
    }
}

static void stm32h7_flash_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = stm32h7_flash_reset_hold;
}

static const TypeInfo stm32h7_flash_info = {
    .name = TYPE_STM32H7_FLASH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Stm32h7FlashState),
    .instance_init = stm32h7_flash_init,
    .class_init = stm32h7_flash_class_init,
};

static void stm32h7_flash_register_types(void)
{
    type_register_static(&stm32h7_flash_info);
}

type_init(stm32h7_flash_register_types);
