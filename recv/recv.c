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

#define PKT_SIZE 60

const int BATCH_SIZE = 64;

int main(int argc,char *argv[])
{
	if(argc != 2){
		printf("Usage: %s <pci bus id 1> <pci bus id 2>\n",argv[0]);
            	return -1;
    	}
    	uint64_t prev_time = monotonic_time();
	uint64_t now_time;

    	//初期化全部
    	struct ixgbe_device *ix_rx = do_ixgbe(argv[1],1,1);

    	struct ixgbe_stats stats,prev_stats;

    	clear_stats(&stats);
	clear_stats(&prev_stats);

	uint32_t seq_num = 0;
	struct pkt_buf *buf[BATCH_SIZE];
	uint32_t counter=0;

	while(true){
		sleep(1);
		uint32_t num_rx = rx_batch(ix_rx,30,buf,BATCH_SIZE);	

		now_time = monotonic_time();
		if(now_time - prev_time > 1000*1000*1000){	
			read_stats(ix_rx,&stats);
			print_rx_stats(&stats);
		}
	}
	return 0;
}

