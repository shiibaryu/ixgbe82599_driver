void vfio_enable_dma(int vfio_fd);
int init_vfio(char *pci_addr);
uint8_t *vfio_map_region(int vfio_fd,int region_index);
uint64_t vfio_map_dma(void *vaddr,uint32_t size);
uint64_t vfio_unmap_dma(int vfio_fd,uint64_t iova,uint32_t size);
