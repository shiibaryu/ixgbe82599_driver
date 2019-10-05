struct ixgbe_device *do_ixgbe(const char *pci_addr,uint16_t rx_queue,uint16_t tx_queue);
uint32_t rx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf);
void tx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf);
