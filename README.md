# Orange Pi Zero3 embedded Linux

A custom Buildroot-based embedded Linux image for the Orange Pi Zero3
(Allwinner H618), built to host a custom C++/Qt5 kiosk application
(`myqtapp`), with a branded boot splash and a fullscreen GStreamer-backed
video player.

## Repository layout

```
.
├── buildroot/              git submodule (buildroot/buildroot, master)
├── linux-orangepi/         git submodule (orangepi-xunlong/linux-orangepi,
│                           branch orange-pi-6.1-sun50iw9) - kernel actually
│                           used by the build (see buildroot/local.mk)
├── u-boot/                 git submodule (u-boot/u-boot)
├── arm-trusted-firmware/   git submodule (ARM-software/arm-trusted-firmware)
├── sunxi-tools/            git submodule (linux-sunxi/sunxi-tools)
├── patches/                our local fixes to the submodules above, as
│                           plain git-am-able patch files (the submodules
│                           themselves are kept at pristine upstream HEAD)
├── br2-external/           BR2_EXTERNAL tree: the myqtapp package
│                           (custom Qt5 app) and its Buildroot glue
└── buildroot/configs/orangepi_zero3_defconfig   the board defconfig
```

`linux-mainline/` (a vanilla `torvalds/linux` clone used briefly to test
whether mainline booted better than the vendor fork) is not part of the
tracked project - it's `.gitignore`d local scratch space, kept only for
reference. The board builds and boots against `linux-orangepi`.

## What's been done

Bring-up of a stock Orange Pi Zero3 from nothing to a working kiosk
device, plus the app that runs on it:

- **Bootloader/firmware**: ARM Trusted Firmware (`sun50i_h616` platform,
  shared between H616/H618) + U-Boot 2024.10 (`orangepi_zero3` board
  defconfig), loaded from a GPT-partitioned SD card.
- **Kernel**: Orange Pi's BSP fork, `linux-orangepi` @
  `orange-pi-6.1-sun50iw9` (6.1.31), not mainline - see
  `patches/linux-orangepi/` for why:
  - GPU (Panfrost) probing hung the board indefinitely until the H616
    PRCM power-domain driver was backported from mainline v6.17
    (`drivers/soc/sunxi/sun50i-h6-prcm-ppu.c`) and wired up on the GPU
    node.
  - The PMIC's I2C bus (`r_i2c`) was missing `pinctrl` properties,
    which cascaded into the SD card's power regulator never coming up
    (board hung forever at "Waiting for root device").
  - Thermal zones were firing false shutdowns because the H618's blank
    calibration efuse produces a garbage temperature reading; trip
    type changed from `critical` to `hot`.
  - `sun50i-cpufreq-nvmem.c` had an `int`/pointer type mismatch that
    only GCC 15 rejected.
- **Display**: the EGLFS + DRM/KMS + EGL/GBM + Mesa stack hangs
  indefinitely on this board somewhere between GBM surface creation and
  the first GL context (reproduced with real Panfrost, with
  `LIBGL_ALWAYS_SOFTWARE=1`, and with `MESA_LOADER_DRIVER_OVERRIDE=swrast`
  - not specific to one rendering backend). The GUI runs on `linuxfb`
  + Qt Quick/Widgets' software (raster) paint path instead, which
  draws straight to `/dev/fb0` and bypasses EGL/DRM/GBM/Mesa entirely.
- **Kernel boot logo**: `drivers/video/logo/logo_linux_clut224.ppm` is
  replaced with a 1920x1000, <=224-color rendering of the full Payzone
  logo (`patches/linux-orangepi/0002-*`), shown via
  `fbcon=logo-pos:center,logo-count:1` (a single centered copy instead
  of one per CPU core) and sized to stay just under the fbcon "boot
  logo bigger than screen" cutoff so it still displays. Requires
  `CONFIG_LOGO_LINUX_CLUT224=y` (set in `linux-extras.config`) and a
  non-`quiet` console loglevel - a fully quiet boot reaches userspace
  faster than the HDMI monitor can sync, and the logo is never
  actually seen.
- **Storage**: SD card auto-expands its partition and filesystem to
  fill the card on first boot (`board/orangepi/orangepi-zero3/
  rootfs-overlay/etc/init.d/S03rootfs-expand`), raspi-config style.
