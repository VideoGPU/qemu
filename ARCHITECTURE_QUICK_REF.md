# QEMU Architecture Quick Reference

This is a condensed reference for quick lookups. For comprehensive details, see `ARCHITECTURE.md`.

## What is QEMU?

QEMU = Quick Emulator: A versatile machine emulator and virtualizer

**Version:** 10.1.50

## Core Modes

1. **System Mode** (`qemu-system-*`): Full machine emulation
2. **User Mode** (`qemu-*`): Run binaries for different architectures

## Key Directories

| Dir | What |
|-----|------|
| `target/` | CPU architectures (21 supported) |
| `hw/` | Hardware devices (70+ categories) |
| `accel/` | Accelerators (TCG, KVM, HVF, etc.) |
| `block/` | Disk images and block devices |
| `tcg/` | Binary translator |
| `qom/` | Object model |
| `docs/` | Documentation |
| `tests/` | Test suite |

## Major Subsystems

1. **TCG**: Dynamic binary translation
2. **QOM**: Object model and type system
3. **Device Emulation**: Hardware simulation
4. **Block Layer**: Storage and disk images
5. **Migration**: Live VM migration
6. **QAPI**: Management API (QMP/HMP)
7. **Memory**: Address space management
8. **Tracing**: Performance debugging

## Supported Architectures

x86, ARM, RISC-V, PowerPC, MIPS, S390x, SPARC, Alpha, HPPA, M68K, LoongArch, Xtensa, Hexagon, AVR, MicroBlaze, OpenRISC, RX, SH4, TriCore, and more.

## Build System

- **Config**: `./configure` (shell script)
- **Build**: Meson + Make
- **Rust**: Optional (requires rustc >= 1.83.0)

## Common Commands

```bash
# Build
./configure && make -j$(nproc)

# Run VM
qemu-system-x86_64 -m 2G -cdrom image.iso

# Create disk
qemu-img create -f qcow2 disk.qcow2 20G

# User mode
qemu-arm ./arm-binary

# Monitor
info qtree      # Device tree
info mtree      # Memory tree
info qom-tree   # Object tree
```

## Testing

- `tests/unit/` - Unit tests
- `tests/qtest/` - Device tests
- `tests/tcg/` - CPU instruction tests
- `tests/functional/` - Full VM tests
- `tests/qemu-iotests/` - Block layer tests

## Resources

- **Docs**: `docs/` directory, https://www.qemu.org/documentation/
- **Architecture**: `ARCHITECTURE.md` (detailed)
- **Maintenance**: `.github/ARCHITECTURE_MAINTENANCE.md`
- **Maintainers**: `MAINTAINERS` file
- **Mailing List**: qemu-devel@nongnu.org
- **Source**: https://gitlab.com/qemu-project/qemu

## Architecture Layers

```
User Interfaces (Monitor, GDB, QAPI)
         ↓
  Device Emulation (hw/)
         ↓
    Object Model (QOM)
         ↓
  CPU Emulation (target/)
         ↓
   Acceleration (accel/)
         ↓
Support Infrastructure (block, net, migration)
```

## File Size Reference

- `block.c`: 256K lines
- `blockdev.c`: 114K lines
- `qemu-img.c`: 187K lines
- Total C/H files: Millions of lines

## Contributing

1. Subscribe to qemu-devel@nongnu.org
2. Follow `docs/devel/submitting-a-patch.rst`
3. Use `git format-patch` and `git send-email`
4. Include `Signed-off-by` line

---

**For detailed information, see:** `ARCHITECTURE.md`  
**Last Updated:** 2025-11-03
