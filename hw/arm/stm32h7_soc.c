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
 *
 * This SoC model is deliberately minimal: it wires up the Cortex-M7 core,
 * NVIC, on-chip RAM/flash and a single USART for console output. Every
 * other peripheral is left as an unimplemented-device stub, to be replaced
 * by a real model in a follow-up patch as needed.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/arm/stm32h7_soc.h"
#include "hw/char/stm32l4x5_usart.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/unimp.h"

/* Main SYSCLK frequency in Hz (nominal 400MHz Cortex-M7 clock) */
#define SYSCLK_FRQ 400000000ULL

#define FLASH_BASE_ADDRESS 0x08000000

#define ITCM_RAM_BASE 0x00000000
#define ITCM_RAM_SIZE (64 * KiB)
#define DTCM_RAM_BASE 0x20000000
#define DTCM_RAM_SIZE (128 * KiB)
#define AXI_SRAM_BASE 0x24000000
#define AXI_SRAM_SIZE (512 * KiB)
#define SRAM1_BASE_ADDRESS 0x30000000
#define SRAM1_SIZE (128 * KiB)
#define SRAM2_BASE_ADDRESS 0x30020000
#define SRAM2_SIZE (128 * KiB)
#define SRAM3_BASE_ADDRESS 0x30040000
#define SRAM3_SIZE (32 * KiB)
#define SRAM4_BASE_ADDRESS 0x38000000
#define SRAM4_SIZE (64 * KiB)
#define BACKUP_SRAM_BASE 0x38800000
#define BACKUP_SRAM_SIZE (4 * KiB)

#define USART1_BASE_ADDRESS 0x40011000
#define USART1_IRQ 37

/* FLASH global interrupt; stable across the F4/F7/H7 lineage's vector table */
#define FLASH_IRQ 4

/*
 * TIM2, at its real STM32H7 address/IRQ -- stable across the whole
 * F2/F4/F7/H7 lineage sharing this general-purpose timer IP. The only
 * real interrupt source on this SoC model besides USART1; lets guest
 * firmware (and test harnesses) exercise a genuinely repeatedly-firing
 * NVIC-dispatched interrupt instead of only a one-shot software pend.
 */
#define TIM2_BASE_ADDRESS 0x40000000
#define TIM2_IRQ 28

/*
 * 1MHz: a clean, test-friendly choice, not a real derived clock -- this
 * SoC model has no functional RCC clock tree to derive one from (same
 * simplification SYSCLK's fixed 400MHz already makes).
 */
#define TIM2_CLOCK_FRQ 1000000

static void stm32h7_soc_initfn(Object *obj)
{
    Stm32h7SocState *s = STM32H7_SOC(obj);

    object_initialize_child(obj, "usart1", &s->usart1, TYPE_STM32L4X5_USART);
    object_initialize_child(obj, "timer2", &s->timer2, TYPE_STM32F2XX_TIMER);
}

