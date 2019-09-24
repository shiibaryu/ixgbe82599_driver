struct pkt_buf{
   struct mempool *mempool;
   uintptr_t buf_addr_phy;
   uint32_t  mempool_idx;
   uint32_t  size;
   uint8_t   data[] __attribute__((aligned(64)));
};

struct ixgbe_device{
    uint8_t *addr;
    void *tx_queues;
    void *rx_queues;
    const char *pci_addr;
    int device_id;
    uint16_t num_rx_queues;
    uint16_t num_tx_queues;
    int vfio_fd;  
    bool vfio;
    u64 iovamask;
};

struct dma_address{
    void* virt_addr;
    uintptr_t phy_addr;
};

struct mempool{
    void *base_addr;
    uint32_t buf_size;
    uint32_t num_entries;
    uint32_t free_stack_top;
    uint32_t free_stack[];
};

struct tx_queue{
    volatile union ixgbe_adv_tx_desc *descriptors;
    struct mempool *mempool;
    uint16_t num_entries;
    uint16_t clean_index;
    uint16_t tx_index;
    void *virtual_address[];
};

struct rx_queue{
    volatile union ixgbe_adv_rx_desc *descriptors;
    struct mempool *mempool;
    uint16_t num_entries;
    uint16_t rx_index;
    void *virtual_address[];
};

struct ixgbe_stats{
        //GPRCに何パケットきているか
        uint16_t rx_pkts_num;
        uint16_t tx_pkts_num;
        uint64_t rx_bytes;
        uint64_t tx_bytes;
};
