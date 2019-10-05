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

#define BATCH_SIZE 68 

#define wrap_ring(index,ring_size) (uint16_t)((index+1)&(ring_size-1))
const int CLEAN_BATCH = 64;

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

static struct mempool *init_mempool(int size)
{
	const int NUM_BUFS = 2048;
	struct mempool *mempool = allocate_mempool_mem(NUM_BUFS,0);
	struct pkt_buf *bufs[NUM_BUFS];
	
	for(int id=0;id < NUM_BUFS;id++){
		struct pkt_buf *buf = alloc_pkt_buf(mempool);
		buf->size = size;
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
static void buf_free(struct pkt_buf *buf)
{
    struct mempool *mempool = buf->mempool;
    mempool->free_stack[mempool->free_stack_top++] = buf->mempool_idx;
}

static void transmit(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_bufs)
{
    struct tx_queue *txq = ((struct tx_queue*)(ix_dev->tx_queues)) + queue_id;
    //uint16_t tx_index = txq->tx_index;
    uint16_t clean_index = txq->clean_index;

    while(true){
            int64_t cleanable = txq->tx_index - clean_index;
            if(cleanable < 0){
                    cleanable = txq->num_entries + cleanable;
            }
            if(cleanable < CLEAN_BATCH){
                    break;
            }
            int cleanup_to = clean_index + CLEAN_BATCH - 1;
            if(cleanup_to >= txq->num_entries){
                    cleanup_to -= txq->num_entries;
            }
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + cleanup_to;
            uint32_t status = txd->wb.status;
            if(status & IXGBE_ADVTXD_STAT_DD){
                    //int32_t i = clean_index;
                    while(true){
                            struct pkt_buf *buf = txq->virtual_address[clean_index];
                           // pkt_buf_free(buf);
                            buf_free(buf);
                            if(clean_index==cleanup_to){
                                    break;
                            }
                            clean_index = wrap_ring(clean_index,txq->num_entries);
                    }
                    clean_index = wrap_ring(cleanup_to,txq->num_entries);
            }
            else{break;}
    }
    txq->clean_index = clean_index;
    uint32_t sent;
    for(sent=0;sent<num_bufs;sent++){
            uint32_t next_index = wrap_ring(txq->tx_index,txq->num_entries);
            if(clean_index == next_index){
                    break;
            }
            struct pkt_buf *buf = bufs[sent];
            txq->virtual_address[txq->tx_index] = (void *)buf;
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + txq->tx_index;
            txq->tx_index = next_index;
            txd->read.buffer_addr = buf->buf_addr_phy + offsetof(struct pkt_buf,data);
            txd->read.cmd_type_len = IXGBE_ADVTXD_DCMD_EOP | IXGBE_ADVTXD_DCMD_RS | IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_DATA | buf->size;
            txd->read.olinfo_status = buf->size << IXGBE_ADVTXD_PAYLEN_SHIFT;
    }
    set_reg32(ix_dev->addr,IXGBE_TDT(queue_id),txq->tx_index);
}

int main(int argc,char *argv[])
{
	if(argc != 3){
		printf("Usage: %s <pci bus id>\n",argv[0]);
            	return -1;
    	}
	int size = atoi(argv[2]);
	struct mempool *memp = init_mempool(size);
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
	alloc_pkt_buf_batch(memp,buf,BATCH_SIZE);

	uint64_t seq_num=0;
    	clear_stats(&stats);
	clear_stats(&prev_stats);
	if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) {
        	perror("timer_create");
        	return -1;
    	}
 
    	if(timer_settime(tid, 0, &itval, NULL) < 0) {
        	perror("timer_settime");
        	return -1;
    	}
	while(true){
		//inline_tx_batch(ix_tx,0,buf,BATCH_SIZE);
		transmit(ix_tx,0,buf,BATCH_SIZE);
		alloc_pkt_buf_batch(memp,buf,BATCH_SIZE);
	}
	timer_delete(tid);
    	sigaction(SIGALRM, &oldact, NULL);
	return 0;
}

