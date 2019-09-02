#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

#define NUM_RX_QUEUES 512
#define NUM_TX_QUEUES 512
#define MAX_QUEUES 64
#define SIZE_PKT_BUF_HEADROOM 40

#ifndef NDEBUG
#define debug(fmt, ...) do {\
	fprintf(stderr, "[DEBUG] %s:%d %s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);\
} while(0)
#else
#define debug(fmt, ...) do {} while(0)
#undef assert
#define assert(expr) (void) (expr)
#endif

#define info(fmt, ...) do {\
	fprintf(stdout, "[INFO ] %s:%d %s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);\
} while(0)


uintptr_t vtop(uintptr_t vaddr);
struct dma_address allocate_dma_address(uint32_t ring_size);
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size);
static inline void set_reg32(uint8_t *addr,int reg,uint32_t value){
    __asm__ volatile ("" : : : "memory");
    *((volatile uint32_t *)(addr+reg)) = value;
}

static inline uint32_t get_reg32(uint8_t *addr,int reg){
    __asm__ volatile ("" : : : "memory");
    return *((volatile uint32_t *)(addr + reg));
}


static inline void wait_clear_reg32(const uint8_t* addr, int reg, uint32_t mask) {
	__asm__ volatile ("" : : : "memory");
	uint32_t cur = 0;
	while (cur = *((volatile uint32_t*) (addr + reg)), (cur & mask) != 0) {
		debug("waiting for flags 0x%08X in register 0x%05X to clear, current value 0x%08X", mask, reg, cur);
		usleep(10000);
		__asm__ volatile ("" : : : "memory");
	}
}

static inline void wait_set_reg32(const uint8_t* addr, int reg, uint32_t mask) {
	__asm__ volatile ("" : : : "memory");
	uint32_t cur = 0;
	while (cur = *((volatile uint32_t*) (addr + reg)), (cur & mask) != mask) {
		debug("waiting for flags 0x%08X in register 0x%05X, current value 0x%08X", mask, reg, cur);
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

static inline uint16_t read_io16(int fd,size_t offset){
        __asm__ volatile("" : : : "memory");
        uint16_t temp;
        if(pread(fd,&temp,sizeof(temp),offset) != sizeof(temp)){
                printf("failed to pread to resource");
        }
        return temp;
 }
 
static inline uint8_t read_io8(int fd,size_t offset){
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


