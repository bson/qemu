/*
 * ARMv7-M Flash Patch and Breakpoint (FPB) unit
 *
 * Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the ARMv7-M Architecture Reference Manual
 * (ARM DDI0403E.d), section C1.11 "Flash Patch and Breakpoint unit,
 * FPBv2".
 */

#include "qemu/osdep.h"
#include "hw/misc/armv7m_fpb.h"
#include "hw/core/cpu.h"
#include "hw/core/registerfields.h"
#include "qemu/log.h"

REG32(FP_CTRL, 0x00)
    FIELD(FP_CTRL, ENABLE, 0, 1)
    FIELD(FP_CTRL, KEY, 1, 1)
    FIELD(FP_CTRL, NUM_CODE_LO, 4, 4)
    FIELD(FP_CTRL, NUM_LIT, 8, 4)
    FIELD(FP_CTRL, NUM_CODE_HI, 12, 3)
    FIELD(FP_CTRL, REV, 28, 4)
REG32(FP_REMAP, 0x04)
REG32(FP_COMP0, 0x08)
    FIELD(FP_COMPn, ENABLE, 0, 1)
    FIELD(FP_COMPn, ADDR, 1, 31)

#define FP_COMPn(n) (A_FP_COMP0 + 4 * (n))

static void fpb_update_bp(ARMv7mFPBState *s, unsigned n)
{
    if (s->bp[n]) {
        cpu_breakpoint_remove_by_ref(CPU(s->cpu), s->bp[n]);
        s->bp[n] = NULL;
    }
    if ((s->fp_ctrl & R_FP_CTRL_ENABLE_MASK) &&
        (s->fp_comp[n] & R_FP_COMPn_ENABLE_MASK)) {
        vaddr addr = s->fp_comp[n] & ~1u;
        cpu_breakpoint_insert(CPU(s->cpu), addr, BP_CPU, &s->bp[n]);
    }
}

static void fpb_update_all(ARMv7mFPBState *s)
{
    unsigned n;

    for (n = 0; n < ARMV7M_FPB_NUM_CODE; n++) {
        fpb_update_bp(s, n);
    }
}

static MemTxResult fpb_read(void *opaque, hwaddr addr,
                            uint64_t *data, unsigned size,
                            MemTxAttrs attrs)
{
    ARMv7mFPBState *s = opaque;

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    case A_FP_CTRL:
        *data = (s->fp_ctrl & R_FP_CTRL_ENABLE_MASK) |
                (1 << R_FP_CTRL_REV_SHIFT) |
                (ARMV7M_FPB_NUM_CODE << R_FP_CTRL_NUM_CODE_LO_SHIFT);
        break;
    case A_FP_REMAP:
        *data = 0;
        break;
    case FP_COMPn(0) ... FP_COMPn(ARMV7M_FPB_NUM_CODE - 1):
        *data = s->fp_comp[(addr - A_FP_COMP0) / 4];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "FPB: read of unimplemented offset 0x%x\n",
                      (uint32_t)addr);
        *data = 0;
        break;
    }
    return MEMTX_OK;
}

static MemTxResult fpb_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size,
                             MemTxAttrs attrs)
{
    ARMv7mFPBState *s = opaque;

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    case A_FP_CTRL:
        /* Writes only take effect if KEY is written as 1 */
        if (value & R_FP_CTRL_KEY_MASK) {
            s->fp_ctrl = value & R_FP_CTRL_ENABLE_MASK;
            fpb_update_all(s);
        }
        break;
    case A_FP_REMAP:
        /* No literal comparators in FPBv2: RAZ/WI */
        break;
    case FP_COMPn(0) ... FP_COMPn(ARMV7M_FPB_NUM_CODE - 1):
    {
        unsigned n = (addr - A_FP_COMP0) / 4;

        s->fp_comp[n] = value & (R_FP_COMPn_ENABLE_MASK |
                                 R_FP_COMPn_ADDR_MASK);
        fpb_update_bp(s, n);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "FPB: write of unimplemented offset 0x%x\n",
                      (uint32_t)addr);
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps fpb_ops = {
    .read_with_attrs = fpb_read,
    .write_with_attrs = fpb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void armv7m_fpb_reset_hold(Object *obj, ResetType type)
{
    ARMv7mFPBState *s = ARMV7M_FPB(obj);
    unsigned n;

    s->fp_ctrl = 0;
    for (n = 0; n < ARMV7M_FPB_NUM_CODE; n++) {
        s->fp_comp[n] = 0;
        if (s->bp[n]) {
            cpu_breakpoint_remove_by_ref(CPU(s->cpu), s->bp[n]);
            s->bp[n] = NULL;
        }
    }
}

static void armv7m_fpb_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMv7mFPBState *s = ARMV7M_FPB(obj);

    memory_region_init_io(&s->iomem, obj, &fpb_ops,
                          s, "armv7m-fpb", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void armv7m_fpb_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = armv7m_fpb_reset_hold;
}

static const TypeInfo armv7m_fpb_info = {
    .name = TYPE_ARMV7M_FPB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7mFPBState),
    .instance_init = armv7m_fpb_init,
    .class_init = armv7m_fpb_class_init,
};

static void armv7m_fpb_register_types(void)
{
    type_register_static(&armv7m_fpb_info);
}

type_init(armv7m_fpb_register_types);
