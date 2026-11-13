#include <stdint.h>
#include <stddef.h>

extern uint64_t hnt_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint64_t hnt_int80(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

#define SYS_GET_PID 7
#define SYS_GET_TID 8

/* Quiet Ring-3 probe used by the repeated-launch regression test. */
int main(void) {
    uint64_t pid = hnt_syscall(SYS_GET_PID, 0, 0, 0, 0, 0);
    uint64_t tid = hnt_int80(SYS_GET_TID, 0, 0, 0, 0, 0);
    return (pid != 0 && tid != 0) ? 7 : 8;
}
