#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "ixgbe.h"
#include "lib.h"
#include "struct.h"
#include "stats.h"

void clear_stats(struct ixgbe_stats *ix_stats)
{
    ix_stats->rx_pkts_num = 0;
    ix_stats->tx_pkts_num = 0;
    ix_stats->rx_bytes = 0;
    ix_stats->tx_bytes = 0;
}

void read_stats(struct ixgbe_device *ix_dev,struct ixgbe_stats *ix_stats)
{
    uint32_t rx_pkts = get_reg32(ix_dev->addr,IXGBE_GPRC);
    uint32_t tx_pkts = get_reg32(ix_dev->addr,IXGBE_GPTC);
    uint64_t rx_bytes = get_reg32(ix_dev->addr,IXGBE_GORCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GORCH)) << 32);
    uint64_t tx_bytes = get_reg32(ix_dev->addr,IXGBE_GOTCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GOTCH)) << 32);
    if(ix_stats){
            ix_stats->rx_pkts_num += rx_pkts;
            ix_stats->tx_pkts_num += tx_pkts;
            ix_stats->rx_bytes += rx_bytes;
            ix_stats->tx_bytes += tx_bytes;
    }
}

void print_stats(struct ixgbe_stats *ix_stats)
{
    //printf("RX pkt: %d\n",ix_stats->rx_pkts_num);
    //printf("%ld bytes\n",ix_stats->rx_bytes);

    printf("TX pkt: %d\n",ix_stats->tx_pkts_num);
    printf("%ld bytes\n",ix_stats->tx_bytes);
}

uint64_t monotonic_time(){
    struct timespec timespec;
    clock_gettime(CLOCK_MONOTONIC,&timespec);
    return timespec.tv_sec * 1000 * 1000 * 1000 + timespec.tv_nsec;
}

