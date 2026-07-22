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

## How to actually obtain a dev board / SDK (verified 2026-07-21, 3rd pass)

6 access routes, 15 sources, procurement facts (price/purchasability/
access-model) verified 3-vote (1 killed: the "no online store, quote-only"
claim about OpenSSD — refuted, crz-mart.com is a live storefront). Ordered
cheapest → most gated.

### Open, buy online, no NDA — CRZ Technology / OpenSSD (crz-mart.com)

Sold by CRZ Technology under license from Indilinx + Hanyang Univ ENC Lab.
FPGA/firmware sources fully open: github.com/CRZ-Technology/OpenSSD-OpenChannelSSD.

| Board | FPGA / cores | Price | NAND | Notes |
|---|---|---|---|---|
| **Cosmos+** | XC7Z045 Zynq-7000, dual **A9 (32-bit)** | KRW 3.95M (~$2.9k), **board only** | **separate**, toggle-mode; 64 GB Micron module KRW 670k (~$500); bitstream hard-pinned to NAND vendor+geometry | 1 GB DDR3, dual PCIe Gen2 x8 cabled, JTAG |
| **Daisy** | Zynq US+ ZU17EG, quad **A53+MMU** | on store (not on spec page) | **emulated in DRAM** (2× DDR4 DIMM as NAND, ≤32 GB/slot) — no physical NAND to source | easiest NAND story |
| **DaisyPlus** | Zynq US+ ZU17EG, quad **A53@1.5G+MMU** + dual R5 | **USD $6,150** | **1 TB module (CFM-002) included** | all-in; "Buy Now"→crz-mart |

Prereqs (all CRZ boards): **Vivado** matching the project (bulk = 2019.1;
newest ports = 2025.1) · Digilent USB-JTAG · host with a **cabled-PCIe path**
(adapter/cable not included) · matching NAND module except Daisy. Only Daisy/
DaisyPlus have AArch64+MMU (A53) → the ones that can run 64-bit Linux.

### Cheapest — DIY SBC-in-endpoint (~$200, fully open)

FriendlyElec **NanoPC-T6** (RK3588, ~$159) or **CM3588+NAS kit** (~$174) +
backend NVMe SSD + host PC + `rick-heig/nvme_csd` firmware. **No FPGA, no EDA
license, no JTAG.** Stock arm64 Linux. Only friction = the cabled-PCIe/M.2
endpoint path to the host (paper ships open-hardware adapter PCBs). This is
the S0/S2 target for [../README.md](../README.md).

### FPGA + NVMe IP — open boards, you assemble the stack

- **AMD/Xilinx ZCU106** (EK-U1-ZCU106-G): open purchase via DigiKey, in
  stock, no NDA (the board the Wertenbroek paper used).
