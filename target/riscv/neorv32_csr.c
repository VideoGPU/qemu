/*
 * Neorv32-specific CSR.
 *
 * Copyright (c) 2024 Michael Levit
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_vendorid.h"

#define    CSR_MXISA    (0xfc0)

static riscv_csr_operations csr_ops_xisa;

static RISCVException smode(CPURISCVState *env, int csrno)
{
	return RISCV_EXCP_NONE;
}

static RISCVException read_neorv32_xisa(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
	/* We don't support any extension for now on QEMU */
    *val = 0x00;
    return RISCV_EXCP_NONE;
}

void neorv32_register_xisa_csr(RISCVCPU *cpu)
{
	csr_ops_xisa.name = "neorv32.xisa";
	csr_ops_xisa.predicate = smode;
	csr_ops_xisa.read = read_neorv32_xisa;
	riscv_set_csr_ops(CSR_MXISA, &csr_ops_xisa);
}
