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

struct dma_address{
    uintptr_t virt_addr;
    uintptr_t phy_addr;
}
