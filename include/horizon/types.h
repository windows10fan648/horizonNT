#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#include <horizon/abi.h>

typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

typedef int32_t  NTSTATUS;
typedef uint64_t HANDLE;
typedef uint32_t ACCESS_MASK;

#define INVALID_HANDLE_VALUE ((HANDLE)-1)

#define PAGE_SIZE  4096ULL
#define PAGE_SHIFT 12

#define HHDM_BASE        0xFFFF800000000000ULL
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_BASE 0x0000000000100000ULL

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((uint64_t)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))

#define PHYS_TO_VIRT(p)  ((void *)((uint64_t)(p) + HHDM_BASE))
#define VIRT_TO_PHYS(v)  ((phys_addr_t)((uint64_t)(v) - HHDM_BASE))
#define KVIRT_TO_PHYS(v) ((phys_addr_t)((uint64_t)(v) - KERNEL_VIRT_BASE))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define UNUSED(x)     ((void)(x))

/* Generic access rights (NT-inspired), shared with libHorizon. */
#define GENERIC_READ    HNT_ACCESS_READ
#define GENERIC_WRITE   HNT_ACCESS_WRITE
#define GENERIC_EXECUTE HNT_ACCESS_EXECUTE
#define GENERIC_ALL     HNT_ACCESS_ALL

#define HNT_PACKED   __attribute__((packed))
#define HNT_ALIGNED(x) __attribute__((aligned(x)))
#define HNT_NORETURN __attribute__((noreturn))
