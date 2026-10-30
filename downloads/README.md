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
├── lib-modules-<version>/          # install as /lib/modules/<version>/
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
# IMPORTANT: do NOT rely on /boot/dtb/ here.
# On Armbian, /boot/dtb is often a symlink owned by the distro and can be overwritten
# during kernel updates. Keep the Sky1 DTB as its own file under /boot.
sudo cp downloads/$VERSION/boot/dtb/sky1-orangepi-6-plus.dtb /boot/sky1-orangepi-6-plus.dtb

# Modules
# The tree in this repo is named lib-modules-$VERSION. depmod looks up
# /lib/modules/$VERSION, so the destination directory must use that name.
sudo rm -rf "/lib/modules/${VERSION:?}"
sudo cp -a downloads/$VERSION/lib-modules-$VERSION /lib/modules/$VERSION
sudo depmod -a $VERSION

# Firmware
sudo cp -a downloads/$VERSION/firmware/arm /lib/firmware/
sudo cp downloads/$VERSION/firmware/dsp_fw.bin /lib/firmware/
sudo cp downloads/$VERSION/firmware/*.fwb /lib/firmware/
```

Then create a GRUB entry. The template is [`../gpu/grub/06_sky1`](../gpu/grub/06_sky1). Replace `YOUR-ROOT-UUID` in that file with the output of `findmnt -no UUID /`, install it as `/etc/grub.d/06_sky1` (mode `0755`), and run `update-grub`. Boot the "Sky1 Linux" entry. An Armbian-generated entry for this vmlinuz does not load the device tree.

## Licenses

- **Kernel + modules + DTB:** GPL-2.0 (Linux kernel + Sky1-Linux patches)
- **Mali firmware (`mali_csffw.bin`):** ARM Ltd, proprietary, redistributable
- **DSP/VPU firmware (`dsp_fw.bin`, `*.fwb`):** CIX Technology, proprietary, redistributable

Firmware files are redistributable per [Sky1-Linux/sky1-firmware](https://github.com/Sky1-Linux/sky1-firmware).
