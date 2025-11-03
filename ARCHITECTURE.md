# QEMU Architecture Overview

**Version:** 10.1.50  
**Last Updated:** 2025-11-03  
**Purpose:** Comprehensive architectural reference for understanding QEMU codebase structure

## Table of Contents
1. [Introduction](#introduction)
2. [Core Architecture](#core-architecture)
3. [Major Subsystems](#major-subsystems)
4. [Directory Structure](#directory-structure)
5. [Target Architectures](#target-architectures)
6. [Build System](#build-system)
7. [Key Technologies](#key-technologies)

## Introduction

QEMU (Quick Emulator) is a generic and open-source machine and userspace emulator and virtualizer. It provides:

- **Full System Emulation**: Emulates complete machines in software without hardware virtualization
- **User Mode Emulation**: Runs binaries compiled for one architecture on another architecture
- **Virtualization**: Integrates with hypervisors (KVM, Xen, HVF, WHPX) for near-native performance

### Key Capabilities
- Dynamic binary translation using TCG (Tiny Code Generator)
- Support for 20+ target architectures
- Integration with multiple acceleration frameworks
- Comprehensive device emulation
- Live migration support
- Snapshot and record/replay functionality

## Core Architecture

### High-Level Components

```
┌─────────────────────────────────────────────────────────┐
│                    User Interfaces                       │
│         (Monitor, GDB, QAPI, GUI, CLI)                  │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│              Device Emulation (hw/)                      │
│  CPU, Memory, PCI, USB, Network, Storage, Display, etc. │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│           QEMU Object Model (QOM)                        │
│      Type System, Properties, Lifecycle Management       │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│              CPU Emulation (target/)                     │
│     Architecture-specific CPU implementation             │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│            Acceleration Layer (accel/)                   │
│     TCG, KVM, HVF, WHPX, Xen, MSHV, NVMM               │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│              Support Infrastructure                      │
│  Block I/O, Network, Migration, Memory, Tracing, etc.   │
└─────────────────────────────────────────────────────────┘
```

### Execution Modes

1. **System Mode** (`-softmmu`): Full machine emulation
   - Emulates complete hardware including CPU, RAM, peripherals
   - Used for running operating systems
   
2. **User Mode** (`linux-user`, `bsd-user`): Application-level emulation
   - Emulates only CPU and system calls
   - Runs binaries compiled for different architectures

## Major Subsystems

### 1. TCG (Tiny Code Generator)
**Location:** `tcg/`, `target/*/tcg/`

The dynamic binary translator that converts guest instructions to host instructions:
- **Translation Blocks (TB)**: Basic unit of translated code
- **IR (Intermediate Representation)**: Architecture-independent intermediate language
- **Direct Block Chaining**: Optimizes TB transitions
- **Register Allocation**: Maps guest registers to host registers

### 2. QOM (QEMU Object Model)
**Location:** `qom/`, `include/qom/`

Object-oriented framework for QEMU:
- **Type System**: Dynamic type registration and inheritance
- **Properties**: Expose internal state for configuration
- **Lifecycle Management**: Object creation, initialization, destruction
- **Composition Tree**: Represents machine hierarchy (`info qom-tree`)

### 3. Device Emulation
**Location:** `hw/`

Comprehensive hardware emulation organized by type:
- **CPU devices** (`hw/cpu/`)
- **Memory controllers** (`hw/mem/`)
- **PCI/PCIe devices** (`hw/pci/`, `hw/pci-bridge/`, `hw/pci-host/`)
- **Storage controllers** (`hw/ide/`, `hw/scsi/`, `hw/nvme/`)
- **Network devices** (`hw/net/`)
- **Display adapters** (`hw/display/`)
- **Input devices** (`hw/input/`)
- **USB controllers and devices** (`hw/usb/`)
- **Interrupt controllers** (`hw/intc/`)
- **Timers** (`hw/timer/`)
- **DMA controllers** (`hw/dma/`)

### 4. Block Layer
**Location:** `block/`, `block.c`, `blockdev.c`

Handles all disk image and block device operations:
- **Image Formats**: qcow2, raw, vmdk, vdi, vhdx, etc.
- **Block Drivers**: File, network (NBD, iSCSI, NFS), backend support
- **I/O Scheduling**: Throttling, queuing, priorities
- **Snapshots**: Internal and external snapshot support
- **Encryption**: LUKS support

### 5. Acceleration Frameworks
**Location:** `accel/`

Hardware and software acceleration:
- **TCG** (`accel/tcg/`): Software dynamic translation
- **KVM** (`accel/kvm/`): Linux Kernel-based Virtual Machine
- **HVF** (`accel/hvf/`): macOS Hypervisor Framework
- **WHPX** (Windows): Windows Hypervisor Platform
- **Xen** (`accel/xen/`): Xen hypervisor integration
- **MSHV** (`accel/mshv/`): Microsoft Hypervisor
- **QTest** (`accel/qtest/`): Testing framework

### 6. Memory Management
**Location:** `system/`, `include/exec/memory.h`

- **AddressSpace**: Represents view of memory from device/CPU perspective
- **MemoryRegion**: Represents a region of memory or I/O
- **RAMBlock**: Physical RAM allocation
- **IOMMU**: I/O memory management unit support
- **Dirty Tracking**: For live migration and snapshots

### 7. Migration
**Location:** `migration/`

Live migration and snapshot functionality:
- **Migration Protocols**: TCP, Unix sockets, exec, fd
- **State Serialization**: VMState framework
- **Compression**: Multi-threaded compression
- **Post-copy Migration**: Allows faster migration start
- **COLO-FT**: Coarse-grained Lock-stepping for fault tolerance

### 8. QAPI (QEMU API)
**Location:** `qapi/`

Machine-readable API definition:
- **Schema Definition**: JSON schema for commands and events
- **Code Generation**: Automatic C code generation
- **QMP (QEMU Machine Protocol)**: JSON-based management protocol
- **HMP (Human Monitor Protocol)**: Human-readable command interface

### 9. Tracing
**Location:** `trace/`, `trace-events`

Performance analysis and debugging:
- **Multiple Backends**: log, ftrace, dtrace, syslog, simple
- **Dynamic Enable/Disable**: Runtime control of trace points
- **Zero-overhead**: When disabled, minimal performance impact

### 10. Guest Agent (QEMU GA)
**Location:** `qga/`

Agent running inside guest for management:
- **Guest Information**: OS version, network config, filesystem info
- **Guest Control**: File operations, command execution
- **Integration**: Works with management tools like libvirt

## Directory Structure

### Top-Level Directories

| Directory | Purpose |
|-----------|---------|
| `accel/` | Acceleration frameworks (TCG, KVM, HVF, etc.) |
| `audio/` | Audio backend implementations |
| `authz/` | Authorization framework |
| `backends/` | Various host resource backends (RNG, crypto, memory) |
| `block/` | Block device and image format implementations |
| `bsd-user/` | BSD user mode emulation |
| `chardev/` | Character device backends (serial, parallel, etc.) |
| `common-user/` | Common code for user mode emulation |
| `configs/` | Build configuration files |
| `contrib/` | Community-contributed tools and plugins |
| `crypto/` | Cryptographic algorithm implementations |
| `disas/` | Disassemblers for various architectures |
| `docs/` | Comprehensive documentation |
| `dump/` | VM memory dump functionality |
| `ebpf/` | eBPF support for virtio-net RSS |
| `fpu/` | Floating-point emulation |
| `fsdev/` | VirtFS (9P filesystem) support |
| `gdbstub/` | GDB remote debugging protocol implementation |
| `gdb-xml/` | Architecture descriptions for GDB |
| `host/` | Host-specific optimized code |
| `hw/` | Hardware device emulation |
| `include/` | Header files (mirrors source structure) |
| `io/` | I/O channel abstractions |
| `libdecnumber/` | Decimal number arithmetic library |
| `linux-headers/` | Linux kernel headers for KVM |
| `linux-user/` | Linux user mode emulation |
| `migration/` | Live migration framework |
| `monitor/` | Monitor (HMP/QMP) implementation |
| `nbd/` | Network Block Device server |
| `net/` | Network backend support |
| `pc-bios/` | Pre-built firmware and boot images |
| `plugins/` | TCG plugin framework |
| `python/` | Python build and test utilities |
| `qapi/` | QAPI schema definitions |
| `qga/` | QEMU Guest Agent |
| `qobject/` | QEMU Object implementation |
| `qom/` | QEMU Object Model implementation |
| `replay/` | Record/replay framework |
| `roms/` | Source for firmware and ROMs |
| `rust/` | Rust integration (new devices in Rust) |
| `scripts/` | Build, test, and utility scripts |
| `scsi/` | SCSI subsystem support |
| `semihosting/` | Semihosting implementation |
| `stats/` | Statistics monitoring |
| `storage-daemon/` | QEMU storage daemon |
| `stubs/` | Stub functions for minimal builds |
| `subprojects/` | Meson subprojects |
| `system/` | System mode implementation (CPU, MMU, boot) |
| `target/` | Target architecture-specific code |
| `tcg/` | Tiny Code Generator |
| `tests/` | Test suite |
| `tools/` | Standalone tools |
| `trace/` | Tracing framework |
| `ui/` | User interface implementations (GTK, SDL, VNC, etc.) |
| `util/` | Utility functions |

### Key Source Files

| File | Purpose |
|------|---------|
| `block.c` | Block layer core (256K+ lines) |
| `blockdev.c` | Block device management (114K lines) |
| `blockjob.c` | Block job operations |
| `configure` | Build configuration script |
| `meson.build` | Meson build system configuration |
| `qemu-img.c` | Disk image utility (187K lines) |
| `qemu-io.c` | Block I/O testing tool |
| `qemu-nbd.c` | NBD server utility |

## Target Architectures

**Location:** `target/`

QEMU supports the following target architectures:

| Architecture | Directory | Description |
|--------------|-----------|-------------|
| **x86** | `target/i386/` | Intel/AMD x86 (32-bit and 64-bit) |
| **ARM** | `target/arm/` | ARM (32-bit and 64-bit/AArch64) |
| **RISC-V** | `target/riscv/` | RISC-V (32-bit and 64-bit) |
| **PowerPC** | `target/ppc/` | PowerPC (32-bit and 64-bit) |
| **MIPS** | `target/mips/` | MIPS (32-bit and 64-bit) |
| **S390x** | `target/s390x/` | IBM System/390 and z/Architecture |
| **SPARC** | `target/sparc/` | SPARC and SPARC64 |
| **Alpha** | `target/alpha/` | DEC Alpha |
| **HPPA** | `target/hppa/` | HP PA-RISC |
| **M68K** | `target/m68k/` | Motorola 68000 |
| **LoongArch** | `target/loongarch/` | LoongArch 64-bit |
| **Xtensa** | `target/xtensa/` | Tensilica Xtensa |
| **Hexagon** | `target/hexagon/` | Qualcomm Hexagon DSP |
| **AVR** | `target/avr/` | Atmel AVR |
| **Microblaze** | `target/microblaze/` | Xilinx MicroBlaze |
| **OpenRISC** | `target/openrisc/` | OpenRISC 1000 |
| **CRIS** | `target/cris/` | Axis CRIS |
| **RX** | `target/rx/` | Renesas RX |
| **SH4** | `target/sh4/` | SuperH |
| **TriCore** | `target/tricore/` | Infineon TriCore |

### Target Architecture Structure

Each target contains:
- **TCG Translation**: Guest instruction to TCG IR conversion
- **CPU Models**: Definitions for different CPU variants
- **Helper Functions**: Complex operations called from translated code
- **GDB Support**: Architecture-specific GDB integration
- **Machine Definitions**: Board and system configurations

## Build System

### Configuration

**Tool:** `configure` (shell script) + Meson

**Usage:**
```bash
mkdir build
cd build
../configure [options]
make
```

**Key Configuration Options:**
- `--target-list`: Select target architectures to build
- `--enable-kvm`: Enable KVM support
- `--enable-debug`: Build with debug symbols
- `--disable-werror`: Disable -Werror
- `--enable-rust`: Enable Rust support (requires rustc >= 1.83.0)

### Build System Files

| File | Purpose |
|------|---------|
| `configure` | Main configuration script |
| `meson.build` | Meson build definitions (top-level) |
| `meson_options.txt` | Build options |
| `Makefile` | Make wrapper around Meson |
| `*/meson.build` | Per-directory build definitions |

### Build Outputs

- **System Mode Binaries**: `qemu-system-<arch>`
- **User Mode Binaries**: `qemu-<arch>`
- **Tools**: `qemu-img`, `qemu-io`, `qemu-nbd`, etc.

## Key Technologies

### 1. Dynamic Binary Translation (DBT)

QEMU uses TCG for efficient guest-to-host translation:
- **Translation Phase**: Guest instructions → TCG IR → Host instructions
- **Execution Phase**: Execute translated code with periodic interrupt checks
- **Optimization**: Block chaining, register allocation, constant propagation

### 2. Device Models

Devices are implemented using QOM:
```c
// Device structure
typedef struct MyDevice {
    DeviceState parent_obj;
    // Device state
} MyDevice;

// Register device type
static const TypeInfo my_device_info = {
    .name = TYPE_MY_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MyDevice),
};
```

### 3. Memory Regions

Memory is organized hierarchically:
```c
MemoryRegion *system_memory;  // Root memory region
MemoryRegion *ram;            // RAM region
MemoryRegion *rom;            // ROM region
MemoryRegion *mmio;           // MMIO region
```

### 4. QAPI/QMP

Machine-manageable interface:
```json
// QMP command
{ "execute": "query-status" }

// QMP response
{ "return": { "status": "running" } }
```

## Architecture Patterns

### Event Loop

QEMU uses an event-driven architecture:
- **Main Loop** (`system/main-loop.c`): Central event dispatcher
- **AIO Context** (`util/aio-*.c`): Asynchronous I/O handling
- **IOThread** (`iothread.c`): Additional event loops for I/O

### Coroutines

Block layer uses coroutines for async operations:
- **Cooperative Multitasking**: Explicit yield points
- **Stack Management**: Per-coroutine stacks
- **Zero-copy**: Efficient I/O operations

### Visitors Pattern

Serialization uses visitor pattern:
- **QObject Visitor**: JSON serialization
- **String Visitor**: String representation
- **VMState**: Migration state serialization

## Testing Infrastructure

**Location:** `tests/`

### Test Categories

1. **Unit Tests** (`tests/unit/`): Low-level functionality tests
2. **QTest** (`tests/qtest/`): Device emulation tests
3. **TCG Tests** (`tests/tcg/`): CPU instruction tests
4. **Functional Tests** (`tests/functional/`): Full VM boot tests
5. **I/O Tests** (`tests/qemu-iotests/`): Block layer tests

### CI/CD

- **GitLab CI** (`.gitlab-ci.yml`, `.gitlab-ci.d/`)
- **Docker Containers** (`tests/docker/`)
- **Multiple Platforms**: Linux, Windows, macOS, BSD

## Maintenance Notes

This document serves as a living reference for QEMU's architecture. It should be updated when:

1. **Major architectural changes** are merged to master
2. **New subsystems** are added
3. **Directory structure** changes significantly
4. **Key technologies** are replaced or updated

### Update Process

1. Monitor commits to the master branch
2. Review architectural impact of changes
3. Update relevant sections in this document
4. Commit with descriptive message referencing the upstream changes

## References

- **Official Documentation**: https://www.qemu.org/documentation/
- **Developer Docs**: https://www.qemu.org/docs/master/devel/index.html
- **Wiki**: https://wiki.qemu.org/
- **Source Repository**: https://gitlab.com/qemu-project/qemu
- **Mailing List**: qemu-devel@nongnu.org

## Quick Reference

### Common Commands

```bash
# Build QEMU
./configure && make -j$(nproc)

# Run x86-64 VM
qemu-system-x86_64 -m 2G -cdrom image.iso

# Create disk image
qemu-img create -f qcow2 disk.qcow2 20G

# Run with KVM acceleration
qemu-system-x86_64 -enable-kvm -m 4G disk.img

# User mode (run ARM binary on x86)
qemu-arm ./arm-binary

# Monitor commands
info qtree        # Device tree
info mtree        # Memory tree
info qom-tree     # QOM composition tree
```

### Important Files to Know

- `MAINTAINERS`: Subsystem maintainers and file ownership
- `VERSION`: Current QEMU version
- `COPYING`, `LICENSE`: Licensing information
- `README.rst`: Quick start guide
- `docs/`: Comprehensive documentation

---

**Document Version:** 1.0  
**QEMU Version:** 10.1.50  
**Last Updated:** 2025-11-03
