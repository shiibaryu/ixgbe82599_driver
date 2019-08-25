#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <linux/limits.h>
#include <linux/vfio.h>
#include <sys/mman.h>

const int TX_CLEAN_BATCH = 32;
uint32_t path_id = 1;
uintptr_t vtop(uintptr_t vaddr)
{
    FILE *pagemap;
    uintptr_t paddr = 0;
    int offset = (vaddr / sysconf(_SC_PAGESIZE)) * sizeof(uint64_t);
    uint64_t e;
    
    if ((pagemap = fopen("/proc/self/pagemap", "r"))) {
        if (lseek(fileno(pagemap), offset, SEEK_SET) == offset) {
            if (fread(&e, sizeof(uint64_t), 1, pagemap)) {
                if (e & (1ULL << 63)) { // page present ?
                    paddr = e & ((1ULL << 54) - 1); // pfn mask
                    paddr = paddr * sysconf(_SC_PAGESIZE);
                    // add offset within page
                    paddr = paddr | (vaddr & (sysconf(_SC_PAGESIZE) - 1));
                }   
            }   
        }   
        fclose(pagemap);
    }   
    return paddr;
}   

struct dma_address allocate_dma_address(uint32_t ring_size)
{
    char path[PATH_MAX];
    uint32_t this_id;

   //ringサイズ分のページをmap
   //まずはメモリ確保のためのファイルへのアクセスを得るため、ファイルディスクリプターをもらう
    this_id = __sync_fetch_and_add(&path_id,1);
    snprintf(path,PATH_MAX,"/mnt/huge/ixgbe-%d-%d",this_id,getpid());
    int fd = open(path,O_CREAT|O_RDWR,S_IRWXU);
    //ring_size以上は切り捨てる
    ftruncate(fd,(off_t)ring_size);
   //仮想アドレスが帰ってくるのでそれを物理にして、dma_address
   //に格納してreturn
   //ここで仮想アドレスget
   uintptr_t virt_addr = (uintptr_t)mmap(NULL,ring_size,PROT_READ|PROT_WRITE,MAP_SHARED | MAP_HUGETLB,fd,0);

   close(fd);
   unlink(path);

   return(struct dma_address){
           .virt_addr = (void *)virt_addr,
           .phy_addr =  vtop(virt_addr)
   };
}

/*ディスクリプターにパケットが届いた時にそれを格納するためのメモリープール*/
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size)
{
    struct dma_address dma_addr;
    entry_size = entry_size ? entry_size : 2048;

    struct mempool *mempool = (struct mempool*)malloc(sizeof(struct mempool) + num_entries * sizeof(uint32_t));
    
    dma_addr = allocate_dma_address(num_entries*entry_size);
    mempool->num_entries = num_entries;
    mempool->num_entries = entry_size;
    mempool->base_addr   = dma_addr.virt_addr;
    mempool->free_stack_top = num_entries;

    for(uint32_t i=0;i<num_entries;i++){
        mempool->free_stack[i] = i;
        struct pkt_buf *buf = (struct pkt_buf *)(((uint8_t *)mempool->base_addr) + i * entry_size);
        buf->buf_addr_phy = vtop((uintptr_t)buf);
        buf->mempool_idx = i;
        buf->mempool = mempool;
        buf->size = 0;
    }
    
    return mempool;
}

uint32_t alloc_pkt_buf_batch(struct mempool *mempool,struct pkt_buf *buf[],uint32_t num_bufs)
{
   if(mempool->free_stack_top < num_bufs){
           printf("memory pool %p only has %d free bufs, requested %d",mempool,mempool->buf_size,num_bufs);
           num_bufs = mempool->free_stack_top;
   }
    for(uint32_t i=0;i<num_bufs;i++){
            uint32_t entry_id = mempool->free_stack[--mempool->free_stack_top];
            buf[i] = (struct pkt_buf*)(((uint8_t*)mempool->base_addr) + entry_id * mempool->buf_size);
    }
    return num_bufs;
}

struct pkt_buf *alloc_pkt_buf(struct mempool *mempool)
{
    struct pkt_buf *pb = NULL;
    alloc_pkt_buf_batch(mempool,&pb,1);
    return pb;
}


void pkt_buf_free(struct pkt_buf *buf)
{
    struct mempool *mempool = buf->mempool;
    mempool->free_stack[mempool->free_stack_top++] = buf->mempool_idx;
}

