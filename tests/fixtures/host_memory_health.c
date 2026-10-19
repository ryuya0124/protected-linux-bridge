/* MIT. Kernel health is parsed only; no host settings are read or changed. */
#define _GNU_SOURCE
#include "host_memory_health.h"
#include <assert.h>

int main(void) {
    assert(plb_host_memory_healthy_text("0\n"));
    assert(plb_host_memory_healthy_text("4608\n"));  /* expected O/W taints */
    assert(!plb_host_memory_healthy_text("4640\n")); /* BAD_PAGE */
    assert(!plb_host_memory_healthy_text("128\n"));  /* OOPS */
    assert(!plb_host_memory_healthy_text(NULL));
    assert(!plb_host_memory_healthy_text(""));
    assert(!plb_host_memory_healthy_text("-1"));
    assert(!plb_host_memory_healthy_text("0\nextra"));
    assert(!plb_host_memory_healthy_text("184467440737095516160"));
    return 0;
}
