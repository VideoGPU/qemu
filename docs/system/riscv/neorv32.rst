
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
* TWD (two-wire device) I2C slave on an internal I2C bus.
* MIPI CSI-2 TX AXI4-Lite IP on the external bus (XBUS).
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

* Write bytes to an I2C address (``W <addr> <count> <data...>``)::

  W 0x52 3 0x11 0x22 0x33

* Read ``N`` bytes from an I2C address::

    R 0x52 04

Notes:

* The write command requires an explicit byte count.
* Numeric arguments are parsed with C-style base auto-detection.
  Use explicit ``0x`` prefixes for data bytes if you want hexadecimal values.

Response format from QEMU:

* ``OK`` for successful write
* ``D xx xx ...`` for read data
* ``ERR ...`` for malformed command / NACK / other transfer error

One-shot client mode (non-interactive) is also available::

  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "W 0x52 3 0x11 0x22 0x33"
  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "R 0x52 04"

MIPI CSI-2 TX IP (``neorv32.mipi-tx``)
--------------------------------------

The machine instantiates the MIPI CSI-2 TX AXI4-Lite IP (``mipi_tx_axi_ip``)
as an external slave on the processor's external bus interface (XBUS), at
``0x44A00000``. The matching NEORV32 software driver is
``sw/lib/source/neorv32_mipi_tx.c``; ``sw/example/demo_mipi_tx`` is a ready to
run example.

Because the IP lives on the XBUS and not in the NEORV32 IO region, it has no
SYSINFO feature bit of its own. ``neorv32_mipi_tx_available()`` checks the
``XBUS`` bit in ``SYSINFO.SOC``, which this machine reports as implemented.

Register map (32-bit accesses only)::

  0x00  CONTROL          [0] start (W, self-clearing)   [1] stop
                         [2] irq enable                 [3] legacy trigger
                         [4] irq clear (W, self-clearing)
                         [5] debug overlay enable
  0x04  STATUS  (RO)     [0] hs active   [1] hs data valid
                         [2] frame done  [3] irq pending
  0x10  PIXELS_PER_LINE  payload bytes per line
  0x14  N_LINES          lines per frame
  0x18  TYPE_VC          [5:0] CSI-2 data type, [9:8] virtual channel
  0x1c  FRAME_END_WORD   payload of the Frame End short packet
  0x20  TLP_SOT_DELAY_CLK
  0x24  TLPX_DELAY_CLK
  0x28  TLP_SOT_DELAY_DATA
  0x2c  TLPX_DELAY_DATA
  0x30  TLP_SOT_SHORT
  0x34  THS_PREPARE
  0x38  THS_ZERO
  0x3c  THS_EXIT         D-PHY timing, in 10ns ticks
  0x40  N_MIPI_LANES     1 = 1 lane, 2 = 2 lanes, 3 = 4 lanes

Reset values match the RTL defaults (QSXGA RAW10: 3240 bytes per line, 1944
lines, 2 lanes). The configuration registers are latched by the start pulse,
so software programs them first and then writes ``CONTROL[0]``.

The IP's interrupt output is wired to the CPU machine external interrupt
(``mie.MEIE``), gated by ``CONTROL[2]``. The ``irq pending`` status bit is
latched on every frame-done edge regardless of that gate, so the flag can also
be polled, which is what ``demo_mipi_tx`` does by default.

Frame transmission is modelled with a virtual-clock timer whose duration is
derived from the programmed geometry, lane count and D-PHY timing registers.

Capturing the generated CSI-2 stream
------------------------------------

The device can write the CSI-2 packet stream of every transmitted frame to a
file. Note that the type name contains a dot, so the long ``-global`` syntax
has to be used::

  $ qemu-system-riscv32 -nographic -machine neorv32 \
      -bios /path/to/neorv32_raw_exe.bin \
      -drive file=$HOME/flash_contents.bin,if=mtd,format=raw \
      -global driver=neorv32.mipi-tx,property=dumpfile,value=/tmp/mipi_tx.bin

The file contains back-to-back CSI-2 packets: a Frame Start short packet, one
long packet per line (4-byte header with ECC, payload, 2-byte CRC) and a Frame
End short packet. It can be fed directly to the ``mipi_decoder`` tooling::

  $ mipi-decoder decode /tmp/mipi_tx.bin --frame-summary
  $ mipi-decoder decode /tmp/mipi_tx.bin --export-raw10-png-dir /tmp/frames

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

For the MIPI CSI-2 TX IP specifically, the D-PHY line coding (LP/HS states,
sync sequences, per-lane distribution) is not modelled. The colorbar payload
in the dump file reproduces the RTL's 75% colorbar and its per-frame overlay
scroll, but the band widths follow the programmed line length instead of the
RTL's compile-time ``PIXELS_PER_LINE_MAX`` generic.