- **USB auto-mount**: a `mdev` rule + `/etc/mdev/usbmount.sh` mount any
  inserted USB mass-storage partition (`sd[a-z][0-9]*`) read-only to
  `/mnt/usb` (vfat is built into the kernel; exfat/ntfs3 are loaded as
  modules on demand so `mount`'s auto-detection can pick up either).
- **WiFi**: onboard SDIO chip is a Unisoc/Spreadtrum UWE5622
  (Marlin3-Lite, chip_id `0x2355b001`); `sprdwl_ng` is auto-loaded via
  `/etc/modules-load.d/wifi.conf` (mdev's `$MODALIAS` rule doesn't
  pick it up). Needs `/lib/firmware/wcnmodem.bin` +
  `wifi_2355b001_1ant.ini`, extracted from Orange Pi's own official OS
  image for this board - the `wcnmodem.bin.hex` bundled in
  `linux-orangepi`'s own driver tree is a reference blob tagged for a
  *different* chip variant (Marlin3/Marlin3E) and silently fails to
  match ours. `wpa_supplicant`/`iw` are enabled for scanning/
  connecting; verified with `iw dev wlan0 scan` finding real networks.
  Not yet wired up to auto-connect on boot (see "What's next").
- **Access**: SSH (`root`/`orangepi`) via OpenSSH.
- **`myqtapp`** (`br2-external/package/myqtapp`): a Qt5 Widgets kiosk
  app that:
  1. Shows a fullscreen Payzone splash screen (embedded as a Qt
     resource, `assets/payzone-logo.png`) for a minimum of 5 seconds,
     with a 1-second fade transition into the video.
  2. Plays a fullscreen, looping video (`QMediaPlayer` + `QVideoWidget`,
     GStreamer backend). If a USB drive is mounted at `/mnt/usb` and
     has any `*.mp4` files at its top level, those are played in name
     order, looping back to the first after the last; otherwise it
     falls back to `/root/myvideo.mp4` (bundled in the image) or
     `$VIDEO_SRC` if set. Checked every 2s, so plugging/unplugging a
     drive at runtime switches the playlist without a restart.
  - Looping/advancing is done manually (one `setMedia()`+`play()` call
    at a time on `EndOfMedia`) rather than with `QMediaPlaylist`'s own
    `Loop` mode - the latter intermittently failed with "Internal data
    stream error" when re-opening the next (or same, single-item)
    source on this board's GStreamer backend, even though the same
    file played back fine standalone via `gst-launch-1.0`, twice in a
    row.
  - Video uses software H.264/AAC decode (`gst1-libav`, ffmpeg-based) -
    no hardware (cedrus) decode yet. Audio is intentionally disabled
    (`GST_PLUGIN_FEATURE_RANK=alsasink:0` in `S99myqtapp`) because
    GStreamer's `autoaudiosink` hangs indefinitely opening this board's
    ALSA device - the audio codec/DAI bring-up hasn't been done.
  - The splash/video crossfade is done with a custom `QWidget::paintEvent`
    (`QPainter::setOpacity`) rather than `QGraphicsOpacityEffect`, which
    did not composite visibly under the `linuxfb` raster backend.
  - The splash window's geometry is set explicitly from
    `QGuiApplication::primaryScreen()->geometry()` before the first
    show/paint - without this, the window briefly paints at Qt's
    default fallback size in the top-left corner before the fullscreen
    resize takes effect, visible as a black/white flash during
    `myqtapp`'s (dynamic-linking-heavy) startup.
  - `QT_QPA_FB_HIDECURSOR=1` disables the `linuxfb` platform's mouse
    cursor, which otherwise defaults to `Qt::ArrowCursor` at (0,0) -
    there's no pointer device on this kiosk anyway.

## What's next

Not done yet, in roughly the order they'd likely come up:

- **U-Boot splash**: the `orangepi_zero3` U-Boot defconfig has no video
  driver enabled at all (`VIDEO_DE2`/`SUNXI_DE2`/HDMI are all off) -
  showing a logo this early means bringing up display output in U-Boot
  from scratch, which is untested territory on this board and could
  hit the same kind of issues as the kernel's GPU/display bring-up did.
- **Audio**: the codec/DAI (`audiocodec`/`ahubdam`/`ahubhdmi` ALSA
  cards are present per `/proc/asound/cards`, but untested) hasn't
  been brought up - GStreamer's `autoaudiosink` hangs opening it, so
  video playback is currently silent (`GST_PLUGIN_FEATURE_RANK=
  alsasink:0`, see above).
- **Hardware video decode**: `cedrus`/`sunxi_cedrus` (`/dev/video0`) is
  registered by the kernel, but `myqtapp` decodes video in software via
  `gst1-libav`. Wiring up the stateless V4L2 M2M decode path (the
  `v4l2codecs` GStreamer plugin) needs udev-style hotplug support that
  isn't there yet (this rootfs uses `mdev`).
- **WiFi auto-connect**: scanning/connecting work manually
  (`wpa_supplicant` and `udhcpc`), but nothing brings the interface up
  or connects automatically on boot yet - needs a `wpa_supplicant.conf`
  with real credentials plus an init script (or `ifupdown`/similar),
  which isn't something to bake into the image with a real password
  anyway.
- **Bluetooth**: the UWE5622 is a combo WiFi+BT chip and
  `sprdbt_tty.ko` is built, but BT hasn't been touched (the
  `bt_configure_*.ini` files are bundled for when it is).

