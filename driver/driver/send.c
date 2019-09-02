#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <linux/limits.h>

#include "ixgbe.h"
#include "lib.h"
#include "struct.h"
#include "vfio.h"
#include "stats.h"
#include "init.h"

const int BATCH_SIZE = 32;

int main(int argc,char *argv[])
{
    if(argc != 3){
            printf("Usage: %s <pci bus id 1> <pci bus id 2>\n",argv[0]);
            return -1;
    }
    uint64_t prev_time = monotonic_time();

    struct ixgbe_device *ix_tx = do_ixgbe(argv[1],1,1);

    struct ixgbe_stats ix_stat;

    clear_stats(&ix_stat);
    print_stats(&ix_stat);

    uint16_t i=0;
    while(true){
            struct pkt_buf *rtx_buf[BATCH_SIZE];
            uint32_t tx_b = tx_batch(ix_tx,0,rtx_buf,BATCH_SIZE);
            uint64_t now_time = monotonic_time();
            if(now_time-prev_time > 1000000000){
                read_stats(ix_tx,&ix_stat);
                print_stats(&ix_stat);
                prev_time = now_time;
            }
            i++;
            if(i>100){
                    break;
            }
    }

    return 0;
}

