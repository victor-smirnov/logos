# Linux-capable SSD controllers — landscape (2025–2026)

Scope: which SSD controllers run **64-bit Linux** (AArch64 / RV64 + MMU) on
the device itself. Compiled from a fan-out web research pass (22 sources, 107
claims extracted, 25 verified by 3-vote adversarial check, 8 confirmed 3-0).
Companion to the DIY firmware route in [../README.md](../README.md)
(Wertenbroek et al., ACM ToS 21(2), 2025).

## The MMU line

Mainline Linux needs an MMU. That single fact partitions the whole field.

- **Mainstream controllers = MPU-only, no Linux.** Arm's own R82 announcement
  (2020): *"Real-time embedded systems, such as Solid-State Drives (SSDs),
  have relied heavily on the proven, 32-bit Arm Cortex-R5 and Cortex-R8
  processors … have not had a need to run Linux."* Every Cortex-R before R82
  (R4/R5/R7/R8/R52) is PMSA/MPU-only. R52 is Armv8-R but AArch32-only, still
  no MMU. Corroboration: Phison PS5026-E26 (flagship PCIe 5.0 consumer) =
  dual Cortex-R5 + CoXProcessor, TSMC 12nm — MMU-less by construction.
  ⇒ On stock retail SSD controllers, on-device Linux is architecturally
  impossible (only noMMU/uClinux, never shipped in production SSDs).
- **The inflection: Arm Cortex-R82** (2020). First 64-bit Cortex-R
  (Armv8-R AArch64); first Arm core to combine an MPU with an **optional MMU
  at EL1** in one core. Arm: *"Linux, or any other High Level Operating
  System, that today work on Arm Cortex-A series processors will seamlessly
  work on Cortex-R82."* Target class named explicitly: SSDs, HDDs, storage,
  computational storage (up to 1 TB DRAM). Caveat: the MMU is **optional
  per-implementation** — only MMU-fitted silicon is Linux-capable. Which
  shipping controllers licensed R82 *with* the MMU is unestablished
  (R82AE, 2024, is a functional-safety derivative, not a predecessor).

## Confirmed: 64-bit Linux on an SSD controller

Three classes cleared verification.

### 1. Commercial — NGD Systems Newport (historical)

Custom 14 nm, 16-channel ASIC with an embedded **quad Cortex-A53** (AArch64,
~1.0–1.2 GHz). Ran **64-bit Ubuntu + Docker directly on the drive** for
in-situ compute (indexing, search, Hadoop/MPI). Called *"the first commodity
SSD that can be configured to run a server-like operating system"*
(Torabzadehkashi et al., PDP 2019 / J. Big Data 2019; measured on-drive
workloads in ACM TECS 2022, arXiv 2002.07215). FPGA prototype = *Catalina*
(Zynq US+); production *Newport* = the custom ASIC with the same A53 complex.

**NGD Systems is deadpooled (~2023).** Capability is real but historical;
hardware only on the secondary market. Sourcing note: primary facts are
trade-press (TechTarget, Blocks & Files) corroborated by peer-review whose
co-authors are partly NGD-affiliated.

### 2. Research FPGA platform — Daisy / DaisyPlus (alive)

Xilinx **Zynq UltraScale+ ZU17EG** (XCZU17EG-2FFVC1760-E): **quad Cortex-A53
@ 1.5 GHz** (AArch64 + MMU) + dual Cortex-R5 @ 600 MHz. Full research SSD
controller: device-side NVMe controller + NAND flash controller + FTL
("OpenSSD capable"). Stock firmware (`nvme_bsp`) is bare-metal on
`psu_cortexa53_0`; the vendor page names no OS, but the A53 APU is a standard
PetaLinux/Yocto target and third parties run it as a CSD (NCKU OpenSSD course;
InstInfer, arXiv 2409.04992).

**This is the only live open platform with AArch64+MMU cores today.** Sold by
CRZ Technology (~$6150; 1 TB NAND + DDR4 DIMM in the kit). Practical limits
(InstInfer): expensive FPGA, ~4 channels, small capacity. Frequency nuance:
1.5 GHz is the family max (-3E grade); the -2 grade is nominally ~1.333 GHz
per DS925.

### 3. Commercial — Samsung SmartSSD gen2 (platform-capable, unconfirmed on-drive)

Built on **AMD/Xilinx Versal Adaptive SoC** (VM1802 class: dual **Cortex-A72**
ARMv8-A/AArch64+MMU, + Cortex-R5F + PL fabric). Samsung PR (2022-07-21):
*"Powered by Xilinx Versal Adaptive SoCs from AMD … in-built Arm cores."*

**Important scoping:** Samsung's PR never says "Linux." Linux-capability is
inferred from AMD Versal docs (A72 APU officially runs Linux — Embedded
Design Tutorial, Ubuntu on VCK190), **not** from a Samsung claim of running
an OS on the drive. Customer IP → PL fabric; customer software → Arm cores.
gen2 rollout was limited; current purchasability unclear.

## Contrast points (verified, non-qualifying)

- **Cosmos+ OpenSSD** — HYU Tiger4 in Zynq-7000 XC7Z045: **dual Cortex-A9
  @ 1 GHz (ARMv7-A)**. MMU present but 32-bit only ⇒ 32-bit Linux is
  *possible* on the controller (same die as ZC706, official PetaLinux BSP),
  64-bit is impossible in principle. Stock stack is **bare-metal**: NVMe
  manager + FTL + low-level scheduler in one thread, one big loop, one core
  (2nd A9 reserved). Linux appears only as a *host-side* driver. Repo grep
  (3502 files): zero petalinux/freertos artifacts; firmware programs the MMU
  itself via standalone-BSP headers.
