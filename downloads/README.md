# Pre-built Sky1-Linux Kernel Downloads

Pre-built kernel, modules, device tree, and firmware extracted from the
[Sky1-Linux GNOME desktop image](https://github.com/Sky1-Linux/) for the
Orange Pi 6 Plus (CIX CD8180).

Each subfolder is named by kernel version.

## Current Versions

| Folder | Kernel | Track | Status |
|--------|--------|-------|--------|
| `6.19.0-rc8-sky1-rc.r6` | Linux 6.19.0-rc8 | Sky1-Linux RC | ✅ GPU + Display working |

## Contents per Version

```
<version>/
├── boot/
│   ├── vmlinuz-<version>           # Kernel image
│   ├── initrd.img-<version>        # Initial ramdisk
│   ├── config-<version>            # Kernel config
│   └── dtb/
│       └── sky1-orangepi-6-plus.dtb  # Device tree blob
├── lib-modules-<version>/          # Kernel modules (/lib/modules/)
└── firmware/
    ├── arm/mali/                   # Mali-G720 CSF firmware (ARM Ltd, proprietary)
    ├── dsp_fw.bin                  # Audio DSP firmware (CIX, proprietary)
    └── *.fwb                       # VPU video codec firmware (CIX, proprietary)
```

## Installation

Copy files to your Armbian rootfs (assumes NVMe boot):

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

Then create a GRUB entry — see [../gpu/grub/11_sky1](../gpu/grub/11_sky1) for the template.

## Licenses

- **Kernel + modules + DTB:** GPL-2.0 (Linux kernel + Sky1-Linux patches)
- **Mali firmware (`mali_csffw.bin`):** ARM Ltd, proprietary, redistributable
- **DSP/VPU firmware (`dsp_fw.bin`, `*.fwb`):** CIX Technology, proprietary, redistributable

Firmware files are redistributable per [Sky1-Linux/sky1-firmware](https://github.com/Sky1-Linux/sky1-firmware).
