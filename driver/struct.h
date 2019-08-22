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

struct pkt_buf{
   struct mempool *mempool;
   uintptr_t buf_addr_phy;
   uint32_t  mempool_idx;
   uint32_t  size;
   uint8_t   head_room[SIZE_PKT_BUF_HEADROOM];
   uint8_t   data[] __attribute__((aligned(64)));
}

struct mempool{
    void *base_addr;
    uint32_t buf_size;
    uint32_t num_entries;
    uint32_t free_stack_top;
    uint32_t free_stack[];
}

