/*
 * STM32H743 evaluation machine
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a synthetic bring-up target for the STM32H7 SoC model, not a
 * model of any specific ST reference board.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/stm32h7_soc.h"

#define TYPE_STM32H743_EVAL MACHINE_TYPE_NAME("stm32h743-eval")
OBJECT_DECLARE_SIMPLE_TYPE(Stm32h743EvalMachineState, STM32H743_EVAL)

struct Stm32h743EvalMachineState {
    MachineState parent_obj;

    Stm32h7SocState soc;
};

static void stm32h743_eval_init(MachineState *machine)
{
    Stm32h743EvalMachineState *s = STM32H743_EVAL(machine);
    const Stm32h7SocClass *sc;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_STM32H743_SOC);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    sc = STM32H7_SOC_GET_CLASS(&s->soc);
    armv7m_load_kernel(s->soc.armv7m.cpu, machine->kernel_filename, 0,
                       sc->flash_size);
}

static void stm32h743_eval_machine_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m7"),
        NULL
    };

    mc->desc = "STM32H743 evaluation target (Cortex-M7)";
    mc->init = stm32h743_eval_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->ignore_memory_transaction_failures = true;

    /* RAM/flash are pre-allocated as part of the SoC instantiation */
    mc->default_ram_size = 0;
}

static const TypeInfo stm32h743_eval_machine_type[] = {
    {
        .name           = TYPE_STM32H743_EVAL,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(Stm32h743EvalMachineState),
        .class_init     = stm32h743_eval_machine_init,
        .interfaces     = arm_machine_interfaces,
    }
};

DEFINE_TYPES(stm32h743_eval_machine_type)
