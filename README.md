# Orange Pi 6 Plus GPU — Mali-G720 Immortalis MC10

Enabling the Mali-G720 Immortalis MC10 (Panthor) GPU on the Orange Pi 6 Plus (CIX CD8180 / Sky1 SoC) under mainline Linux. This document covers our full reverse engineering process, the root cause of GPU power failure on ACPI boot, and how to get a working GPU using device tree boot with the [Sky1-Linux](https://github.com/Sky1-Linux/) kernel.

## Quick Start

→ **[Pre-built Sky1-Linux Kernel Downloads](./downloads/README.md)** — kernel, modules, DTB, and firmware ready to install.

Then create a GRUB entry from [`gpu/grub/06_sky1`](gpu/grub/06_sky1) and reboot.

## Status: ✅ GPU Working

```
panthor 15000000.gpu: [drm] clock rate = 900000000
panthor 15000000.gpu: [drm] GPU DVFS: 6 OPPs from SCMI perf domain
panthor 15000000.gpu: [drm] Mali-G720-Immortalis id 0xc870
panthor 15000000.gpu: [drm] shader_present=0x550555 l2_present=0x1 tiler_present=0x1
```

| Detail | Value |
|--------|-------|
| Kernel | 6.19.0-rc8-sky1-rc.r6 ([Sky1-Linux](https://github.com/Sky1-Linux/) RC track) |
| Boot mode | Device tree (`acpi=off`) |
| DRM render | `/dev/dri/renderD128` |
| Display | HDMI via Linlon-D60 + Trilin-DPSUB + PS185 DP-to-HDMI bridge |
| OpenGL ES | 3.1 (Panfrost, Mesa 25.3.3) |
| Vulkan | 1.4.328 (panvk, Mesa 25.3.3) |
| GPU clock | 900 MHz, 6 DVFS operating points |
| Shader cores | 10 (MC10 configuration) |

---

## Table of Contents

1. [The Problem](#the-problem)
2. [Mapping the Power Architecture](#step-1-mapping-the-power-architecture)
3. [Exploring Every ACPI Path](#step-2-exploring-every-acpi-path)
4. [The Breakthrough: TF-A's Dual Code Path](#step-3-the-breakthrough--tfas-dual-code-path)
5. [Device Tree Boot on Armbian](#step-4-device-tree-boot-on-armbian)
6. [Sky1-Linux: The Complete Solution](#step-5-sky1-linux-the-complete-solution)
7. [How to Install](#how-to-install)
8. [Boot Configuration](#boot-configuration)
9. [Kernel Config Options](#required-kernel-config-options)
10. [Verification](#verification)
11. [Benchmarks](#benchmarks)
12. [Known Issues](#known-issues)
13. [Hardware Reference](#hardware-reference)
14. [What's in This Repo](#whats-in-this-repo)
15. [License & Attribution](#license--attribution)

---

## The Problem

The Orange Pi 6 Plus ships with CIX's vendor kernel (6.6-cix) and a proprietary out-of-tree `mali_kbase` driver. On a mainline Armbian kernel (6.18.x), the Mali-G720 GPU at MMIO address `0x15010000` is completely unreachable. Any attempt to read GPU registers — even the basic `GPU_ID` — results in a **Bus Error** that can cascade into a stuck process and system hang.

The mainline Panthor driver (in-tree since kernel 6.10) supports Mali-G720, but it only has an `of_match_table` (device tree) — no ACPI match. On Armbian's default ACPI boot, the driver never probes.

We set out to understand *why* the GPU couldn't power on and whether we could fix it.

---

## Step 1: Mapping the Power Architecture

We reverse-engineered the GPU power control path. The CIX CD8180 uses ARM's RCSU (Reset and Clock Supervision Unit) with DMRP (Domain Resource Power) registers to manage power partitions:

| Address | Register | Access from EL1 | Purpose |
|---------|----------|:---:|---------|
| 0x15000010 | RCSU PASS | ✅ Readable | Power status readback |
| 0x15000014 | RCSU ENBL | ❌ Firewalled | **Power enable — the critical register** |
| 0x15000018 | RCSU BUSY | ✅ Readable | Power transition status |
| 0x15000218 | RCSU MSK0 | ✅ Readable | Clock/partition mask |
| 0x15010000 | GPU_ID | ❌ Bus Error | Mali GPU ID — unreachable when unpowered |
| 0x16000400 | SRC DOMAIN_0 | ✅ Read/Write | Reset control — bit 6 = GPU |
| 0x16000504 | MVCK | ❌ Firewalled | Validation gate, always reads 0 |

The critical finding: `RCSU ENBL` at `+0x14` is the power-on switch, but it's **firewalled from EL1**. The OS kernel cannot write it directly. Only EL3 (TF-A secure world) can touch it.

---

## Step 2: Exploring Every ACPI Path

Since the board boots with ACPI by default, we systematically tried every possible ACPI mechanism to power the GPU. We built diagnostic kernel modules for each approach (see [`modules/`](modules/)):

### ACPI `GPUP._ON` Method
The ACPI tables define a power resource for the GPU that calls into DMRP. But the DMRP path is gated by the MVCK register at `0x16000504`, which always reads zero and is firewalled. **Dead end.**

### ACPI DSM (Device Specific Method)
CIX's kernel source references a `cix_acpi_pd.c` GUID for ACPI power domain management. We checked: the DSM does not exist on the Generic BIOS v1.3 firmware. **Dead end.**

### SCMI Power Protocol via TF-A
The ARM SCMI interface exposes 22 power domains through SMC call `0xc2000001`. Domain 21 (`gpu_pd`) accepts SET requests and **returns success (0)**. But the GPU stays dead. The SET is bookkeeping only — it never writes hardware registers. **Dead end.**

### SCMI Power Protocol via SCP
We tried reaching the System Control Processor's SCMI endpoint directly via the mailbox transport. The SCP does not expose the POWER protocol at all — request DENIED (-8). **Dead end.**

### SCMI Clocks
GPU PLLs are running (1 GHz core clock, 900 MHz top clock visible in SCMI clock enumeration). The hardware is clocked — it's just not powered. **Confirmed clocks are not the issue.**

### Reset Controller
Sky1's `reset-sky1` driver probes for ACPI ID `CIXHA020`, but the vendor `RSTL` ACPI binding isn't in mainline. `reset_control_get()` returns `-ENOENT`. **Dead end.**

### Direct Reset Deassert
We wrote a diagnostic module to directly write the SRC (System Reset Controller) domain register. Bit 6 at offset `0x400` controls GPU reset. We successfully deasserted GPU reset — **confirmed by readback** — but without power, it accomplished nothing. **Proved reset works, but power is the blocker.**

### Diagnostic Modules

| Module | What It Does |
|--------|-------------|
| `gpu_power_diag.c` | Enumerates all 22 TF-A power domains, tests SET, safe GPU register read |
| `gpu_reset_deassert.c` | Direct SRC register write to deassert GPU reset (bit 6) — confirmed working |
| `gpu_acpi_poweron.c` | Triggers ACPI `PPRS._ON` via `acpi_device_set_power()` |
| `gpu_dsm_poweron.c` | ACPI DSM attempt — confirmed DSM doesn't exist on this firmware |
| `gpu_clk_enable.c` | Raw SCMI mailbox clock enable attempt |

---

## Step 3: The Breakthrough — TF-A's Dual Code Path

After exhausting every ACPI path, we examined the behavior difference between ACPI and device tree boot at the firmware level. The same SMC call (`0xc2000001`) goes to the same TF-A handler, but **the handler has two code paths**:

| Boot Mode | SCMI Power SET Behavior | Hardware Effect |
|-----------|------------------------|-----------------|
| **ACPI** | Software bookkeeping only — returns success | **GPU stays unpowered** |
| **Device Tree** | Writes RCSU DMRP ENBL register at EL3 | **GPU powers on** |

This is the root cause. On ACPI boot, TF-A's SCMI power handler is effectively a **stub**. On DT boot, TF-A performs the actual EL3 register write to `RCSU ENBL` at `+0x14`, powering on the GPU partition.

The fix was not a kernel hack — it was **switching boot modes**.

### Full ACPI Failure Summary

| Layer | What Happens | Why It Fails |
|-------|-------------|--------------|
| ACPI `GPUP._ON` | Calls DMRP to power GPU | DMRP gated by MVCK (0x16000504), always 0, firewalled |
| ACPI DSM | Would use `cix_acpi_pd.c` GUID | DSM does not exist on Generic BIOS v1.3 |
| TF-A SCMI Power | Domain 21 (`gpu_pd`) SET succeeds | Software-only bookkeeping — never writes DMRP registers |
| SCP SCMI Power | Mailbox transport | POWER protocol not exposed — request DENIED (-8) |
| SCMI Clocks | GPU PLLs running (1 GHz, 900 MHz) | Consumers not registered in kernel on ACPI boot |
| Reset control | `reset-sky1` for CIXHA020 | Vendor `RSTL` binding not in mainline — returns -ENOENT |
| Panthor driver | `of_match_table` only | No ACPI match — driver never probes |
| RCSU DMRP | ENBL at +0x14 | Firewalled from EL1; only EL3 can write |

---

## Step 4: Device Tree Boot on Armbian

With the root cause understood, the fix was straightforward: boot with `acpi=off` and provide a proper device tree blob. But the stock Armbian kernel was missing critical platform drivers needed for DT boot:

- `RESET_SKY1` — Sky1 reset controller
- `PINCTRL_SKY1` — Sky1 pin controller
- `PCI_SKY1` — Sky1 PCIe host (needed for ethernet, NVMe)
- `PHY_CIX_PCIE` — CIX PCIe PHY

Without these, a DT boot attempt left the board dead — no network, no storage. The mainline kernel config has `CONFIG_ARCH_CIX=n` — everything CIX is disabled.

We could rebuild the kernel with these options enabled, but we'd still be missing the display drivers (Linlon-D60, Trilin-DPSUB), GPU DVFS integration, and the Orange Pi 6 Plus-specific device tree with corrected GPIO assignments and the DP-to-HDMI bridge chain.

---

## Step 5: Sky1-Linux — The Complete Solution

[Sky1-Linux](https://github.com/Sky1-Linux/) maintains a [40-patch kernel series](https://github.com/Sky1-Linux/linux-sky1) (GPL-2.0) on top of mainline Linux that enables the full CIX CD8180 platform. Their work provides everything needed beyond what we established through reverse engineering:

**What our research established:**
- The GPU power architecture (RCSU DMRP registers, EL1 firewalls)
- The TF-A dual code path (ACPI = stub, DT = real power-on)
- That `acpi=off` with device tree boot is mandatory
- Which platform drivers are required (`RESET_SKY1`, `PINCTRL_SKY1`, `PCI_SKY1`, `PHY_CIX_PCIE`)
- That the SCMI power domain, clocks, and reset are all functional — the only missing piece was TF-A's ACPI-mode stub

**What Sky1-Linux provides:**
- **Panthor DVFS** — SCMI performance domain integration for dynamic GPU frequency/voltage scaling (6 OPPs up to 900 MHz)
- **Display stack** — Linlon-D60 display controller, Trilin-DPSUB DisplayPort transmitter
- **DP-to-HDMI bridge** — PS185 converter support (critical for HDMI output on the Orange Pi 6 Plus)
- **Platform drivers** — `RESET_SKY1`, `PINCTRL_SKY1`, `PCI_SKY1`, `PHY_CIX_PCIE`
- **Orange Pi 6 Plus device tree** — Board-specific GPIO assignments, USB-C DP alt-mode, PCIe power
- **Peripherals** — RTL8126 5GbE, RTL8125 2.5GbE, HDA + DSP audio, VPU video codecs
- **Firmware** — Mali CSF firmware (ARM Ltd), DSP/VPU firmware (CIX) via [sky1-firmware](https://github.com/Sky1-Linux/sky1-firmware)

We copied the Sky1-Linux kernel, modules, device tree, and firmware to our Armbian NVMe install. Created a custom GRUB entry with `acpi=off` and an explicit `devicetree` directive. First boot: Panthor loaded, GPU_ID read successfully, CSF firmware loaded, `/dev/dri/renderD128` appeared.

### GPU Power-On Chain (Device Tree Boot)

```
GRUB loads DTB (sky1-orangepi-6-plus.dtb)
  → Kernel boots with acpi=off, parses device tree
    → Panthor driver probes GPU node (compatible = "arm,mali-valhall-csf")
      → Requests power domain ON via ARM SCMI
        → SCMI message sent to TF-A via SMC 0xc2000001
          → TF-A writes RCSU DMRP ENBL register (0x15000014) at EL3
            → GPU power partition activates
              → Panthor reads GPU_ID @ 0x15010000 = 0xc870 (Mali-G720)
                → CSF firmware loaded (mali_csffw.bin)
                  → GPU operational, /dev/dri/renderD128 created
```

### Firmware Dependencies

The GPU requires proprietary firmware not available in mainline Linux:

| File | Vendor | Purpose |
|------|--------|---------|
| `arm/mali/arch12.8/mali_csffw.bin` | ARM Ltd | Mali CSF (Command Stream Frontend) firmware |
| `dsp_fw.bin` | CIX Technology | Tensilica HiFi5 audio DSP firmware |
| `*.fwb` (h264dec, hevcdec, av1dec, etc.) | CIX Technology | VPU video codec firmware |

All firmware is proprietary but redistributable. Pre-built copies are in [`downloads/`](./downloads/README.md).

---

## How to Install

### Option 1: Use Pre-Built Files from This Repo (Fastest)

See [`downloads/README.md`](./downloads/README.md) for the full file listing and copy commands. Summary:

```bash
VERSION="6.19.0-rc8-sky1-rc.r6"

# Kernel, initrd, config
sudo cp downloads/$VERSION/boot/vmlinuz-* /boot/
sudo cp downloads/$VERSION/boot/initrd.img-* /boot/
sudo cp downloads/$VERSION/boot/config-* /boot/

# Device tree
sudo mkdir -p /boot/dtb
sudo cp downloads/$VERSION/boot/dtb/*.dtb /boot/dtb/

# Modules
sudo cp -a downloads/$VERSION/lib-modules-* /lib/modules/
sudo depmod -a $VERSION

# Firmware
sudo cp -a downloads/$VERSION/firmware/arm /lib/firmware/
sudo cp downloads/$VERSION/firmware/dsp_fw.bin /lib/firmware/
sudo cp downloads/$VERSION/firmware/*.fwb /lib/firmware/
```

### Option 2: Install via Sky1-Linux APT

```bash
wget -qO- https://sky1-linux.github.io/apt/key.gpg | sudo tee /usr/share/keyrings/sky1-linux.asc > /dev/null
echo "deb [signed-by=/usr/share/keyrings/sky1-linux.asc] https://sky1-linux.github.io/apt sid main non-free-firmware" | \
  sudo tee /etc/apt/sources.list.d/sky1-linux.list
sudo apt update && sudo apt install sky1-minimal
```

### Option 3: Build from Source

```bash
git clone https://github.com/Sky1-Linux/linux-sky1.git
cd linux-sky1
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.9.tar.xz
tar xf linux-6.18.9.tar.xz && cd linux-6.18.9
for p in ../patches/*.patch; do patch -p1 < "$p"; done
cp ../config/config.sky1 .config
make ARCH=arm64 olddefconfig
make ARCH=arm64 -j$(nproc) Image modules dtbs
```

---

## Boot Configuration

**⚠️ `acpi=off` is mandatory.** Without it, the GPU will not power on.

Create a GRUB entry (see [`gpu/grub/06_sky1`](gpu/grub/06_sky1) for a complete template):

```
menuentry "Sky1 Linux (GPU+NPU)" {
    search --no-floppy --fs-uuid --set=root YOUR-ROOT-UUID
    linux /boot/vmlinuz-6.19.0-rc8-sky1-rc.r6 root=UUID=YOUR-ROOT-UUID ro \
        acpi=off clk_ignore_unused linlon_dp.enable_fb=1 \
        linlon_dp.enable_render=0 fbcon=map:01111111 rootwait
    initrd /boot/initrd.img-6.19.0-rc8-sky1-rc.r6
    devicetree /boot/dtb/sky1-orangepi-6-plus.dtb
}
```

**Boot parameters:**
| Parameter | Purpose |
|-----------|---------|
| `acpi=off` | **Critical.** Forces device tree boot so TF-A powers the GPU |
| `clk_ignore_unused` | Prevents kernel from disabling clocks that appear unused |
| `linlon_dp.enable_fb=1` | Enable framebuffer on Linlon display controller |
| `linlon_dp.enable_render=0` | Disable render mode (use GPU for acceleration) |
| `fbcon=map:01111111` | Map virtual consoles to framebuffer |
| `rootwait` | Wait for root device (NVMe) |

---

## Required Kernel Config Options

If building from source, these are the critical options:

### Architecture & Platform
```
CONFIG_ARCH_CIX=y           # CIX CD8180 SoC support
CONFIG_RESET_SKY1=y         # Sky1 reset controller
CONFIG_PINCTRL_SKY1=y       # Sky1 pin controller
CONFIG_PCI_SKY1=y           # Sky1 PCIe host controller
CONFIG_PHY_CIX_PCIE=y       # CIX PCIe PHY
CONFIG_CIX_MBOX=y           # CIX mailbox (SCMI transport)
```

### GPU & Display
```
CONFIG_DRM_PANTHOR=m         # Mali Panthor GPU driver (mainline)
CONFIG_DRM_CIX=m             # CIX display subsystem
CONFIG_DRM_LINLONDP=m        # Linlon-D60 display controller
CONFIG_DRM_TRILIN_DP_CIX=m   # Trilin DisplayPort (generic)
CONFIG_DRM_TRILIN_DPSUB=m    # Trilin DisplayPort sub-driver
```

---

## Verification

After booting with `acpi=off`:

```bash
# GPU detected?
dmesg | grep panthor
# → panthor 15000000.gpu: [drm] Mali-G720-Immortalis id 0xc870

# Render node present?
ls /dev/dri/renderD128
# → /dev/dri/renderD128

# GPU modules loaded?
lsmod | grep -E 'panthor|linlon|trilin'

# Vulkan working?
vulkaninfo --summary 2>&1 | grep deviceName
# → deviceName = Mali-G720

# OpenGL ES benchmark (requires connected display)
glmark2-es2-drm --winsys-options "drm-device=/dev/dri/card5"
```

---

## Benchmarks

### glmark2 — OpenGL ES 3.1, 1920×1080 HDMI

| Scene | FPS | Scene | FPS |
|-------|:---:|-------|:---:|
| texture nearest | 5,848 | bump normals | 5,847 |
| build (VBO) | 5,388 | conditionals (base) | 5,263 |
| pulsar | 5,142 | function (low) | 4,866 |
| shading gouraud | 4,653 | shading phong | 3,726 |
| effect2d 3×3 | 3,907 | build (no VBO) | 3,386 |
| terrain | 3,201 | jellyfish | 3,201 |
| desktop shadow | 2,974 | refract | 2,636 |
| ideas | 2,274 | effect2d 5×5 | 1,690 |
| desktop blur | 702 | terrain (heavy) | 130 |

### Vulkan Buffer Bandwidth

| Test | Throughput |
|------|:----------:|
| Buffer Fill (256 MB) | **37.4 GB/s** |
| Buffer Copy (256 MB) | **21.4 GB/s** |

### GPU vs NPU Comparison

| Component | Model | Benchmark | Result |
|-----------|-------|-----------|:------:|
| GPU | Mali-G720 MC10 | Vulkan fill | 37.4 GB/s |
| GPU | Mali-G720 MC10 | OpenGL ES (peak) | 5,848 FPS @ 1080p |
| NPU | Zhouyi Z3 | CLIP Text inference | 58.2 inf/s (17.2 ms) |
| NPU | Zhouyi Z3 | CLIP Visual inference | 60.1 inf/s (16.6 ms) |
| NPU | Zhouyi Z3 | YOLOv8n detection | 58.9 inf/s (17.0 ms) |

GPU for graphics and general compute. NPU for dedicated AI inference. See [NPU repository](https://github.com/visorcraft/orange-pi-6-plus-npu).

---

## Known Issues

| Issue | Impact | Workaround |
|-------|--------|------------|
| GPU thermal sensors return err=-2 | Cosmetic (no temperature monitoring) | None needed — GPU runs fine |
| ACPI boot cannot power GPU | No GPU without `acpi=off` | Use device tree boot |
| Display hotplug unreliable | Display may not detect if plugged in after boot | Connect before booting |
| panvk Vulkan non-conformant | May fail edge-case CTS tests | Functional for all tested workloads |
| Killing GPU processes with SIGKILL | Panthor MCU reset fails → GPU unusable | Reboot to recover; let GPU processes exit cleanly |

---

## Hardware Reference

| Component | Detail |
|-----------|--------|
| Board | Orange Pi 6 Plus |
| SoC | CIX CD8180 (Sky1) — reports as "CIX Phecda Board" in DMI |
| CPU | 12 cores — 4× A520 (1.8 GHz) + 8× A720 (2.6 GHz) |
| RAM | 32 GB LPDDR5 |
| GPU | Mali-G720 Immortalis MC10 — 10 shader cores, 900 MHz, ray tracing |
| NPU | ARM China Zhouyi Z3 — 30 TOPS (3 cores × 4 TECs) |
| Display | DP via Linlon-D60 + Trilin-DPSUB, HDMI via PS185 bridge |
| NIC | Intel i226-V × 2 (PCIe, requires `PCI_SKY1` on DT boot) |
| GPU MMIO | 0x15010000 (**Bus Error when unpowered — do not read**) |

### Shader Core Layout

```
shader_present = 0x550555 → 10 shader cores (MC10)
l2_present     = 0x1      → 1 L2 cache slice
tiler_present  = 0x1      → 1 tiling unit
```

### CIX SIP SMC Function Map

```
0xc2000001  SCMI-SMC (GPU power domain — stub on ACPI, real on DT)
0xc2000002  CIX_SIP_SVC_SET_REBOOT_REASON
0xc2000003  CIX_SIP_DSU_HW_CTL
0xc2000004  CIX_SIP_DSU_SET_PD (L3 cache)
0xc200000c  CIX_SIP_SMMU_GOP_CTRL
0xc200000d  CIX_SIP_CPUFREQ_SUPPORT
0xc200000f  CIX_SIP_DP_GOP_CTRL (Display GOP handoff)
0xc2000010  CIX_SIP_SET_DDRLP (DDR low-power)
```

---

## What's in This Repo

```
.
├── README.md                  ← You are here (full documentation)
├── downloads/                 ← Pre-built Sky1-Linux kernel files
│   └── 6.19.0-rc8-sky1-rc.r6/
├── gpu/
│   └── grub/06_sky1           ← GRUB entry template
├── modules/                   ← Diagnostic kernel modules (our ACPI research)
│   ├── gpu_power_diag.c       ← SCMI power domain enumeration
│   ├── gpu_reset_deassert.c   ← Direct SRC register GPU reset
│   ├── gpu_acpi_poweron.c     ← ACPI power resource trigger
│   ├── gpu_dsm_poweron.c      ← ACPI DSM probe (confirmed absent)
│   ├── gpu_clk_enable.c       ← SCMI clock enable attempt
│   └── gpu_poweron.asl        ← ACPI ASL reference
├── patches/                   ← SCMI framework patches (ACPI research)
└── LICENSE
```

---

## License & Attribution

This project is licensed under the [GNU General Public License v2.0](LICENSE).

GPU support is made possible by [Sky1-Linux](https://github.com/Sky1-Linux/), an independent project providing full hardware support for CIX CD8180 SoC-based boards.

| Component | License | Source |
|-----------|---------|--------|
| Sky1-Linux kernel patches | GPL-2.0 | [Sky1-Linux/linux-sky1](https://github.com/Sky1-Linux/linux-sky1) |
| Panthor GPU driver | GPL-2.0 | Mainline Linux (in-tree since 6.10) |
| Linlon-DP, Trilin-DPSUB | GPL-2.0 | Sky1-Linux patches (out-of-tree) |
| Mali CSF firmware | Proprietary (ARM Ltd) | [sky1-firmware](https://github.com/Sky1-Linux/sky1-firmware) |
| VPU/DSP firmware | Proprietary (CIX) | [sky1-firmware](https://github.com/Sky1-Linux/sky1-firmware) |

## See Also

- [NPU Repository](https://github.com/visorcraft/orange-pi-6-plus-npu) — Zhouyi Z3 NPU driver and inference guide
- [Sky1-Linux](https://github.com/Sky1-Linux/) — Community kernel, firmware, and build system
- [Panthor docs](https://docs.kernel.org/gpu/panthor.html) — Mainline Panthor driver documentation