- **Opsero FPGA Drive FMC Gen4**: FMC card → 2× M.2 NVMe on an FPGA carrier;
  needs Vivado + a PCIe IP (or the FPGA's hard PCIe block). SSDs not included.
- **Design Gateway NVMe-IP**: NVMe host IP for Xilinx/Versal; **free eval
  bitstreams** to benchmark before licensing. (Other IP vendors: Eideticom,
  IntelliProp, Mobiveil — license, not evaluated here.)

### Vendor SSD-controller kits — gated (real ASIC targets)

- **Microchip Flashtec** — *most accessible vendor.* Eval board orderable by
  part number: **PM35161-KIT** (Kit 1, 4 TB NAND pre-populated) / **PM35162-KIT**
  (Kit 2, customer-consigned NAND), around the Flashtec NVMe 5016 Gen5
  (16-ch). BUT full **design files gated**: "qualified customers" submit a
  request-access form + accept EULA ("Login for Design Files"). Controller
  was at *sampling* (not GA). Public tier = brochure + sell sheet only.
- **Marvell Bravera SC5** (MV-SS1331/1333) — **NDA**, partner extranet, no
  price, Request-For-Information form only. Fully gated.
- **Silicon Motion SM8366 / MonTitan RDK** — **sampling to hand-picked
  partners** (Mar 2025), no public order/price, contact-vendor.
- **Phison IMAGIN+** — not a board: a **bespoke design-in service**,
  quote-based, 3-step (concept → estimate → SoW), case-by-case cost.
- **FADU** — controller vendor selling to SSD makers; no public dev kit.
- **Samsung SmartSSD / ScaleFlux** — no concrete current dev-kit availability
  surfaced; effectively hard to obtain as a developer.

## Emulators / no-hardware route (verified 2026-07-22)

6 angles, 15 sources, 20 claims confirmed 3-vote (0 killed) + one targeted
follow-up on the single open question. Answers: what can be developed in
pure software, and where does it stop.

### QEMU `-device nvme` — host-side controller emulation, mature

Implements **NVMe spec 1.4**, all mandatory features with a few gaps.
Multi-queue (`max_ioqpairs`, default 64), multiple namespaces via `nvme-ns`,
**ZNS (TP 4053) fully compliant with one exception** (since QEMU 6.0). Gaps:
no interrupt coalescing; **zone state doesn't survive a QEMU restart**
(only guest reboot); **no latency/timing-fidelity modeling** — docs are
silent on it, meaning none is claimed. This is what runs *inside* a VM as
"a drive" — the host-driver side, same role real hardware plays toward a
real host. Doesn't touch controller-side/device-side behavior.

### NVMeVirt (SNU, FAST'23) — closer to controller-firmware development

Kernel module operating at the **PCI layer**, presents as a native NVMe
device to the whole system (not layered above the host block layer like
QEMU). Four device personalities via Kbuild: `CONFIG_NVMEVIRT_NVM` (conv.
NVM), `_SSD` (ZNS SSD... — check repo for exact mapping), `_ZNS`, `_KV`
(Key-Value SSD) — i.e. it also models **compute/command-set diversity**,
not just block I/O, closer to what CSD command-set experiments need than
QEMU is. Setup cost: reserve a **contiguous physical memory region** (e.g.
64 GiB) via GRUB to back storage; **incompatible with IOMMU** (disable
Intel VT-d / `intremap=off` or risk kernel panics in `__pci_enable_msix()`).

### SPDK — user-space NVMe-oF target, synthetic backends

`nvmf_tgt` exposes block devices over TCP/RDMA/FC fabrics; **TCP built in,
no special libs**. Backends can be **synthetic `malloc` bdevs** — no
physical NVMe needed to exercise the fabric/target path. `vhost-user`
target itself is host-side, user-space — orthogonal to device-side emulation.

### The actual gap: no software model of the DEVICE side (PCIe endpoint)

This is the decisive finding for our S0 plan. **QEMU has no PCI Endpoint
Controller (EPC) emulation** — nothing that lets Linux's
`pci-endpoint-framework` function drivers (the `pci_epf_*` code that
`rick-heig/nvme_csd` and any Memoria-firmware would be) run against a
simulated root complex instead of a real board.

- A concrete proposal exists: **Shunsuke Mie (Igel Co.)**, "[RFC] Proposal
  of QEMU PCI Endpoint test environment," posted **2023-08-18** to
  qemu-devel — QEMU EPC device + Linux `pcie-qemu-ep.c` driver + EPF-bridge
  device, TLP-equivalent messaging over Unix sockets between QEMU processes.
  Manivannan Sadhasivam (Linux PCI-EP maintainer) replied supportively but
  questioned the design (2023-10). The same *unmerged* proposal resurfaced
  as a talk — **KVM Forum 2024**, "Virtual device for testing the Linux
  PCIe endpoint framework" — a year later, still design-stage.
- **Not merged.** No `hw/misc/qemu-epc.c`, no `epf-bridge.c` in mainline
  QEMU; no `pcie-qemu-ep.c` in upstream Linux. No newer (2025–2026) patch
  series found.
- Linux's own `tools/testing/selftests/pci_endpoint` tests the **host-side**
  `pci_endpoint_test` driver against a **real** EP-capable board running
  `pci-epf-test` — it validates the framework, it doesn't emulate the EP.

**Bottom line: as of 2026-07, real PCIe-endpoint-capable hardware (SBC or
FPGA) is required to develop/test any `pci-endpoint-framework` function
driver — including an NVMe EPF for Memoria.** No amount of QEMU/NVMeVirt/
SPDK substitutes for it; those tools only emulate the *other* side (a drive,
as seen by a host).

### What this means for S0/S1/S2

Revises the plan in [../README.md](../README.md): **S0's "mock NVMe
queue-pair transport" cannot be a QEMU-side device-emulation trick** — it
has to be our own userspace/UDS mock of the *command surface* (what a
Memoria-graph-executor sees), independent of any real PCIe transport, which
is what was already planned. It's *not* a step that later gets replaced by
"real QEMU NVMe emulation" — there's no such bridge; S0's mock and S2's real
SBC endpoint are the only two rungs, nothing fills the middle in software.
Confirms the SBC route (~$200) is not just the cheap option — for the
device-side of this problem, **it's the only one, software or hardware,
short of DaisyPlus or a vendor NDA.**

### Recommendation for the csd subproject

- **Protocol/firmware spike (S0/S2):** SBC route (~$200). Matches our
  device-side-Linux + NVMe-endpoint model exactly; zero licensing.
- **If we want the *real* SSD stack (FTL, NAND, device-side NVMe) on
  AArch64+MMU:** DaisyPlus ($6,150, all-in). Only open board with A53+MMU.
- **Production ASIC path later:** Microchip Flashtec is the one vendor with a
  self-service entry point (eval board by part number; SDK behind
  qualified-customer + EULA).

## ZNS SSD market availability (verified 2026-07-22)

5 angles, 14 sources, 17 claims — 14 confirmed, 3 killed. ZNS matters here
because it's the one *shipping, standardized* NVMe path that hands the host
explicit zone/erase-boundary control instead of an opaque FTL — the closest
market analog to what our own functional-NAND-model direction is reaching
for, and worth knowing whether it's usable off the shelf.

### Real ZNS product: WD Ultrastar DC ZN540 — now actually buyable

Announced Nov 2020 as *"the world's first ZNS SSD,"* originally
hyperscaler-sampling only. **Verification killed the "still gated" claim**:
current listings show real SKUs (0TS2094/2099/2108/2109 for
1024/2048/4096/8192 GB, ISE/SE/TCG/TCG-FIPS variants) at ordinary enterprise
IT resellers (HardDiskDirect, Advanced HPC) — "Call for Stock" / "Request a
Quote," i.e. **enterprise-distributor channel, not hyperscaler-exclusive,
but also not add-to-cart retail.** Implements the NVMe TP 4053 ZNS command
set as a production part, not a prototype.

### Samsung PM1731a — real ZNS, but TLC not QLC (correction)

2H2021 mass production, 2 TB / 4 TB. **Verification corrected the NAND
type**: Samsung's own material states **TLC (6th-gen V-NAND)**, not QLC —
QLC was announced only as a *future* next-gen ZNS drive, not this part.
General developer purchasability wasn't established in this pass.

### Kioxia — a dead end in this pass, not a confirmed ZNS product

Both models searched (**CD8P-R**, **CM7-R**) turned out to be **standard,
non-zoned enterprise NVMe SSDs** — their own product pages make no ZNS
mention. (CD8P-R *is* however normally distributor-available — CDW, SHI,
Tech America ~$364/1.92TB — so the "OEM-only" framing on it was refuted too,
it's just not a ZNS drive.) Kioxia has contributed to the ZNS spec and has
shipped ZNS SKUs elsewhere in its catalog, but this pass didn't locate the
actual ZNS part number — open question below.

### Market trajectory: ZNS adoption is stalling, FDP is the industry's answer

Trade press (storagenewsletter.com, 2025-02): ZNS **"faced adoption
challenges** because it required the host to modify the data pattern and
software stack" to realize its write-amplification win — unlike other
placement schemes. **FDP (Flexible Data Placement, newer NVMe TP)** is
explicitly positioned as capturing *most* of the same WAF benefit **"with
minimal changes"** to the host stack — i.e. the industry's compromise that
trades host control for adoption ease.

This cuts directly against our own direction: FDP is *less* host control by
design (hints, not host-owned zone boundaries) — exactly the opposite of
what a Memoria-tuned functional NAND/FTL layer wants. **ZNS remains the more
relevant standardized primitive for us even as the market broadly drifts
toward FDP** — worth tracking as a divergence, not following the trend.

### Open questions

1. Kioxia's actual shipping ZNS SKU (not CD8P-R/CM7-R) — name and
   purchasability unconfirmed.
2. SK Hynix / Solidigm ZNS parts — no claims surfaced at all this pass.
3. Real end-to-end price for a WD ZN540 unit (only "call for stock"/"quote"
   surfaced, no number).
4. Whether any ZN540/PM1731a unit is obtainable in single-unit quantity for
   a small dev team, vs. minimum-order/qualified-account gating.

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
