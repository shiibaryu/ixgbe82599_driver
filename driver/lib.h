#define NUM_RX_QUEUES 512
#define NUM_TX_QUEUES 512
#define MAX_QUEUES 64



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


uintptr_t vtop(uintptr_t vaddr);
