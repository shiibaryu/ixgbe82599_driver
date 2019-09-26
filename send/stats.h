void clear_stats(struct ixgbe_stats *ix_stats);
void read_stats(struct ixgbe_device *ix_dev,struct ixgbe_stats *ix_stats);
void print_tx_stats(struct ixgbe_stats *ix_stats);
void print_rx_stats(struct ixgbe_stats *ix_stats);
static inline uint64_t monotonic_time(){
    struct timespec timespec;
    clock_gettime(CLOCK_MONOTONIC,&timespec);
    return timespec.tv_sec * 1000 * 1000 * 1000 + timespec.tv_nsec;
}
