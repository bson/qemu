/*
 * STM32H743 dual-bank FLASH controller
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * This is a model of the STM32H743 embedded FLASH controller (RM0433
 * section 4, register block at 0x52002000), plus the two 1MiB flash bank
 * arrays it drives. The STM32H743's 2MiB flash is always organized as two
 * fixed banks of 8 x 128KiB sectors each (unlike some other H7 variants,
 * it has no configurable single-bank mode), so that fixed geometry is
 * built in rather than modeled as a configuration option.
 *
 * Modeled: per-bank KEYR unlock sequence gating CR.LOCK, PG/SER/BER
 * program and erase, SR/CCR status bookkeeping, per-sector write
 * protection (WPSN), bank swapping (OPTCR.SWAP_BANK), and the per-bank
 * CR interrupt-enable bits driving the single shared FLASH_IRQn line.
 *
 * Deliberately not modeled: read protection (RDP) enforcement, ECC,
 * ICACHE/DCACHE/ART interaction, and the CRC engine/PCROP/secure-area
 * registers. Program/erase operations complete synchronously within the
 * register write that triggers them -- no operation-latency timer is
 * modeled, matching every other register-gated device in this SoC.
 *
 * Direct guest stores into the mapped bank MemoryRegions succeed
 * regardless of CR.PG, since QEMU's plain RAM regions can't cheaply fault
 * on the CPU-store path the way real silicon bus-faults an unlocked or
 * non-PG write; only sector/bank erase is actually mediated by this
 * device.
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank at 0x52002000
 *  + sysbus IRQ 0: FLASH_IRQn, the single interrupt line shared by both
 *    banks (real silicon has no per-bank FLASH interrupt)
 *  + stm32h7_flash_get_bank(): the RAM MemoryRegion currently mapped for
 *    a given logical bank (0 or 1), for the SoC to subregion into the
 *    system address space
 */

#ifndef HW_MISC_STM32H7_FLASH_H
#define HW_MISC_STM32H7_FLASH_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32H7_FLASH "stm32h7-flash"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32h7FlashState, STM32H7_FLASH)

#define STM32H7_FLASH_NUM_BANKS 2
#define STM32H7_FLASH_BANK_SIZE (1 * 1024 * 1024)
#define STM32H7_FLASH_SECTOR_SIZE (128 * 1024)
#define STM32H7_FLASH_SECTORS_PER_BANK \
    (STM32H7_FLASH_BANK_SIZE / STM32H7_FLASH_SECTOR_SIZE)

struct Stm32h7FlashState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    MemoryRegion bank_ram[STM32H7_FLASH_NUM_BANKS];
    qemu_irq irq;

    uint32_t acr;
    uint32_t cr[STM32H7_FLASH_NUM_BANKS];
    uint32_t sr[STM32H7_FLASH_NUM_BANKS];
    uint32_t wpsn_cur[STM32H7_FLASH_NUM_BANKS];
    uint32_t wpsn_prg[STM32H7_FLASH_NUM_BANKS];
    /* KEYR unlock sequence state per bank: 0 = idle, 1 = KEY1 seen */
    uint8_t key_state[STM32H7_FLASH_NUM_BANKS];

    uint32_t optcr;
    uint32_t optsr_cur;
    uint32_t optsr_prg;
    uint8_t optkey_state;

    /* true once OPTCR.SWAP_BANK has swapped which array maps where */
    bool swap_bank;
};

/*
 * Returns the MemoryRegion currently mapped at the given logical
 * address-space bank slot (0 = 0x08000000, 1 = 0x08100000), accounting
 * for OPTCR.SWAP_BANK.
 */
MemoryRegion *stm32h7_flash_get_bank(Stm32h7FlashState *s, unsigned slot);

#endif
