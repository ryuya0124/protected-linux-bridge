/* MIT. Guest-only polling UART: the address comes from the validated guest ADT.
 * This is the crosvm virtual 16550, never the physical Galaxy UART. No IRQ,
 * host-memory inspection, dynamic sharing or host driver is required.
 */
#if defined(XNU_BRIDGE_UART16550) && XNU_BRIDGE_UART16550
static volatile uint8_t *bridge_uart;

static void bridge_uart_init(void) {
    bridge_uart[1] = 0; /* Polling only; firmware already selected byte-wide registers. */
}

static unsigned int bridge_uart_transmit_ready(void) {
    return bridge_uart[5] & 0x20;
}

static void bridge_uart_transmit_data(uint8_t value) {
    bridge_uart[0] = value;
}

static unsigned int bridge_uart_receive_ready(void) {
    return bridge_uart[5] & 1;
}

static uint8_t bridge_uart_receive_data(void) {
    return bridge_uart[0];
}

static struct pe_serial_functions bridge_uart_functions = {
    .init = bridge_uart_init,
    .transmit_ready = bridge_uart_transmit_ready,
    .transmit_data = bridge_uart_transmit_data,
    .receive_ready = bridge_uart_receive_ready,
    .receive_data = bridge_uart_receive_data,
    .device = SERIAL_BRIDGE_UART16550,
};

static void bridge_uart_register(void) {
    DTEntry node = NULL;
    const uint64_t *region = NULL;
    const char *compatible = NULL;
    uint32_t length = 0;
    if (SecureDTFindEntry("name", "bridge-uart", &node) != kSuccess ||
        SecureDTGetProperty(node, "compatible", (const void **)&compatible, &length) != kSuccess ||
        length != sizeof("ns16550a") || memcmp(compatible, "ns16550a", length) != 0 ||
        SecureDTGetProperty(node, "reg", (const void **)&region, &length) != kSuccess ||
        length != 2 * sizeof(uint64_t)) {
        return;
    }
    /* The ADT carries the absolute VM bus address, not an arm-io offset. */
    uint64_t base = region[0], size = region[1];
    if (base == 0 || size != 8 || base > UINT64_MAX - size) {
        return;
    }
    bridge_uart = (volatile uint8_t *)ml_io_map(base, size);
    if (bridge_uart != NULL) {
        register_serial_functions(&bridge_uart_functions);
    }
}
#endif
