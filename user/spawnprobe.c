#include <horizon/libhorizon.h>

/* Quiet Ring-3 probe used by the repeated-launch regression test. It exercises
 * both syscall entry paths through libHorizon. */
int main(void) {
    uint64_t pid = hnt_getpid();
    uint64_t tid = hnt_int80(HNT_SYS_GET_TID, 0, 0, 0, 0, 0);
    return (pid != 0 && tid != 0) ? 7 : 8;
}
