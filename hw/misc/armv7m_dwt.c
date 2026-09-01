/*
 * ARMv7-M Data Watchpoint and Trace (DWT) unit
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the ARMv7-M Architecture Reference Manual
 * (ARM DDI0403E.d), section C1.8 "Data Watchpoint and Trace unit".
 */

#include "qemu/osdep.h"
#include "hw/misc/armv7m_dwt.h"
#include "hw/core/cpu.h"
#include "hw/core/registerfields.h"
#include "exec/watchpoint.h"
#include "qemu/log.h"

REG32(DWT_CTRL, 0x00)
    FIELD(DWT_CTRL, NUMCOMP, 28, 4)
REG32(DWT_COMP0, 0x20)
REG32(DWT_MASK0, 0x24)
    FIELD(DWT_MASKn, MASK, 0, 4)
REG32(DWT_FUNCTION0, 0x28)
    FIELD(DWT_FUNCTIONn, FUNCTION, 0, 4)
    FIELD(DWT_FUNCTIONn, MATCHED, 24, 1)

#define DWT_STRIDE 0x10

/* FUNCTION encodings we implement: basic address-only watchpoints */
#define DWT_FUNC_DISABLED 0x0
#define DWT_FUNC_WATCH_READ 0x4
#define DWT_FUNC_WATCH_WRITE 0x5
#define DWT_FUNC_WATCH_RW 0x6

static void dwt_update_wp(ARMv7mDWTState *s, unsigned n)
{
    int flags;

    if (s->wp[n]) {
        cpu_watchpoint_remove_by_ref(CPU(s->cpu), s->wp[n]);
        s->wp[n] = NULL;
    }

    switch (FIELD_EX32(s->function[n], DWT_FUNCTIONn, FUNCTION)) {
    case DWT_FUNC_WATCH_READ:
        flags = BP_CPU | BP_MEM_READ;
        break;
    case DWT_FUNC_WATCH_WRITE:
        flags = BP_CPU | BP_MEM_WRITE;
        break;
    case DWT_FUNC_WATCH_RW:
        flags = BP_CPU | BP_MEM_ACCESS;
        break;
    default:
        return;
    }

    {
        vaddr len = 1ULL << FIELD_EX32(s->mask[n], DWT_MASKn, MASK);
        vaddr addr = s->comp[n] & ~(len - 1);

        cpu_watchpoint_insert(CPU(s->cpu), addr, len, flags, &s->wp[n]);
    }
}

static MemTxResult dwt_read(void *opaque, hwaddr addr,
                            uint64_t *data, unsigned size,
                            MemTxAttrs attrs)
{
    ARMv7mDWTState *s = opaque;

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    if (addr == A_DWT_CTRL) {
        *data = ARMV7M_DWT_NUM_COMP << R_DWT_CTRL_NUMCOMP_SHIFT;
        return MEMTX_OK;
    }

    if (addr >= A_DWT_COMP0 &&
        addr < A_DWT_COMP0 + ARMV7M_DWT_NUM_COMP * DWT_STRIDE) {
        unsigned n = (addr - A_DWT_COMP0) / DWT_STRIDE;

        switch ((addr - A_DWT_COMP0) % DWT_STRIDE) {
        case 0x0: /* COMPn */
            *data = s->comp[n];
            return MEMTX_OK;
        case 0x4: /* MASKn */
            *data = s->mask[n];
            return MEMTX_OK;
        case 0x8: /* FUNCTIONn */
            *data = s->function[n];
            return MEMTX_OK;
        default:
            break;
        }
    }

    qemu_log_mask(LOG_UNIMP, "DWT: read of unimplemented offset 0x%x\n",
                  (uint32_t)addr);
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult dwt_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size,
                             MemTxAttrs attrs)
{
    ARMv7mDWTState *s = opaque;

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    if (addr == A_DWT_CTRL) {
        /* Trace/profiling counters not modeled: RAZ/WI */
        return MEMTX_OK;
    }

    if (addr >= A_DWT_COMP0 &&
        addr < A_DWT_COMP0 + ARMV7M_DWT_NUM_COMP * DWT_STRIDE) {
        unsigned n = (addr - A_DWT_COMP0) / DWT_STRIDE;

        switch ((addr - A_DWT_COMP0) % DWT_STRIDE) {
        case 0x0: /* COMPn */
            s->comp[n] = value;
            dwt_update_wp(s, n);
            return MEMTX_OK;
        case 0x4: /* MASKn */
            s->mask[n] = value & R_DWT_MASKn_MASK_MASK;
            dwt_update_wp(s, n);
            return MEMTX_OK;
        case 0x8: /* FUNCTIONn */
        {
            unsigned func = FIELD_EX32(value, DWT_FUNCTIONn, FUNCTION);

            if (func != DWT_FUNC_DISABLED && func != DWT_FUNC_WATCH_READ &&
                func != DWT_FUNC_WATCH_WRITE && func != DWT_FUNC_WATCH_RW) {
                qemu_log_mask(LOG_UNIMP,
                              "DWT: unimplemented FUNCTION encoding %u on "
                              "comparator %u\n", func, n);
                func = DWT_FUNC_DISABLED;
            }
            s->function[n] = FIELD_DP32(0, DWT_FUNCTIONn, FUNCTION, func);
            dwt_update_wp(s, n);
            return MEMTX_OK;
        }
        default:
            break;
        }
    }

    qemu_log_mask(LOG_UNIMP, "DWT: write of unimplemented offset 0x%x\n",
                  (uint32_t)addr);
    return MEMTX_OK;
}

static const MemoryRegionOps dwt_ops = {
    .read_with_attrs = dwt_read,
    .write_with_attrs = dwt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void armv7m_dwt_reset_hold(Object *obj, ResetType type)
{
    ARMv7mDWTState *s = ARMV7M_DWT(obj);
    unsigned n;

    for (n = 0; n < ARMV7M_DWT_NUM_COMP; n++) {
        s->comp[n] = 0;
        s->mask[n] = 0;
        s->function[n] = 0;
        if (s->wp[n]) {
            cpu_watchpoint_remove_by_ref(CPU(s->cpu), s->wp[n]);
            s->wp[n] = NULL;
        }
    }
}

static void armv7m_dwt_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMv7mDWTState *s = ARMV7M_DWT(obj);

    memory_region_init_io(&s->iomem, obj, &dwt_ops,
                          s, "armv7m-dwt", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void armv7m_dwt_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = armv7m_dwt_reset_hold;
}

static const TypeInfo armv7m_dwt_info = {
    .name = TYPE_ARMV7M_DWT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7mDWTState),
    .instance_init = armv7m_dwt_init,
    .class_init = armv7m_dwt_class_init,
};

static void armv7m_dwt_register_types(void)
{
    type_register_static(&armv7m_dwt_info);
}

type_init(armv7m_dwt_register_types);