## WiFi/BT firmware provenance

`board/orangepi/orangepi-zero3/rootfs-overlay/lib/firmware/` bundles
`wcnmodem.bin`, `wifi_2355b001_1ant.ini`, and `bt_configure_*.ini`
directly (already committed, via `patches/buildroot/`), so a normal
build just works - you don't need to redo this. It's documented here
only in case the firmware ever needs to be re-extracted (e.g. a
different chip revision, or Orange Pi ships an updated blob):

1. Download Orange Pi's official OS image for the Zero3 (any image
   built for this exact board works, since the firmware is tied to
   the board's onboard chip, not the OS - the `Opios-arch-aarch64-xfce-
   opizero3-*.img.xz` Arch build is what was used here).
2. Decompress it: `xz -dk -T0 -c theimage.img.xz > opios.img` (uses a
   few GB of disk - do this somewhere with room, not `/tmp`).
3. Find the Linux rootfs partition's byte offset:
   `fdisk -l opios.img` lists partitions in 512-byte sectors; the
   second partition (`Linux`, not the small FAT32 boot one) is the
   one you want. Offset in bytes = start sector * 512.
4. Loop-mount it read-only:
   `sudo mount -o loop,offset=<bytes>,ro opios.img mnt_rootfs`
5. Copy out `mnt_rootfs/lib/firmware/wcnmodem.bin` and
   `mnt_rootfs/lib/firmware/wifi_<chipid>_1ant.ini` into
   `board/orangepi/orangepi-zero3/rootfs-overlay/lib/firmware/`. The
   exact chip ID for the `_1ant.ini` filename is board-specific -
   boot with just `wcnmodem.bin` in place first, and read it back out
   of `dmesg` (`marlin_get_wcn_chipid: chipid: 0x...` and
   `wifi ini path = /lib/firmware/wifi_<chipid>_1ant.ini`).
6. `sudo umount mnt_rootfs`, then rebuild.

Why this is needed at all: `linux-orangepi`'s own
`drivers/net/wireless/uwe5622/unisocwcn/fw/wcnmodem.bin.hex` looks
like the right file (same directory, same name minus `.hex`, `WCNM`
container format), but it's a reference blob tagged for a *different*
chip variant (`3EAA`/`30AC`, i.e. Marlin3/Marlin3E) than this board's
actual chip (Marlin3-Lite, which needs an `MLAA`/`MLAB`-tagged image).
The driver silently fails to find a matching tag
(`marlin_firmware_parse_image imginfo is NULL`) and never brings the
chip up. The official OS image's `wcnmodem.bin` has no such wrapper
at all - it's the final, already-selected firmware for this one chip,
used as-is.

## How to build

### 1. Clone

```sh
git clone --recurse-submodules <this-repo-url> OrangePi
cd OrangePi
# if you cloned without --recurse-submodules:
#   git submodule update --init --recursive
```

### 2. Apply the local fixes to the kernel and Buildroot

The submodules are kept at pristine upstream HEAD; our changes live as
patch files and must be applied once after cloning:

```sh
cd linux-orangepi && git am ../patches/linux-orangepi/*.patch && cd ..
cd buildroot        && git am ../patches/buildroot/*.patch        && cd ..
```

### 3. Configure and build

```sh
cd buildroot
make orangepi_zero3_defconfig
make -j$(nproc)
```

This builds ATF, U-Boot, the kernel, all of Qt5/GStreamer, `myqtapp`,
and produces `output/images/sdcard.img`.

### 4. Flash

Write `output/images/sdcard.img` to an SD card (e.g. with
[Balena Etcher](https://etcher.balena.io/)), insert it into the Orange
Pi Zero3, and power it on. First boot auto-expands the rootfs to fill
the card; SSH is available at `root`/`orangepi` once it's up.

### 5. Iterating on `myqtapp` only

Once the device is flashed and reachable over SSH, rebuilding just the
app (no need to reflash) is fast:

```sh
cd buildroot
make myqtapp-rebuild
scp output/target/usr/bin/myqtapp root@<device-ip>:/usr/bin/myqtapp
ssh root@<device-ip> "/etc/init.d/S99myqtapp restart"
```

This only works for changes confined to `myqtapp` itself. Any change
that adds or touches a **system-level** package (a new Buildroot
option, a new library, a kernel config change) needs a full
`make -j$(nproc)` and a reflash, since the running SD card image
won't have the new package installed otherwise.