- **OpenExpress** (KAIST, USENIX ATC'20) — fully **RTL/hardware** NVMe
  controller, **no CPU, no firmware, no OS** on the device path (doorbell →
  SQ fetch → PRP → DMA → CQ → MSI all in gates, on a BittWare XUSP3R, fabric
  only). Linux on-device impossible by design. Proof that "open platform"
  ≠ "Linux-capable"; it's an NVMe front-end framework, not even a full flash
  controller.

## Cortex-R82 & RV64 in shipping controllers (verified 2026-07-21, focused pass)

Second research pass — 6 angles, 14 sources, 18 claims verified 3-vote, 0
killed. Answers "who ships R82 / RV64 controllers." Key distinction throughout:
**a 64-bit core in a controller ≠ Linux on the drive.** In every *named*
product below, the 64-bit cores are real-time/control cores; on-device Linux
(and even MMU) is **not claimed by the source** for any shipping part.

### Cortex-R82 — one announcement, zero named shipping parts

- **ScaleFlux announced (2024-03-25)** it is *"integrating the Arm Cortex-R82
  processor in its forthcoming line of enterprise SSD controllers."* A stated
  future commitment — **no part number, not shipping.** (design-reuse.com
  #55937.) This is the closest thing to a commercial Linux-capable controller
  on the horizon, because R82 *does* carry the optional MMU + "Linux-capable"
  billing (up to 8 cores, 1 TB, MMU — Blocks & Files 2020-09).
- **No other vendor confirmed on R82.** Counter-evidence that mainstream is
  still 32-bit in 2024: Phison **E26** = 4× Cortex-R5 (+1 Andes N25F RISC-V);
  Silicon Motion **SM2508** = 4× Cortex-R8 + 1 Cortex-M0 (The Register
  2024-03). R5/R8 = MPU-only, no Linux.

### RV64 — more real activity, several named products

- **T-Head / Alibaba Zhenyue 510** — named PCIe Gen5 SSD controller on
  T-Head's **XuanTie C910** RISC-V cores (Tom's Hardware). Source does **not**
  state bit-width/MMU/Linux; the C910 is externally known as an RV64
  application-class core with MMU, but that's not from this source.
- **Yingren YRS820** — Gen5 SSD controller *"100% based on RISC-V CPU cores"*
  (The Register 2024-03). Named product; bit-width/MMU/Linux unspecified.
- **FADU** — controller ASIC with **4× RV64 RISC-V cores** + PPU control
  plane (Blocks & Files 2024-12; survived 1 refute vote). Earlier FADU
  *Annapurna* controller = SiFive **E51** (64-bit) core IP — **but E51 is the
  E-series real-time core, no MMU, no Linux claimed** (SiFive press). FADU
  explicitly frames RISC-V as *efficiency/hardware-automation* vs "traditional
  Arm-firmware FTL," **not** as running Linux on-drive.

### Net

RV64 is quietly displacing 32-bit Cortex-R as the *control* core in new Gen5
controllers (T-Head, Yingren, FADU) — but for firmware-automation, not for
Linux. R82 (which would bring MMU + Linux) has exactly **one** public taker,
ScaleFlux, and only as an unshipped announcement. **Still zero named,
shipping mainstream controller runs Linux on-device.** The full
64-bit + MMU + application-class + Linux-userspace chain remains demonstrated
only by dead NGD Newport and the FPGA research platforms.

### Still uncovered

- **ScaleFlux FX5016 / CSD5000** internals (cores/MMU) — the R82 announcement
  is about a *forthcoming* line, not these shipping parts.
- Whether ScaleFlux's R82 controllers, when they ship, enable the MMU + Linux.
- **Aldec TySOM-3A-ZU19EG / TySOM-3-ZU7EV** — Zynq US+ MPSoC boards (A53,
  Linux-capable) with NVMe reference designs; viable DIY base, unverified as
  a controller.
- No surviving claims for: Marvell Bravera, Microchip Flashtec, Kioxia,
  Solidigm, InnoGrit, Starblaze.

## Bottom line for us

- **Buy-and-run-Linux-on-the-controller today:** only DaisyPlus (FPGA,
  ~$6k, AArch64+MMU). NGD is dead; SmartSSD gen2 is platform-capable but
  not vendor-confirmed for on-drive OS and hard to buy.
- **The pragmatic route is not a real SSD controller at all** — it's the
  [../README.md](../README.md) SBC-in-PCIe-endpoint-mode approach (RK3588,
  $159, AArch64+MMU, stock mainline Linux). Same programming model
  (device-side Linux + NVMe to host), 40× cheaper, no FPGA, no FTL.
- **Watch ScaleFlux specifically** — their 2024 R82 announcement is the one
  credible path to a commercial Linux-capable controller. When those parts
  ship, check whether the MMU is enabled and Linux actually runs on-drive.

## Open questions (for a follow-up pass)

1. When ScaleFlux's R82 controllers ship: MMU enabled? Linux on-device? Part
   numbers? (No *other* vendor has publicly committed to R82.)
2. T-Head Zhenyue 510 / Yingren YRS820 / FADU RV64 parts — do any run an
   MMU + Linux, or are the RV64 cores strictly control-plane firmware?
3. Any *open* RISC-V + FTL + Linux storage-controller platform (vs the
   proprietary T-Head/Yingren/FADU parts)?
4. DaisyPlus real availability/licensing; SmartSSD gen2 fate post-2022.
