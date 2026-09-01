#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2026 Jan Brittenson <bson@rockgarden.net>
#
# Regression test: confirm that QEMU's own external gdbstub -- hardware
# breakpoints and watchpoints delivered via the target-independent
# BP_GDB mechanism (accel/tcg/*) -- still works correctly against an
# M-profile (Cortex-M) board after the stm32h743-eval architectural
# debug hardware (FPB/DWT/DebugMonitor, see hw/misc/armv7m_fpb.c and
# hw/misc/armv7m_dwt.c) repointed target/arm's per-CPU
# debug_check_breakpoint/debug_check_watchpoint/debug_excp_handler
# hooks for M-profile CPUs. That repointing is the change most likely
# to regress external GDB debugging, since gdbstub's own BP_GDB path
# and the new BP_CPU (guest-programmed FPB/DWT) path both flow through
# the same target/arm/tcg/m_helper.c handler.

import os

from qemu_test import QemuSystemTest, skipIfMissingImports, skipIfMissingEnv


# A tiny bare-metal Cortex-M7 payload -- just a counting loop writing
# to a fixed RAM word -- used purely as something for GDB to set a
# breakpoint/watchpoint against. Built with:
#   clang -target arm-none-eabi -mcpu=cortex-m7 -mthumb -nostdlib \
#         -static -Wl,-n <loop.S+link.ld> -o loop.elf
#   llvm-strip --strip-all loop.elf
KERNEL_LOOP = bytes.fromhex(
    "7f454c4601010100000000000000000002002800010000000000000034000000"
    "680100000002000534002000040028000600050001000000b400000000000008"
    "000000084000000040000000040000000100000001000000f400000040000008"
    "4000000814000000140000000500000004000000010000000801000000000020"
    "000000200400000004000000060000000400000051e574640000000000000000"
    "0000000000000000000000000600000000000000000002204300000841000008"
    "4100000841000008410000084100000800000000000000000000000000000000"
    "4100000841000008000000004100000841000008fee70020024908600868401c"
    "0860fbe700000020000000004128000000616561626900011e00000005636f72"
    "7465782d6d3700060d074d080009020a0822012401002e74657874002e41524d"
    "2e61747472696275746573002e6973725f766563746f72002e73687374727461"
    "62002e6461746100000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000017000000010000000200000000000008"
    "b400000040000000000000000000000001000000000000000100000001000000"
    "0600000040000008f40000001400000000000000000000000400000000000000"
    "2d00000001000000030000000000002008010000040000000000000000000000"
    "0400000000000000070000000300007000000000000000000c01000029000000"
    "0000000000000000010000000000000023000000030000000000000000000000"
    "350100003300000000000000000000000100000000000000"
)

# Address of "adds r0, r0, #1" inside the loop body
BREAK_ADDR = 0x0800004a
# Address of the RAM word the loop increments every iteration
COUNTER_ADDR = 0x20000000


class Stm32h743EvalGdbstub(QemuSystemTest):

    @skipIfMissingImports("pygdbmi")
    @skipIfMissingEnv("QEMU_TEST_GDB")
    def test_hbreak_and_watch(self):
        from qemu_test import GDB
        from qemu_test.ports import Ports

        self.require_accelerator("tcg")
        self.set_machine('stm32h743-eval')

        kernel_path = os.path.join(self.workdir, 'loop.elf')
        with open(kernel_path, 'wb') as f:
            f.write(KERNEL_LOOP)

        with Ports() as ports:
            port = ports.find_free_port()

        self.vm.add_args('-kernel', kernel_path, '-S',
                         '-gdb', 'tcp::%d' % port)
        self.vm.launch()

        gdb_cmd = os.getenv('QEMU_TEST_GDB')
        gdb = GDB(gdb_cmd)
        try:
            gdb.cli("set architecture arm")

            c = gdb.cli("target remote localhost:%d" % port).get_console()
            if "Remote debugging using localhost:%d" % port not in c:
                self.fail("Could not connect to gdbstub")

            # Hardware breakpoint (Z1): should stop exactly at BREAK_ADDR
            gdb.cli("hbreak *%s" % hex(BREAK_ADDR))
            gdb.cli("continue")
            pc = gdb.cli("print $pc").get_addr()
            self.assertEqual(pc, BREAK_ADDR,
                             "hardware breakpoint did not stop at the "
                             "expected PC (got %s)" % (hex(pc) if pc
                                                       else pc))
            gdb.cli("delete 1")

            # Hardware watchpoint (Z2): the loop writes COUNTER_ADDR on
            # every iteration, so this should fire almost immediately.
            gdb.cli("watch *(int*)%s" % hex(COUNTER_ADDR))
            c = gdb.cli("continue").get_console()
            self.assertTrue("Hardware watchpoint" in c or "Old value" in c,
                            "watchpoint did not appear to trigger: %s" % c)
        finally:
            gdb.exit()
            self.vm.shutdown()


if __name__ == '__main__':
    QemuSystemTest.main()
