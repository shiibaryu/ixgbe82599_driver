struct ixgbe_device{
    uint8_t *addr;
    void *tx_queues;
    void *rx_queues;
    char *pci_addr;
    char *driver_name;
    int device_id;
    uint16_t num_rx_queues;
    uint16_t num_rx_queues;
}

struct packet_buffer{
    uint64_t *data_buffer;
    uint16_t *length;
}
