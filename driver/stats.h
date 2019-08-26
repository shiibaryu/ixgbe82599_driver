void clear_stats(struct ixgbe_stats *ix_stats);
void read_stats(struct ixgbe_device *ix_dev,struct ixgbe_stats *ix_stats);
void print_stats(struct ixgbe_stats *ix_stats);
uint64_t monotonic_time();
