#include <linux/limit.h>
#include <unistd.h>
#include <linux/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stddef.h>>

uint32_t path_id = 1;
uintptr_t vtop(uintptr_t vaddr)
{
    FILE *pagemap;
    uintptr_t paddr = 0;
    int offset = (*vaddr / sysconf(_SC_PAGESIZE)) * sizeof(uint64_t);
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
    snprintf(path,PATH_MAX,"/mnt/huge/ixgbe-%d-%d",this_id,get_pid());
    int fd = open(path,O_CREATE|O_RDWR,S_IRWXU);
    //ring_size以上は切り捨てる
    check_err(ftruncate(fd,(off_t)ring_size),"failed to ftruncate");
   //仮想アドレスが帰ってくるのでそれを物理にして、dma_address
   //に格納してreturn
   //ここで仮想アドレスget
   uintptr_t virt_addr = (uintptr_t)mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHAERED | MAP_HUGETLB,fd,0);

   close(fd);
   unlink(path);

   return(struct dma_address){
           .virt_addr = virt_addr;
           .phy_addr =  vtop(virt_addr);
   }
}

/*ディスクリプターにパケットが届いた時にそれを格納するためのメモリープール*/
struct mempool *allocate_mempool_mem(uint32_t num_entries,uint32_t entry_size)
{
    struct dma_address dma_addr;
    entry_size = entry_size ? entry_size : 2048;

    struct mempool *mempool = (struct mempool*)malloc(sizeof(struct mempool) + num_entries * sizeof(uint32_t));
    
    dma_addr = allocate_dma_address(num_entries*entry_size);
    mempool->num_entries = num_entries;
    mempool->buf_entries = entry_size;
    mempool->base_addr   = dma_addr.virt_addr;
    mempool->free_stack_top = num_entries;

    for(uint32_t i=0;i<num_entries;i++){
        mempool->free_stack[i] = i;
        struct pkt_buf *buf = (struct pkt_buf *)(((uint8_t *)mempool->base_addr) + i * entry_size);
        buf->buf_addr_phy = vtop((uintptr_t)buf);
        buf->mempool_idx = i;
        buf->mempool = mempool
        buf->size = 0;
    }
    
    return mempool;
}

void pkt_buf_free(struct pkt_buf *buf)
{
    struct mempool *mempool = buf->mempool;
    mempool->free_stack[mempool->free_stack_top++] = buf->mempool_idx;
}

