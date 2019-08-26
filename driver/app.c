/*
start_ixgbe()
これによって、初期化とvfioのマップとかもできる
そのあとパケット送信のtx・rx batch関数でパケット送信・受信
*/
#include <stddef.h>
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
            printf("Usage: %s <pci bus id2> <pci bus id1>\n",argv[0]);
            return -1;
    }
    uint64_t prev_time = monotonic_time();

    //初期化全部
    struct ixgbe_device *ix_rx = do_ixgbe(argv[1],1,1);
    struct ixgbe_device *ix_tx = do_ixgbe(argv[2],1,1);

    struct ixgbe_stats ix_stat;
    //struct ixgbe_stats ix_stat2;

    clear_stats(&ix_stat);
    print_stats(&ix_stat);

    //init_stats(&ix_stat2);
    //print_stats(&ix_stat2);
    uint16_t i=0;
    while(true){
            struct pkt_buf *rtx_buf[BATCH_SIZE];
            uint32_t rx_b = rx_batch(ix_rx,0,rtx_buf,BATCH_SIZE);
            rtx_buf[0]->data[1]++;
            uint32_t tx_b = tx_batch(ix_tx,0,rtx_buf,rx_b);

            for(uint32_t i=tx_b;i<rx_b;i++){
                    pkt_buf_free(rtx_buf[i]);
            }

            uint64_t now_time = monotonic_time();
            if(now_time-prev_time > 1000000000){
                read_stats(ix_rx,&ix_stat);
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

