# CSD — Memoria on a software-defined NVMe drive

Seedling. Direction: Memoria as the **resident DBMS inside an NVMe device**.
The device is an off-the-shelf SBC whose PCIe controller runs in **endpoint
mode** under Linux; the host enumerates it as a standard NVMe drive (stock
host driver, zero host changes) and additionally speaks a container-level
command surface. No custom FTL, no FPGA: flash management stays in a backend
SSD behind the board.

## Foundation

Wertenbroek, Thoma, Dassatti — *A Portable Linux-based Firmware for NVMe
Computational Storage Devices*, ACM Trans. Storage 21(2), 2025.
[DOI 10.1145/3697352](https://doi.org/10.1145/3697352) · firmware:
[rick-heig/nvme_csd](https://github.com/rick-heig/nvme_csd) (open source,
incl. adapter PCBs as open hardware).

- NVMe controller = Linux **PCI endpoint function driver** (SQ thread →
  transfer threads/DMA → nvmet via blk-mq → CQ thread). Storage backend =
  any Linux block device (NVMe/SATA/RAM/RAID) — FTL abstracted away.
- Command execution paths: inline in decode, deferred kthread, **user-space
  queues** (char devices + mmap'd controller memory buffer). User-space hop
  costs ~33 µs, <10% of command latency — the intended Memoria path.
- TCP/IP tunneled over NVMe vendor commands → SSH into the drive, no extra
  cabling. Device-side FS awareness (mount host FS read-only).
- Their measurements: ≤57% of backend-SSD bandwidth @ 4 MB blocks,
  ~30 kIOPS @ 4 kB random, <400 µs @ 16 kB. Research-platform tier
  (Cosmos+ OpenSSD: 75 kIOPS) — adequate for a spike, not a product.
- Verify when landing S2: mainline status of the NVMe endpoint *function*
  driver (Le Moal / Wertenbroek lineage; expected in-tree post-6.8).

## Hardware candidates

| Board | SoC (all AArch64) | PCIe EP | Price | Notes |
|---|---|---|---|---|
| ROCKPro64 | RK3399: 2×A72+4×A53 | 2.0 x4 | $79 | EP driver fixed by the authors, mainline since 6.5; slow (TLP-per-access) |
| FriendlyElec NanoPC-T6 | RK3588: 4×A76+4×A55 | 3.0 x4 | $159 | perf ≈ ZCU106 FPGA platform; GPU/NPU on-SoC |
| FriendlyElec CM3588 + NAS kit | RK3588 | 3.0 dual x2 | ~$174 | PHY split: x2 EP to host + x2 RC to backend NVMe — true man-in-the-middle; needs the paper's adapter PCBs |
| Nvidia Jetson Orin/Xavier | — | yes | $$ | EP mode documented by Nvidia; untested here |

## Why this fits Memoria

- **Container-plane device**: host speaks containers/snapshots over NVMe
  vendor commands, not blocks. Commits, CoW clones, and the
  prepared==consumed gate execute inside the device, next to the medium.
- **Agent-DBMS angle**: the drive *is* the database server; NVMe is the
  transport; SSH-over-NVMe covers ops. Scales per-drive, not per-host.

## Port surface (findings 2026-07-21)

- `logosc` is host-target-only: `InitializeNativeTarget()` +
  `sys::getDefaultTargetTriple()`; `k_target_arch` in `src/compiler/sema.cpp`
  already carries an `__aarch64__` branch. Least-resistance path = **native
  toolchain build on an arm64 host** (stock LLVM 20 exists for arm64), not a
  cross `--target` flag.
- Fibers: `src/reactor/fiber_switch.S` / `morestack.S` are x86-64 SysV only
  → AAPCS64 twins needed (x19–x28, d8–d15, sp, lr).
- Metacall JIT leans on the x86-64 GOTTPOFF TLS relocation → aarch64 TLS
  needs its own handling, or metacalls stay host-side until S3.
- `rt/*.c`, allocator: expected portable C — gate with dogfood_rt on arm64.

## Stages

- **S0 — protocol spike, no hardware.** Memoria server process + mock NVMe
  queue-pair transport (UDS/shm) + host client lib whose backend later swaps
  to libnvme passthrough unchanged. Deliverable: container
  open/read/write/snapshot-commit round-trip through the mock queue.
- **S1 — aarch64 port.** Fiber asm, TLS/JIT handling, native toolchain build
  on any arm64 host (QEMU/cloud box gates nothing on hardware).
- **S2 — board bring-up.** nvme_csd firmware on the board, host enumerates
  the drive, fio sanity numbers.
- **S3 — Memoria on-device.** Server consumes vendor commands via the
  user-space queue path; bench vs host-side Memoria; agent-productivity
  benchmark hook.

## Open (PAIR design)

- Command-surface shape: raw vendor opcodes vs NVMe KV command set vs a
  SNIA CS API mapping.
- Dual view coherence: block namespace + container commands on one device,
  or pure container namespace.
- Where the S0 transport abstraction lives: `memoria-store` or its own
  module.

Status: seedling, no code yet — build scaffolding (`lforge.writ`) arrives
with S0.
