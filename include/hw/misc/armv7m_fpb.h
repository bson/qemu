/*
 * ARMv7-M Flash Patch and Breakpoint (FPB) unit
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * This is a model of the FPB register block of an M-profile CPU
 * (the registers starting at 0xE0002000). It implements FPB
 * architecture version 2 (as found on Cortex-M7), which provides
 * instruction-address hardware breakpoint comparators only -- FPBv2
 * removes the literal-address remap comparators that FPBv1 (Cortex-M3/
 * M4) has, and widens the code comparators to a full 32-bit address
 * match instead of v1's even/odd-halfword REPLACE-field scheme.
 *
 * A match on an enabled, enabled-unit comparator delivers a
 * DebugMonitor exception to the guest (via the generic
 * cpu_breakpoint_insert()/armv7m_debug_excp_handler() machinery in
 * target/arm/), gated on DEMCR.MON_EN -- this is architectural
 * "Monitor mode debugging" hardware, distinct from and invisible to
 * QEMU's own gdbstub.
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank
 *  + QOM link property "cpu": the ARMCPU this unit's comparators
 *    apply to (set directly by armv7m.c, mirroring how the NVIC's
 *    "cpu" field is wired)
 */

#ifndef HW_MISC_ARMV7M_FPB_H
#define HW_MISC_ARMV7M_FPB_H

#include "hw/core/sysbus.h"
#include "target/arm/cpu-qom.h"

#define TYPE_ARMV7M_FPB "armv7m-fpb"
OBJECT_DECLARE_SIMPLE_TYPE(ARMv7mFPBState, ARMV7M_FPB)

#define ARMV7M_FPB_NUM_CODE 8

struct ARMv7mFPBState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    ARMCPU *cpu;

    uint32_t fp_ctrl;
    uint32_t fp_comp[ARMV7M_FPB_NUM_CODE];
    struct CPUBreakpoint *bp[ARMV7M_FPB_NUM_CODE];
};

#endif
