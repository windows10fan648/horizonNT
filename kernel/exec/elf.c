#include <horizon/elf.h>
#include <horizon/fs.h>
#include <horizon/mm.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

static bool validate_user_range(uint64_t vaddr, uint64_t len) {
    if (len == 0) return true;
    if (vaddr < 0x1000ULL) return false;
    if (vaddr >= 0x00007FFFFFFFFFFFULL) return false;
    if (vaddr + len < vaddr) return false;
    if (vaddr + len > 0x00007FFFFFFFFFFFULL) return false;
    return true;
}

NTSTATUS elf_spawn_process(const char *name, const void *image, size_t size, kprocess_t **out_proc) {
    if (!image || size < sizeof(elf64_ehdr_t)) return STATUS_INVALID_IMAGE_FORMAT;

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    uint32_t magic;
    memcpy(&magic, eh->e_ident, sizeof(magic));
    if (magic != ELF_MAGIC || eh->e_ident[4] != 2 || eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(elf64_phdr_t) > size) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    kprocess_t *proc = NULL;
    NTSTATUS st = ps_create_process(name ? name : "userproc", false, &proc);
    if (!NT_SUCCESS(st)) return st;

    const uint8_t *raw = (const uint8_t *)image;
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(raw + eh->e_phoff);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        if (!validate_user_range(ph->p_vaddr, ph->p_memsz) ||
            ph->p_filesz > ph->p_memsz ||
            ph->p_offset + ph->p_filesz > size) {
            proc->exited = true;
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        uint32_t vmm_flags = VMM_USER | VMM_READ;
        if (ph->p_flags & PF_W) vmm_flags |= VMM_WRITE;
        if (ph->p_flags & PF_X) vmm_flags |= VMM_EXEC;

        st = vmm_alloc_user_region(proc->addr_space, ph->p_vaddr, ph->p_memsz, vmm_flags);
        if (!NT_SUCCESS(st)) {
            proc->exited = true;
            return st;
        }

        /* Copy file bytes page-by-page via HHDM without switching CR3 */
        size_t copied = 0;
        while (copied < ph->p_filesz) {
            virt_addr_t cur_v  = ph->p_vaddr + copied;
            size_t page_off    = cur_v & (PAGE_SIZE - 1);
            size_t chunk       = PAGE_SIZE - page_off;
            if (chunk > ph->p_filesz - copied) chunk = ph->p_filesz - copied;

            phys_addr_t phys = vmm_virt_to_phys(proc->addr_space, ALIGN_DOWN(cur_v, PAGE_SIZE));
            KASSERT(phys != 0);
            memcpy((uint8_t *)PHYS_TO_VIRT(phys) + page_off, raw + ph->p_offset + copied, chunk);
            copied += chunk;
        }
    }

    /* Map Ring-3 stack */
    virt_addr_t stack_base = UTHREAD_STACK_TOP - (UTHREAD_STACK_PAGES * PAGE_SIZE);
    st = vmm_alloc_user_region(proc->addr_space, stack_base, UTHREAD_STACK_PAGES * PAGE_SIZE,
                               VMM_USER | VMM_READ | VMM_WRITE);
    if (!NT_SUCCESS(st)) {
        proc->exited = true;
        return st;
    }

    kthread_t *ut = NULL;
    st = ps_create_uthread(proc, "main", eh->e_entry, UTHREAD_STACK_TOP - 16, 2, &ut);
    if (!NT_SUCCESS(st)) {
        proc->exited = true;
        return st;
    }

    if (out_proc) *out_proc = proc;
    return STATUS_SUCCESS;
}

NTSTATUS elf_spawn_from_fs(const char *path, kprocess_t **out_proc) {
    fs_node_t *node = fs_lookup(path);
    if (!node || node->type != FS_NODE_FILE || !node->data) {
        return STATUS_NO_SUCH_FILE;
    }
    return elf_spawn_process(node->name, node->data, node->size, out_proc);
}
