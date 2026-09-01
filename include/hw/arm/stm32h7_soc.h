/*
 * STM32H7 SoC family
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * The reference used is the STMicroElectronics RM0433 Reference manual
 * for STM32H742, STM32H743/753 and STM32H750 advanced Arm(R)-based
 * 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32h743-753/documentation.html
 */

#ifndef HW_ARM_STM32H7_SOC_H
#define HW_ARM_STM32H7_SOC_H

#include "system/memory.h"
#include "hw/arm/armv7m.h"
#include "hw/char/stm32l4x5_usart.h"
#include "qom/object.h"

#define TYPE_STM32H7_SOC "stm32h7-soc"
#define TYPE_STM32H743_SOC "stm32h743-soc"
OBJECT_DECLARE_TYPE(Stm32h7SocState, Stm32h7SocClass, STM32H7_SOC)

struct Stm32h7SocState {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    Stm32l4x5UsartBaseState usart1;

    Clock *sysclk;

    MemoryRegion itcm;
    MemoryRegion dtcm;
    MemoryRegion axi_sram;
    MemoryRegion sram1;
    MemoryRegion sram2;
    MemoryRegion sram3;
    MemoryRegion sram4;
    MemoryRegion backup_sram;
    MemoryRegion flash;
};

struct Stm32h7SocClass {
    SysBusDeviceClass parent_class;

    size_t flash_size;
};

#endif
