
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
* MIPI CSI-2 TX AXI4-Lite IP on the external bus (XBUS), at ``0x44A00000``.
* Multi-address I2C target with clock stretching (``neorv32.i2c-target``) on
  the XBUS at ``0x44A10000``, attached to the same I2C bus as the TWD.
* CLINT at ``0xFFF40000`` (SiFive-compatible layout: MSWI ``+0x0000``,
  MTIMECMP ``+0x4000``, MTIME ``+0xBFF8``). MTIME counts at the SYSINFO core
  clock (100 MHz), drives the ``time``/``timeh`` CSRs and raises the machine
  timer interrupt (``mip.MTIP``, bit 7). It is the system timer used by
  Zephyr's ``riscv,machine-timer`` driver. SYSINFO reports it as
  ``IO_CLINT``.
* The CPU implements the RISC-V privileged specification 1.12
  (``mcountinhibit``, ``menvcfg``, Zicntr), matching the RTL.

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

``--enable-debug`` is optional; it is only needed for debugging QEMU itself.
Release (optimised) builds are supported as well.

Boot options
------------

Typical usage is to boot the NEORV32 bootloader as the QEMU ``-bios`` image,
and to provide a raw SPI flash image via an MTD drive. The bootloader will
then jump to the application image placed at the configured flash offset.

Because SYSINFO reports the CLINT, the stock bootloader first runs its
auto-boot countdown (``AUTO_BOOT_TIMEOUT``, 8 s by default) and prints
``Press any key to abort.``; any key on the console drops to the bootloader
prompt, otherwise it loads the executable from SPI flash when the countdown
expires.

The executable header written by ``image_gen`` must match the bootloader
build: current NEORV32 trees write the ``NEO!`` header
(``image_gen -t exe -b 0x00000000``), older bootloaders expect the legacy
header and reject newer images with ``ERROR_SIGNATURE``. Always take the
bootloader and ``image_gen`` from the same NEORV32 revision.

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

* Write ``NW`` bytes, then repeated START and read ``NR`` bytes
  (``WR <addr> <nw> <data...> <nr>``), e.g. a 16-bit register read::

    WR 0x36 2 0x30 0x0a 2

Notes:

* The write command requires an explicit byte count.
* Numeric arguments are parsed with C-style base auto-detection.
  Use explicit ``0x`` prefixes for data bytes if you want hexadecimal values.

Response format from QEMU:

* ``OK`` for successful write (and ``WR`` with ``NR`` = 0)
* ``D xx xx ...`` for read data
* ``ERR address NACK``, ``ERR data NACK``, ``ERR timeout`` or ``ERR <syntax>``

The transfer runs byte by byte. A target implementing the
``TYPE_I2C_TARGET_STRETCH`` interface (``include/hw/i2c/i2c_stretch.h``) may
hold SCL low before a byte; the master then waits on a virtual-clock timer so
the guest keeps running, and fails the command with ``ERR timeout`` after
``stretch-timeout-ms`` (default 1000). With ``bus-hz`` greater than 0 every
byte additionally takes 9 bit times of virtual time; the default 0 runs a
transfer that no target stretches in one go, as before. Commands received
while a transfer is running are queued::

  -device i2c-master-chardev,chardev=twdm,bus=i2c,bus-hz=400000,stretch-timeout-ms=500

When the address changes in a ``WR`` command the master issues STOP + START
instead of a repeated START, because the QEMU I2C core keeps the device
matched at the first START.

One-shot client mode (non-interactive) is also available::

  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "W 0x52 3 0x11 0x22 0x33"
  $ python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "R 0x52 04"

TWD register model
~~~~~~~~~~~~~~~~~~

The TWD (``0xFFEA0000``, interrupt on FIRQ0) follows
``rtl/core/neorv32_twd.vhd`` and ``sw/lib/include/neorv32_twd.h``:

* ``CTRL`` bits 11..16 enable the RX-available, RX-full, TX-empty,
  TX-not-full, communication-begin and communication-end interrupts; bits
  17..20 and 21..24 report log2 of the RX and TX FIFO depth (16 entries
  each); bits 25..28 are the FIFO status flags.