static void stm32h7_soc_realize(DeviceState *dev_soc, Error **errp)
{
    Stm32h7SocState *s = STM32H7_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *armv7m, *dev;
    SysBusDevice *busdev;

    /* This clock doesn't need migration because it is fixed-frequency */
    s->sysclk = clock_new(OBJECT(dev_soc), "SYSCLK");
    clock_set_hz(s->sysclk, SYSCLK_FRQ);

    if (!memory_region_init_ram(&s->itcm, OBJECT(dev_soc), "ITCM-RAM",
                                ITCM_RAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, ITCM_RAM_BASE, &s->itcm);

    if (!memory_region_init_ram(&s->dtcm, OBJECT(dev_soc), "DTCM-RAM",
                                DTCM_RAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, DTCM_RAM_BASE, &s->dtcm);

    if (!memory_region_init_ram(&s->axi_sram, OBJECT(dev_soc), "AXI-SRAM",
                                AXI_SRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, AXI_SRAM_BASE, &s->axi_sram);

    if (!memory_region_init_ram(&s->sram1, OBJECT(dev_soc), "SRAM1",
                                SRAM1_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, SRAM1_BASE_ADDRESS, &s->sram1);

    if (!memory_region_init_ram(&s->sram2, OBJECT(dev_soc), "SRAM2",
                                SRAM2_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, SRAM2_BASE_ADDRESS, &s->sram2);

    if (!memory_region_init_ram(&s->sram3, OBJECT(dev_soc), "SRAM3",
                                SRAM3_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, SRAM3_BASE_ADDRESS, &s->sram3);

    if (!memory_region_init_ram(&s->sram4, OBJECT(dev_soc), "SRAM4",
                                SRAM4_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, SRAM4_BASE_ADDRESS, &s->sram4);

    if (!memory_region_init_ram(&s->backup_sram, OBJECT(dev_soc),
                                "Backup-SRAM", BACKUP_SRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(system_memory, BACKUP_SRAM_BASE,
                                &s->backup_sram);

    /* Cortex-M7 core + NVIC */
    object_initialize_child(OBJECT(dev_soc), "armv7m", &s->armv7m,
                            TYPE_ARMV7M);
    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 160);
    qdev_prop_set_uint32(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m7"));
    /*
     * Cortex-M7 has no Security Extension, so the NS VTOR is the only
     * VTOR; point it at flash so the reset vector table is fetched from
     * there instead of from the ITCM, which occupies address 0.
     */
    qdev_prop_set_uint32(armv7m, "init-nsvtor", FLASH_BASE_ADDRESS);
    /*
     * Real STM32H7 silicon implements the Cortex-M7's Flash Patch and
     * Breakpoint (FPB) and Data Watchpoint and Trace (DWT) units for
     * architectural "Monitor mode debugging" -- expose them so guest
     * debug-monitor firmware can use real hardware breakpoints and
     * watchpoints, not just QEMU's own external gdbstub.
     */
    qdev_prop_set_bit(armv7m, "has-fpb", true);
    qdev_prop_set_bit(armv7m, "has-dwt", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", s->sysclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    /* USART1 device, the only real (non-stub) peripheral in this SoC */
    dev = DEVICE(&s->usart1);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    qdev_connect_clock_in(dev, "clk", s->sysclk);
    busdev = SYS_BUS_DEVICE(dev);
    if (!sysbus_realize(busdev, errp)) {
        return;
    }
    sysbus_mmio_map(busdev, 0, USART1_BASE_ADDRESS);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, USART1_IRQ));

    /* TIM2: the only real interrupt source on this SoC besides USART1 */
    dev = DEVICE(&s->timer2);
    qdev_prop_set_uint64(dev, "clock-frequency", TIM2_CLOCK_FRQ);
    busdev = SYS_BUS_DEVICE(dev);
    if (!sysbus_realize(busdev, errp)) {
        return;
    }
    sysbus_mmio_map(busdev, 0, TIM2_BASE_ADDRESS);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, TIM2_IRQ));

    /* D3 domain */
    create_unimplemented_device("PWR",    0x58024800, 0x400);
    create_unimplemented_device("RCC",    0x58024400, 0x400);
    create_unimplemented_device("SYSCFG", 0x58000400, 0x400);
    create_unimplemented_device("EXTI",   0x58000000, 0x400);

    /* AHB4 GPIO A..K, stride 0x400 @ 0x58020000 */
    static const char * const gpio_name[] = {
        "GPIOA", "GPIOB", "GPIOC", "GPIOD", "GPIOE", "GPIOF",
        "GPIOG", "GPIOH", "GPIOI", "GPIOJ", "GPIOK",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(gpio_name); i++) {
        create_unimplemented_device(gpio_name[i], 0x58020000 + i * 0x400,
                                    0x400);
    }

    /* APB1/APB2 UARTs other than USART1 */
    create_unimplemented_device("USART2", 0x40004400, 0x400);
    create_unimplemented_device("USART3", 0x40004800, 0x400);
    create_unimplemented_device("UART4",  0x40004C00, 0x400);
    create_unimplemented_device("UART5",  0x40005000, 0x400);
    create_unimplemented_device("USART6", 0x40011400, 0x400);
    create_unimplemented_device("UART7",  0x40007800, 0x400);
    create_unimplemented_device("UART8",  0x40007C00, 0x400);

    /*
     * Dual-bank flash controller: real registers plus the two banks'
     * guest-writable backing storage (see hw/misc/stm32h7_flash.c). A
     * -kernel image is loaded into bank 1 below, after this device is
     * realized and its bank regions are mapped.
     */
    object_initialize_child(OBJECT(dev_soc), "flash-ctrl", &s->flash_ctrl,
                            TYPE_STM32H7_FLASH);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->flash_ctrl), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->flash_ctrl), 0, 0x52002000);
    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS,
                                stm32h7_flash_get_bank(&s->flash_ctrl, 0));
    memory_region_add_subregion(system_memory,
                                FLASH_BASE_ADDRESS + STM32H7_FLASH_BANK_SIZE,
                                stm32h7_flash_get_bank(&s->flash_ctrl, 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->flash_ctrl), 0,
                       qdev_get_gpio_in(armv7m, FLASH_IRQ));

    /*
     * Debug MCU ID and the factory unique ID: real, always-present
     * peripherals on actual STM32H7 silicon, not board-specific wiring
     * like the GPIO/UART stubs above. Guest firmware (e.g. gdbserver's
     * own HAL) can reasonably read these unconditionally at boot
     * expecting them to always be there, same as on real hardware --
     * stubbing them here (rather than leaving this range genuinely
     * unmapped) lets the machine raise a real BusFault for addresses
     * nothing on this SoC actually claims, instead of needing
     * ignore_memory_transaction_failures as a blanket safety net.
     */
    create_unimplemented_device("DBGMCU", 0x5c001000, 0x400);
    create_unimplemented_device("UID",    0x1ff1e800, 0x20);
}

static void stm32h7_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32h7_soc_realize;
    /* Reason: Mapped at fixed location on the system bus */
    dc->user_creatable = false;
    /* No vmstate or reset required: device has no internal state */
}

static void stm32h743_soc_class_init(ObjectClass *oc, const void *data)
{
    Stm32h7SocClass *ssc = STM32H7_SOC_CLASS(oc);

    ssc->flash_size = 2 * MiB;
}

static const TypeInfo stm32h7_soc_types[] = {
    {
        .name           = TYPE_STM32H743_SOC,
        .parent         = TYPE_STM32H7_SOC,
        .class_init     = stm32h743_soc_class_init,
    }, {
        .name           = TYPE_STM32H7_SOC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32h7SocState),
        .instance_init  = stm32h7_soc_initfn,
        .class_size     = sizeof(Stm32h7SocClass),
        .class_init     = stm32h7_soc_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(stm32h7_soc_types)
