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
#include <linux/mman.h>
#include <linux/limits.h>
#include <linux/vfio.h>
#include <sys/mman.h>

#include "lib.h"
#include "ixgbe.h"
#include "struct.h"
#include "vfio.h"

//const int TX_CLEAN_BATCH = 32;
uint32_t path_id = 1;
uint32_t page_id = 1;

//from ixy
static uintptr_t virt_to_phys(void* virt) {
	info("virt to phys");
	long pagesize = sysconf(_SC_PAGESIZE);
	int fd = open("/proc/self/pagemap", O_RDONLY);
	if(fd = -1){
		perror("failed to get fd");
	}

	lseek(fd, (uintptr_t) virt / pagesize * sizeof(uintptr_t), SEEK_SET);
	uintptr_t phy = 0;
	read(fd, &phy, sizeof(phy));
	close(fd);
	if (!phy) {
		debug("failed to translate virtual address %p to physical address", virt);
	}
	return (phy & 0x7fffffffffffffULL) * pagesize + ((uintptr_t) virt) % pagesize;
}

struct dma_address allocate_dma_address(size_t size,volatile int flag)
{
    if(flag){
	info("allocating dma memory via vfio");
		void* virt_addr = (void*) mmap(NULL,size,PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
		uint64_t iova = (uint64_t) vfio_map_dma(virt_addr,size);
		struct dma_address dma_addr;
		dma_addr.virt_addr = virt_addr;
		dma_addr.phy_addr = iova;
		return dma_addr;
	}
	else{
		info("allocating dma memory via huge page");
	//適当なページsnprintf
	//ftruncateでcut
	//get fd to use open
	//mmap using the fd
	//return address virt and virt_to_phys(virt)
		if(size % HUGE_PAGE_SIZE){
			size = ((size >> HUGE_PAGE_BITS) + 1) << HUGE_PAGE_BITS;
		}
		
		uint32_t id = __sync_fetch_and_add(&page_id,1);
		char path[PATH_MAX];
		snprintf(path,PATH_MAX,"/mnt/huge/ixy-%d-%d",id,getpid());
		int fd = open(path,O_CREAT | O_RDWR,S_IRWXU);
		ftruncate(fd,(off_t)size);
		void *virt_addr = mmap(NULL,size,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_HUGETLB,fd,0);	
		mlock(virt_addr,size);
		close(fd);
		unlink(path);
		return (struct dma_address){
			.virt_addr = virt_addr,
			.phy_addr = virt_to_phys(virt_addr)
		};
	}
}

/*ディスクリプターにパケットが届いた時にそれを格納するためのメモリープール*/
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size)
{
    info("allocate_mempool_mem");
    struct dma_address dma_addr;
    entry_size = entry_size ? entry_size : 2048;

    struct mempool *mempool = (struct mempool*)malloc(sizeof(struct mempool) + num_entries * sizeof(uint32_t));
    
    dma_addr = allocate_dma_address(num_entries*entry_size,1);
    mempool->num_entries = num_entries;
    mempool->num_entries = entry_size;
    mempool->base_addr   = dma_addr.virt_addr;
    mempool->free_stack_top = num_entries;

    for(uint32_t i=0;i<num_entries;i++){
        mempool->free_stack[i] = i;
        struct pkt_buf *buf = (struct pkt_buf *)(((uint8_t *)mempool->base_addr) + i * entry_size);
        buf->buf_addr_phy = (uintptr_t)buf;
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

