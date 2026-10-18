#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#define BASE "/unused"
#include "stream_relay.h"
#include "resource_settings.h"
int main(void) {
    char input[256];
    size_t n = fread(input, 1, sizeof(input), stdin);
    struct resource_settings r;
    return resources_parse(input, n, &r) ? 1 : 0;
}