#define wrap_ring(index,ring_size) (uint16_t)((index+1)&(ring_size-1))

uint32_t rx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf)
{
    //datasheet 7.1
    //パケットの確認
    //addressのフィルタリング(今回はなし)
    //DMA queue assignment
    //paketをstore
    //ホストメモリーのreceive queueにデータをおくつ
    //receive descriptorの状態更新(RDH,RDTを動かしたりとか)

    struct rx_queue *rxq = ((struct rx_queue *)(ix_dev->rx_queues)) + queue_id;
    uint16_t rx_index = rxq->rx_index;
    uint16_t prev_rx_index = rxq->rx_index;
    uint32_t i;
    for(i=0;i<num_buf;i++){
            volatile union ixgbe_adv_rx_desc *rxd = rxq->descriptors + rx_index;
            if(rxd->wb.upper.status_error & IXGBE_RXDADV_STAT_DD){
                    if(!(rxd->wb.upper.status_error & IXGBE_RXDADV_STAT_EOP)){
                            printf("multi_segment packt not supported!");
                    }
                union ixgbe_adv_rx_desc desc = *rxd;
                struct pkt_buf *buf = (struct pkt_buf *)rxq->virtual_address[rx_index];
                buf->size = desc.wb.upper.length;

                //そのポインタをread.pkt_addrに登録
                struct pkt_buf *p_buf = alloc_pkt_buf(rxq->mempool);
                if(!p_buf){
                    printf("failed to allocate pkt_buf");
                    return -1;
                }
            
                rxd->read.pkt_addr = p_buf->buf_addr_phy + offsetof(struct pkt_buf,data);
                rxd->read.hdr_addr = 0;
                rxq->virtual_address[rx_index] = p_buf;
                bufs[i]=buf;
                prev_rx_index = rx_index;
                rx_index = wrap_ring(rx_index,rxq->num_entries);
            }
            else{break;}
    }
    if(rx_index != prev_rx_index){
            set_reg32(ix_dev->addr,IXGBE_RDT(queue_id),prev_rx_index);
            rxq->rx_index = rx_index;
    }
    return i;
}

uint32_t tx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_bufs)
{
    struct tx_queue *txq = ((struct tx_queue*)(ix_dev->tx_queues)) + queue_id;
    uint16_t tx_index = txq->tx_index;
    uint16_t clean_index = txq->clean_index;

    while(true){
            int32_t cleanable = tx_index - clean_index;
            if(cleanable < 0){
                    cleanable = txq->num_entries + cleanable;
            }
            if(cleanable < TX_CLEAN_BATCH){
                    break;
            }
            int cleanup_to = clean_index + TX_CLEAN_BATCH - 1;
            if(cleanup_to >= txq->num_entries){
                    cleanup_to -= txq->num_entries;
            }
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + cleanup_to;
            uint32_t status = txd->wb.status;
            if(status & IXGBE_ADVTXD_STAT_DD){
                    int32_t i = clean_index;
                    while(true){
                            struct pkt_buf *buf = txq->virtual_address[i];
                            pkt_buf_free(buf);
                            if(i==cleanup_to){
                                    break;
                            }
                            i = wrap_ring(i,txq->num_entries);
                    }
                    clean_index = wrap_ring(cleanup_to,txq->num_entries);
            }
            else{break;}
    }
    txq->clean_index = clean_index;
    uint32_t sent;
    for(sent=0;sent<num_bufs;sent++){
            uint32_t next_index = wrap_ring(tx_index,txq->num_entries);
            if(clean_index == next_index){
                    break;
            }
            struct pkt_buf *buf = bufs[sent];
            txq->virtual_address[tx_index] = (void *)buf;
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + tx_index;
            txq->tx_index = next_index;
            txd->read.buffer_addr = buf->buf_addr_phy + offsetof(struct pkt_buf,data);
            txd->read.cmd_type_len = IXGBE_ADVTXD_DCMD_EOP | IXGBE_ADVTXD_DCMD_RS | IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_DATA | buf->size;
            txd->read.olinfo_status = buf->size >> IXGBE_ADVTXD_PAYLEN_SHIFT;
    }

    set_reg32(ix_dev->addr,IXGBE_TDT(queue_id),tx_index);

    return sent;
}
