
NEORV32 Soft SoC (``neorv32``)
==============================

The ``neorv32`` machine models a minimal NEORV32-based SoC sufficient to
exercise the stock NEORV32 bootloader and run example applications from an
emulated SPI NOR flash. It exposes a UART for console I/O and an MTD-backed
SPI flash device that can be populated with user binaries.

Neorv32 full repo:
https://github.com/stnolting/neorv32

Current QEMU implementation base on commit 7d0ef6b2 in Neorv32 repo.

Supported devices
-----------------

The ``neorv32`` machine provides the core peripherals needed by the
bootloader and examples:

* UART for console (mapped to the QEMU stdio when ``-nographic`` or
  ``-serial stdio`` is used).
* SPI controller connected to an emulated SPI NOR flash (exposed to the
  guest via QEMU's ``if=mtd`` backend).
* Basic timer/CLINT-like facilities required by the example software.

(Exact register maps and optional peripherals depend on the QEMU version and
the specific patch series you are using.)


QEMU build configuration:
-------------------------

From the command line::

  $ /path/to/qemu/configure \
  --python=/usr/local/bin/python3.12 \
  --target-list=riscv32-softmmu \
  --enable-fdt \
  --enable-debug \
  --disable-vnc \
  --disable-gtk

Boot options
------------

Typical usage is to boot the NEORV32 bootloader as the QEMU ``-bios`` image,
and to provide a raw SPI flash image via an MTD drive. The bootloader will
then jump to the application image placed at the configured flash offset.

Preparing the SPI flash with a “Hello World” example
----------------------------------------------------

1. Create a 64 MiB flash image (filled with zeros)::

   $ dd if=/dev/zero of=$HOME/flash_contents.bin bs=1 count=$((0x04000000))

2. Place your application binary at the **4 MiB** offset inside the flash.
   Replace ``/path/to/neorv32_exe.bin`` with the path to your compiled
   example application (e.g., the NEORV32 ``hello_world`` example)::

   $ dd if=/path/to/neorv32_exe.bin of=$HOME/flash_contents.bin bs=1 seek=$((0x00400000)) conv=notrunc

Running the “Hello World” example
---------------------------------

Run QEMU with the NEORV32 bootloader as ``-bios`` and attach the prepared
flash image via the MTD interface. Replace the placeholder paths with your
local paths::

  $ /path/to/qemu-system-riscv32 -nographic -machine neorv32 \
      -bios /path/to/neorv32/bootloader/neorv32_raw_exe.bin \
      -drive file=$HOME/flash_contents.bin,if=mtd,format=raw

Notes:

* ``-nographic`` routes the UART to your terminal (Ctrl-A X to quit when
  using the QEMU monitor hotkeys; or just close the terminal).
* The bootloader starts first and will transfer control to your application
  located at the 4 MiB offset of the flash image.
* If you prefer, you can use ``-serial stdio`` instead of ``-nographic``.

Host-driven TWD testing (Python over socket)
--------------------------------------------

For interactive host-to-firmware testing via the emulated TWD path, run QEMU
with a socket chardev and attach the ``i2c-master-chardev`` helper device on
the NEORV32 internal TWD I2C bus.

Using your regular bootloader command as a base::

  $ /home/smishash/shonot/sources/qemu/build/qemu-system-riscv32 \
      -nographic \
      -machine neorv32 \
      -bios /mnt/shonot/fpga_projects/neorv32/sw/bootloader/neorv32_raw_exe.bin \
      -chardev socket,id=twdm,path=/tmp/twd-i2c.sock,server=on,wait=off \
      -device i2c-master-chardev,chardev=twdm,bus=i2c

Then use the helper client script from another shell::

  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock

Command format supported by the helper:

* Write bytes to an I2C address::

    W 0x52 03 11 22 33

* Read ``N`` bytes from an I2C address::

    R 0x52 04

Response format from QEMU:

* ``OK`` for successful write
* ``D xx xx ...`` for read data
* ``ERR ...`` for malformed command / NACK / other transfer error

One-shot client mode (non-interactive) is also available::

  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "W 0x52 03 11 22 33"
  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "R 0x52 04"

Machine-specific options
------------------------

Unless otherwise noted by the patch series, there are no special board
options beyond the standard QEMU options shown above. Commonly useful
generic options include:

* ``-s -S`` to open a GDB stub on TCP port 1234 and start paused, so you can
  debug both QEMU and the guest.
* ``-d guest_errors,unimp`` (or other trace flags) for additional logging.

Example: debugging with GDB::

  $ /path/to/qemu-system-riscv32 -nographic -machine neorv32 \
      -bios /path/to/neorv32/bootloader/neorv32_raw_exe.bin \
      -drive file=$HOME/flash_contents.bin,if=mtd,format=raw \
      -s -S

  # In another shell:
  $ riscv32-unknown-elf-gdb /path/to/neorv32/bootloader/main.elf
  (gdb) target remote :1234


Known limitations
-----------------

This is a functional model intended for software bring-up and testing of
example programs. It may not model all timing details or every optional
peripheral available in a specific NEORV32 SoC configuration.