* ``COM_BEG`` (bit 29) is set when the TWD ACKs its address, ``COM_END``
  (bit 30) when that transfer ends. Both are write-1-to-clear: writing a
  ``CTRL`` value with the bit cleared leaves the flag set. ``COM`` (bit 31)
  is high during the transfer. Disabling the TWD clears both flags.
* A master read sends the TX FIFO head and pops it only when the master
  ACKs it, so the byte a master NACKs (the last one of a read) is sent again
  by the next read. An empty TX FIFO sends ``0xFF``.
* There is no clock stretching and no read/write indication for software:
  data a master reads has to be in the TX FIFO before the transfer starts.
* With ``i2c-master-chardev`` every command is one complete transfer, executed
  without the CPU running in between. Writes longer than the RX FIFO are
  NACKed after 16 bytes, and reads beyond the TX FIFO return ``0xFF``.

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

Multi-address I2C target (``neorv32.i2c-target``)
-------------------------------------------------

The machine instantiates the ``i2c_target_axi_ip`` IP
(``videogpu:user:i2c_target_axi_ip:1.0``) as an XBUS slave at ``0x44A10000``
(4 KiB). Its I2C side is a child device on the TWD's ``i2c`` bus, so the TWD
and the IP share one bus as they share the SCL/SDA pins on the FPGA (enable
only one of them for a given address). The interrupt is level-high and is
ORed with the MIPI TX interrupt onto the machine external interrupt
(``mip.MEIP``).

Register map (32-bit accesses only)::

  0x00  ID               0x49324354 ("I2CT")
  0x04  VERSION          0x00010000
  0x08  CONFIG    (RO)   slots, event and TX FIFO depth
  0x0C  CTRL             [0] enable  [1] ACK_CTRL  [2] soft reset
  0x10  STATUS    (RO)   [0] busy  [1] addressed  [2] rw  [6:4] slot
                         [7] stretching  [8] tx wait  [9] ack wait
                         [21:16] event level  [28:24] TX level
  0x14  IRQ_STATUS       [0] event pending (live)  [1] watchdog
                         [2] TX overflow  [3] event drop; write 1 to clear
  0x18  IRQ_ENABLE
  0x1C  EVENT     (RO)   pops one event; [31] valid, [27:24] type
                         (1 START, 2 RX, 3 TXREQ, 4 STOP, 5 WDOG),
                         [18:16] slot, [15] rw, [14] repeated START,
                         [13] ACK needed, [12:8] flushed bytes, [7:0] data
  0x20  TX_DATA   (WO)   push a byte to send
  0x24  TX_CTRL   (WO)   [0] flush  [1] ACK  [2] NACK
  0x28  FILTER_CYCLES / 0x2C SDA_HOLD_CYCLES / 0x30 SETUP_CYCLES
  0x34  STRETCH_TIMEOUT  watchdog, in 100 MHz cycles (0 = off)
  0x38  STRETCH_MAX      longest stretch seen, in cycles; a write clears it
  0x40 + 4n  SLOTn       [6:0] address  [31] enable  (n = 0..7)

Before a read byte with an empty TX FIFO the target queues one ``TXREQ``
event and stretches SCL until software writes ``TX_DATA``. Before a write
byte it stretches while fewer than 4 event FIFO entries are free; in
``ACK_CTRL`` mode it stretches in the ACK slot until ``TX_CTRL`` ACK or NACK.
The watchdog ends a stretch after ``STRETCH_TIMEOUT``: the pending read byte
is sent as ``0xFF`` and a ``WDOG`` event is queued. The TX FIFO is flushed at
every START. The glitch filter and SDA hold/setup timing are not modelled.

Stretching is only visible to a stretch-aware master such as
``i2c-master-chardev``; run QEMU with ``-icount`` if the watchdog has to be
independent of host load.

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

For the I2C target, a repeated START to a different address is modelled as
STOP + START by ``i2c-master-chardev`` (see above).

