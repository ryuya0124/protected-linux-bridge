#include "diagnostic_kernel_monitor.h"
#include <assert.h>
int main(void) {
    assert(!plb_diagnostic_kernel_bad_text("4,1,20,-;ordinary message\n", 0));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=1\n", 0));
    assert(!plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=1\n", 1));
    assert(!plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=0\n", 1));
    assert(!plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt key_thread_write\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=2\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=1 extra\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;BUG:svprt write=1\nOops: fault\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;BUG: Bad page cache\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;Kernel panic\n", 1));
    assert(plb_diagnostic_kernel_bad_text("3,1,20,-;still mapped when deleted\n", 1));
    assert(plb_diagnostic_kernel_bad_text(NULL, 1));
}
