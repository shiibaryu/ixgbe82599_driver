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

static const uint8_t pkt_data[] = {
	0x01,0x02,0x03,0x04,0x05,0x06,
	0x07,0x08,0x09,0x10,0x11,0x12,
	0x08,0x13,
	0x45,0x00,
	(PKT_SIZE - 14) >> 8,
	(PKT_SIZE - 14) & 0xFF,
	0x00,0x00,0x00,0x00,
	0x40,0x11,0x00,0x00,
	0x0A,0x00,0x00,0x01,
	0x0A,0x00,0x00,0x02,
	0x00,0x2A,0x05,0x39,
	(PKT_SIZE - 20 - 14) >> 8,
	(PKT_SIZE - 20 - 14) & 0xFF,
	0x00,0x00,
	's','i','i','b'
};

static uint16_t calc_ip_checksum(uint8_t *data,uint32_t len)
{
	uint32_t cs = 0;
	for(uint32_t i=0;i<len/2;i++){
		cs += ((uint16_t*)data)[i];
		if(cs > 0xFFFF){
			cs = (cs & 0xFFFF) + 1;
		}
	}
	return ~((uint16_t)cs);
}

static struct mempool *init_mempool()
{
	const int NUM_BUFS = 2048;
	struct mempool *mempool = allocate_mempool_mem(NUM_BUFS,0);
	struct pkt_buf *bufs[NUM_BUFS];
	
	for(int id=0;id < NUM_BUFS;id++){
		struct pkt_buf *buf = alloc_pkt_buf(mempool);
		buf->size = PKT_SIZE;
		memcpy(buf->data,pkt_data,sizeof(pkt_data));
		*(uint16_t*)(buf->data + 24) = calc_ip_checksum(buf->data + 14,20);
		bufs[id] = buf;
	}
	for(int id=0;id<NUM_BUFS;id++){
		pkt_buf_free(bufs[id]);
	}
	return mempool;
}

int main(int argc,char *argv[])
{
	if(argc != 2){
		printf("Usage: %s <pci bus id 1> <pci bus id 2>\n",argv[0]);
            	return -1;
    	}
	struct mempool *memp = init_mempool();
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

