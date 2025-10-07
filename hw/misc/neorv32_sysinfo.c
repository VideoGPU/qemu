#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "system/address-spaces.h"
#include "neorv32_sysinfo.h" /* QEMU related */
#include "neorv32_sysinfo_rtl.h" /* RTL related */

#define SYSINFO_SOC_ENABLE(x) (1U << (x))
// Enable UART and SPI
#define SYSINFO_SOC_VAL \
			(SYSINFO_SOC_ENABLE(SYSINFO_SOC_IO_UART0) | \
			 SYSINFO_SOC_ENABLE(SYSINFO_SOC_IO_SPI))




/* Register addresses (offsets) */
enum {
    REG_SYSINFO_CLK   = 0,
    REG_SYSINFO_MEM   = 4,
    REG_SYSINFO_SOC   = 8,
    REG_SYSINFO_CACHE = 12,
};


struct Neorv32SysInfoState {
    MemoryRegion mmio;
    uint32_t CLK;
    uint8_t  MEM[4];
    uint32_t SOC;
    uint32_t CACHE;
} __attribute__((packed, aligned(4)));


/* Integer log2 */
static unsigned int neorv32_log2(unsigned int x) {
    unsigned int result = 0;
    while (x >>= 1) {
        result++;
    }
    return result;
}

static void neorv32_sysinfo_init(struct Neorv32SysInfoState *s)
{
    /* Initialize the read-only registers */
    s->CLK = cpu_to_le32(SYSINFO_CLK_HZ);
    s->MEM[0] = neorv32_log2(SYSINFO_IMEM_SIZE);
    s->MEM[1] = neorv32_log2(SYSINFO_DMEM_SIZE);
    s->MEM[2] = 0x0;
    s->MEM[3] = neorv32_log2(SYSINFO_RVSG_SIZE);
    s->SOC = cpu_to_le32(SYSINFO_SOC_VAL);
    s->CACHE = cpu_to_le32(SYSINFO_CACHE_VAL);
}

static uint64_t neorv32_sysinfo_read(void *opaque, hwaddr addr, unsigned size)
{
    struct Neorv32SysInfoState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(*s) - sizeof(MemoryRegion)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read at addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    uint8_t *reg_ptr = ((uint8_t *)s) + addr + sizeof(MemoryRegion);
    memcpy(&val, reg_ptr, size);

    return val;
}

static void neorv32_sysinfo_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Since the registers are read-only, log an error on write attempts */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write at addr=0x%" HWADDR_PRIx ", val=0x%" PRIx64 "\n",
                  __func__, addr, val);
}

static const MemoryRegionOps neorv32_sysinfo_ops = {
    .read = neorv32_sysinfo_read,
    .write = neorv32_sysinfo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

void neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base)
{
    struct Neorv32SysInfoState *s = g_new0(struct Neorv32SysInfoState, 1);

    /* Initialize the fields */
    neorv32_sysinfo_init(s);

    /* Initialize the MemoryRegion */
    memory_region_init_io(&s->mmio, NULL, &neorv32_sysinfo_ops,
                          s, "neorv32.sysinfo", sizeof(*s) - sizeof(MemoryRegion));

    /* Map the MemoryRegion into the address space */
    memory_region_add_subregion(address_space, base, &s->mmio);
}
