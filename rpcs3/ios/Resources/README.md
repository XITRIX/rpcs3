# RPCS3 iOS 26 JIT script

`RPCS3UniversalJIT26.js` implements the debugger side of RPCS3's iOS JIT
protocol. It uses only the host functions exposed by StikDebug-compatible
script runners and is intentionally independent from Amethyst's GPL-3.0
implementation.

RPCS3 emits `brk #0x5250`, places `0x5253` in `x17`, the command in `x16`, and
arguments in `x0`/`x1`. Command 1 is a readiness handshake. Command 2 asks the
debugger to prepare an existing reserved range as RX. Only those exact traps are consumed;
other signals and breakpoints are forwarded, and the debugger remains attached.
