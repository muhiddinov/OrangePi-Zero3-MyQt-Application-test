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
- **Storage**: SD card auto-expands its partition and filesystem to
  fill the card on first boot (`board/orangepi/orangepi-zero3/
  rootfs-overlay/etc/init.d/S03rootfs-expand`), raspi-config style.
- **Access**: SSH (`root`/`orangepi`) via OpenSSH.
- **`myqtapp`** (`br2-external/package/myqtapp`): a Qt5 Widgets kiosk
  app that:
  1. Shows a fullscreen Payzone splash screen (embedded as a Qt
     resource, `assets/payzone-logo.png`) for a minimum of 5 seconds,
     with a 1-second fade transition into the video.
  2. Plays a fullscreen, looping video (`QMediaPlayer` + `QVideoWidget`,
     GStreamer backend) from `/root/myvideo.mp4` by default, or
     `$VIDEO_SRC` if set.
  - Video uses software H.264/AAC decode (`gst1-libav`, ffmpeg-based) -
    no hardware (cedrus) decode yet. Audio is intentionally disabled
    (`GST_PLUGIN_FEATURE_RANK=alsasink:0` in `S99myqtapp`) because
    GStreamer's `autoaudiosink` hangs indefinitely opening this board's
    ALSA device - the audio codec/DAI bring-up hasn't been done.
  - The splash/video crossfade is done with a custom `QWidget::paintEvent`
    (`QPainter::setOpacity`) rather than `QGraphicsOpacityEffect`, which
    did not composite visibly under the `linuxfb` raster backend.

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
