/*
 * ARMv7-M Data Watchpoint and Trace (DWT) unit
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * This is a model of the DWT register block of an M-profile CPU
 * (the registers starting at 0xE0001000). Only basic address-only
 * data watchpoints are implemented (DWT_FUNCTIONn = read/write/
 * read-write, DATAVMATCH=0); data-value-compare, linked-comparator,
 * and cycle-count-comparator match functions, and all
 * trace/profiling counters (CYCCNT, CPICNT, etc.), are out of scope
 * and RAZ/WI.
 *
 * A match on an enabled comparator delivers a DebugMonitor exception
 * to the guest (via the generic cpu_watchpoint_insert()/
 * armv7m_debug_excp_handler() machinery in target/arm/), gated on
 * DEMCR.MON_EN, same as the FPB (see hw/misc/armv7m_fpb.c).
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank
 */

#ifndef HW_MISC_ARMV7M_DWT_H
#define HW_MISC_ARMV7M_DWT_H

#include "hw/core/sysbus.h"
#include "target/arm/cpu-qom.h"

#define TYPE_ARMV7M_DWT "armv7m-dwt"
OBJECT_DECLARE_SIMPLE_TYPE(ARMv7mDWTState, ARMV7M_DWT)

#define ARMV7M_DWT_NUM_COMP 4

struct ARMv7mDWTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    ARMCPU *cpu;

    uint32_t comp[ARMV7M_DWT_NUM_COMP];
    uint32_t mask[ARMV7M_DWT_NUM_COMP];
    uint32_t function[ARMV7M_DWT_NUM_COMP];
    struct CPUWatchpoint *wp[ARMV7M_DWT_NUM_COMP];
};

#endif
