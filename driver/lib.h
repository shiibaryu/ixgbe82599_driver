#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

#define NUM_RX_QUEUES 512
#define NUM_TX_QUEUES 512
#define MAX_QUEUES 64
#define SIZE_PKT_BUF_HEADROOM 40

static inline void set_reg32(uint8_t *addr,int reg,uint32_t value){
    __asm__ volatile ("" : : : "memory");
    *((volatile uint32_t *)(addr+reg)) = value;
}

static inline uint32_t get_reg32(uint8_t *addr,int reg){
    __asm__ volatile ("" : : : "memory");
    return *((volatile uint32_t *)(addr + reg));
}

static inline uint32_t wait_enable_reset(uint8_t *addr,int reg,uint32_t mask){
    __asm__ volatile ("" : : : "memory");
    uint32_t ret=0;
    while(ret = *((volatile uint32_t *)(addr+reg)),(ret & mask) != 0){
            usleep(10000);
            __asm__ volatile ("" : : : "memory");
    }
}

static inline void wait_set_reg32(uint8_t *addr,int reg,uint32_t value){
    __asm__ volatile ("" : : : "memory");
    uint32_t ret=0;
    while(ret = *((volatile uint32_t *)(addr+reg)),(ret&value) != 0){
            usleep(10000);
            __asm__ volatile ("" : : : "memory");
    }
}

static inline void set_flag32(uint8_t *addr,int reg,uint32_t flag){
    set_reg32(addr,reg,get_reg32(addr,reg) | flag);
}
static inline void unset_flag32(uint8_t *addr,int reg,uint32_t flag){
    set_reg32(addr,reg,get_reg32(addr,reg) & ~flag);
}

static inline void write_io32(int fd,uint32_t value,size_t offset){
        if(pwrite(fd,&value,sizeof(value),offset) != sizeof(value)){
                printf("failed to pwrite io resouce");
        }
        __asm__ volatile("" : : : "memory");
}

static inline void write_io16(int fd,uint16_t value,size_t offset){
        if(pwrite(fd,&value,sizeof(value),offset) != sizeof(value)){
                printf("failed to pwrite io resouce");
        }
        __asm__ volatile("" : : : "memory");
}

static inline void write_io8(int fd,uint8_t value,size_t offset){
        if(pwrite(fd,&value,sizeof(value),offset) != sizeof(value)){
                printf("failed to pwrite io resouce");
        }
        __asm__ volatile("" : : : "memory");
}

static inline uint32_t read_io32(int fd,size_t offset){
        __asm__ volatile("" : : : "memory");
        uint32_t temp;
        if(pread(fd,&temp,sizeof(temp),offset) != sizeof(temp)){
                printf("failed to pread to resource");
        }
        return temp;
}

static inline uint32_t read_io16(int fd,size_t offset){
        __asm__ volatile("" : : : "memory");
        uint16_t temp;
        if(pread(fd,&temp,sizeof(temp),offset) != sizeof(temp)){
                printf("failed to pread to resource");
        }
        return temp;
 }
 
static inline uint32_t read_io8(int fd,size_t offset){
        __asm__ volatile("" : : : "memory");
        uint8_t temp;
        if(pread(fd,&temp,sizeof(temp),offset) != sizeof(temp)){
                printf("failed to pread to resource");
        }
        return temp;
}
uintptr_t vtop(uintptr_t vaddr);
struct dma_address allocate_dma_address(uint32_t ring_size);
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size);
struct pkt_buf *alloc_pkt_buf(struct mempool *mempool);
void pkt_buf_free(struct pkt_buf *buf);
//uint32_t rx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf);
//uint32_t tx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf);


