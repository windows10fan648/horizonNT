#pragma once

#include <horizon/types.h>
#include <horizon/hal.h>
#include <horizon/abi.h>

/* Syscall numbers and the hnt_sysinfo_t layout come from <horizon/abi.h>,
 * which is shared with the libHorizon Ring-3 runtime. */
void syscall_init(void);
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
