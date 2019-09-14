#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/limits.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "lib.h"

#define VFIO_IOVA 0x800000000

ssize_t MIN_DMA_MEMORY = 4096;
volatile int VFIO_CONTAINER_FILE_DESCRIPTOR = -1;

int get_vfio_container()
{
   return VFIO_CONTAINER_FILE_DESCRIPTOR;
}

void set_vfio_container(int fd)
{
    VFIO_CONTAINER_FILE_DESCRIPTOR = fd;
}

void vfio_enable_dma(int vfio_fd)
{
    int command_register_offset = 4;
    int bus_master_enable_bit = 2;

    struct vfio_region_info conf_reg = {.argsz = sizeof(conf_reg)};
    conf_reg.index = VFIO_PCI_CONFIG_REGION_INDEX;
    ioctl(vfio_fd,VFIO_DEVICE_GET_REGION_INFO,&conf_reg);

    uint16_t dma = 0;
    pread(vfio_fd,&dma,2,conf_reg.offset + command_register_offset);
    dma |= 1 << bus_master_enable_bit;
    pwrite(vfio_fd,&dma,2,conf_reg.offset + command_register_offset);
}

int init_vfio(const char *pci_addr)
{
    char path[PATH_MAX],iommu_group_path[PATH_MAX];
    snprintf(path,sizeof(path),"/sys/bus/pci/devices/%s/",pci_addr);
    
    struct stat buf;
    int ret = stat(path,&buf);
    if(ret<0){perror("failed to get correct path"); return -1;}
    strncat(path,"iommu_group",sizeof(path)-strlen(path) - 1);

    info("readlink iommu group path");
    if((ret = readlink(path,iommu_group_path,sizeof(iommu_group_path))) == -1){perror("readlink");return -1;}
    iommu_group_path[ret] = '\0';

    int group_id;
    char *group_name = basename(iommu_group_path);
    sscanf(group_name,"%d",&group_id);
    info("get iommu group id");

    int flag=0;
    int container = get_vfio_container();
    int vfio_gfd = 0;
    if(container = -1){
	flag = 1;
        /*create new container*/
        container = open("/dev/vfio/vfio",O_RDWR);
        set_vfio_container(container);

        if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION){
                perror("failed to get valid API version from the container");
        }
        if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)){
                perror("Doesn't support the IOMMU driver we want");
        }
    }
    
    /*create vfio group*/
    snprintf(path,sizeof(path),"/dev/vfio/%d",group_id);
    if((vfio_gfd = open(path,O_RDWR)) < 0){
		perror("failed to get vfio_gfd");
   } 
    info("get vfio group fd %d",vfio_gfd);

    struct vfio_group_status group_status = {.argsz = sizeof(group_status)};
    ioctl(vfio_gfd,VFIO_GROUP_GET_STATUS,&group_status);

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)){
        perror("Group is not viable (ie, not all devices bound for vfio)");
    }

    /* Add the group to the container */
    ioctl(vfio_gfd, VFIO_GROUP_SET_CONTAINER, &container);
    if(flag){
            ret = ioctl(container,VFIO_SET_IOMMU,VFIO_TYPE1_IOMMU);
    }

    int vfio_fd = ioctl(vfio_gfd,VFIO_GROUP_GET_DEVICE_FD,pci_addr);
    info("get vfio_fd %d",vfio_fd);
    
    info("enable enable dma for vfio");
    vfio_enable_dma(vfio_fd);

    return vfio_fd;
}

uint8_t *vfio_map_region(int vfio_fd,int region_index)
{
   int ret;
   struct vfio_region_info region_info = {.argsz = sizeof(region_info)};
   region_info.index = region_index;

   if((ret = ioctl(vfio_fd,VFIO_DEVICE_GET_REGION_INFO,&region_info))== -1){
           return MAP_FAILED;
   }
   return (uint8_t *)mmap(NULL,region_info.size,PROT_READ|PROT_WRITE,MAP_SHARED,vfio_fd,region_info.offset);
}

uint64_t vfio_map_dma(void *vaddr,uint32_t size)
{
   //uint64_t iova = (uint64_t)vaddr;
   __u64 vfio_mask = VFIO_BASE - 1;
   struct vfio_iommu_type1_dma_map dma_map ={
           .vaddr = (uint64_t)vaddr,
           .iova  = VFIO_IOVA,
           .size  = size < MIN_DMA_MEMORY ? MIN_DMA_MEMORY : size,
           .argsz = sizeof(dma_map),
           .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE};
   int container = get_vfio_container();
   
   while(dma_map.iova){

   	ioctl(container,VFIO_IOMMU_MAP_DMA,&dma_map);
   }
   
   return dma_map.iova;
}

uint64_t vfio_unmap_dma(int vfio_fd,uint64_t iova,uint32_t size)
{
   struct vfio_iommu_type1_dma_unmap dma_unmap = {
           .argsz = sizeof(dma_unmap),
           .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
           .iova  = iova,
           .size  = size
   };
   int container = get_vfio_container();
   int ret = ioctl(container,VFIO_IOMMU_UNMAP_DMA,&dma_unmap);
   if(ret == -1){
           return -1;
   }

   return ret;
}

