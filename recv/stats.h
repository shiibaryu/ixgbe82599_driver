void clear_stats(struct ixgbe_stats *ix_stats);
void read_stats(struct ixgbe_device *ix_dev,struct ixgbe_stats *ix_stats);
void print_tx_stats(struct ixgbe_stats *ix_stats);
void print_rx_stats(struct ixgbe_stats *ix_stats);
uint64_t monotonic_time();

