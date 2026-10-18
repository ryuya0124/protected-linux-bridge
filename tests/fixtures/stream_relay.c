#include <stdlib.h>
#include "stream_relay.h"
int main(int n, char **v) {
    if (n != 5)
        return 2;
    plb_relay(atoi(v[1]), atoi(v[2]), atoi(v[3]), atoi(v[4]));
    return 0;
}
