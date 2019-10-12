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

uint32_t page_id = 1;


static uintptr_t virt_to_phys(void* virt) {
	int ret=0;
	long pagesize = sysconf(_SC_PAGESIZE);
	int fd = open("/proc/self/pagemap", O_RDONLY);
	if(fd == -1){
		perror("failed to open");
	}
	// pagemap is an array of pointers for each normal-sized page
	lseek(fd, (uintptr_t) virt / pagesize * sizeof(uintptr_t),SEEK_SET);
	uintptr_t phy = 0;
	ret = read(fd, &phy, sizeof(phy));
	if(ret == -1){
		perror("failed to read");
	}
	close(fd);
	if (!phy) {
		debug("failed to translate virtual address %p to physical address", virt);
	}
	// bits 0-54 are the page number
	return (phy & 0x7fffffffffffffULL) * pagesize + ((uintptr_t) virt) % pagesize;
}

static int pci_id=0;
struct dma_address allocate_dma_address(size_t size,volatile int flag)
{
	/*if(flag){
		info("allocating dma memory via vfio");
		void* virt_addr = (void*) mmap(NULL,size,PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
		uint64_t iova = (uint64_t) vfio_map_dma(virt_addr,size);
		info("done vfio_map_dma");
		return (struct dma_address){
			.virt_addr = virt_addr,
			.phy_addr  = iova,
		};
	}*/
	 //else {
		debug("allocating dma memory via huge page");
		if (size % HUGE_PAGE_SIZE) {
			size = ((size >> HUGE_PAGE_BITS) + 1) << HUGE_PAGE_BITS;
		}
		//if (require_contiguous && size > HUGE_PAGE_SIZE) {
		//	error("could not map physically contiguous memory");
		//}
		uint32_t id = __sync_fetch_and_add(&pci_id, 1);
		char path[PATH_MAX];
		snprintf(path,PATH_MAX,"/mnt/huge/%d_%d",getpid(),id);
		int fd = open(path,O_CREAT | O_RDWR,S_IRWXU);
		ftruncate(fd,(off_t)size);
		void *vaddr = (void*)mmap(NULL,size,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_HUGETLB,fd,0);
		mlock(vaddr,size);
		close(fd);
		unlink(path);
		return(struct dma_address){
			.virt_addr = vaddr,
			.phy_addr = virt_to_phys(vaddr),
		};
	//}
}

/*ディスクリプターにパケットが届いた時にそれを格納するためのメモリープール*/
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size)
{
    info("start:allocate_mempool_mem");
    entry_size = entry_size ? entry_size : 2048;

    struct mempool *mempool = (struct mempool*)malloc(sizeof(struct mempool) + num_entries * sizeof(uint32_t));
    
    struct dma_address dma_addr;
    dma_addr = allocate_dma_address(num_entries*entry_size,0);
    info("done");
    mempool->num_entries = num_entries;
    mempool->num_entries = entry_size;
    mempool->base_addr   = dma_addr.virt_addr;
    mempool->free_stack_top = num_entries;
    for(uint32_t i=0;i<num_entries;i++){
        mempool->free_stack[i] = i;
        struct pkt_buf *buf = (struct pkt_buf *)(((uint8_t *)mempool->base_addr) + i * entry_size);
        buf->buf_addr_phy = virt_to_phys(buf);
        buf->mempool_idx = i;
        buf->mempool = mempool;
        buf->size = 0;
    }
    info("end:allocate_mempool_mem"); 
    return mempool;
}

uint32_t alloc_pkt_buf_batch(struct mempool *mempool,struct pkt_buf *buf[],uint32_t num_bufs)
{
   if(mempool->free_stack_top < num_bufs){
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

