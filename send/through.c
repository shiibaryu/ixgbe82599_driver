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
#include <time.h>
#include <signal.h>

#include "ixgbe.h"
#include "lib.h"
#include "struct.h"
#include "vfio.h"
#include "stats.h"
#include "init.h"

#define PKT_SIZE 1210

int BATCH_SIZE; 

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
struct ixgbe_device *ix_tx;
struct ixgbe_stats stats,prev_stats;

void get_stats(int signum)
{	
	read_stats(ix_tx,&stats);
	print_tx_stats(&stats);
	exit(1);
}

int main(int argc,char *argv[])
{
	if(argc != 3){
		printf("Usage: %s <pci bus id>\n",argv[0]);
            	return -1;
    	}
	//struct ixgbe_device *ix_tx;
	//struct ixgbe_stats stats,prev_stats;
	BATCH_SIZE = atoi(argv[2]);
	struct mempool *memp = init_mempool();
	//struct mempool *memp2 = init_mempool();
	struct sigaction act,oldact;
	timer_t tid;
	struct itimerspec itval;
    	memset(&oldact, 0, sizeof(struct sigaction));
	memset(&act,0,sizeof(struct sigaction));
	act.sa_handler = get_stats;
	act.sa_flags = SA_RESTART;
	if(sigaction(SIGALRM, &act, &oldact) < 0) {
        	perror("sigaction()");
        	return -1;
    	}
	itval.it_value.tv_sec = 10;    
   	itval.it_value.tv_nsec = 0;
	itval.it_interval.tv_sec = 10;
    	itval.it_interval.tv_nsec = 0;
    	
	ix_tx = do_ixgbe(argv[1],2,2);


	struct pkt_buf *buf[BATCH_SIZE];
	//struct pkt_buf *buf2[BATCH_SIZE];
	alloc_pkt_buf_batch(memp,buf,BATCH_SIZE);
	//alloc_pkt_buf_batch(memp2,buf2,BATCH_SIZE);
	uint64_t seq_num=0;
    	clear_stats(&stats);
	clear_stats(&prev_stats);
	sleep(1);
	if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) {
        	perror("timer_create");
        	return -1;
    	}
 
    	if(timer_settime(tid, 0, &itval, NULL) < 0) {
        	perror("timer_settime");
        	return -1;
    	}
	while(true){
		//receiveの準備
		//transmitの準備
		//statの更新
		//for(uint32_t i=0;i<BATCH_SIZE;i++){
		//	*(uint32_t*)(buf[i]->data + PKT_SIZE - 4) = seq_num++;
		//}
				
		alloc_pkt_buf_batch(memp,buf,BATCH_SIZE);
		inline_tx_batch(ix_tx,0,buf,BATCH_SIZE);
		//alloc_pkt_buf_batch(memp,buf,BATCH_SIZE);
		//sleep(1);	
		//alloc_pkt_buf_batch(memp2,buf2,BATCH_SIZE);
		//inline_tx_batch(ix_tx,1,buf2,BATCH_SIZE);

		sleep(0.18);	

	}
	timer_delete(tid);
    	sigaction(SIGALRM, &oldact, NULL);
	return 0;
}

