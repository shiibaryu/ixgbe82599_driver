int init_vfio(char *pci_addr)
{
    char path[PATH_MAX],iommu_group_path[PATH_MAX];
    snprintf(path,sizeof(path),"/sys/buf/pci/devices/%s/",pci_addr);
    
    struct stat buf;
    int ret = stat(path,&buf);
    if(ret<0){error("failed to get correct path"); return -1;}
    strncat(path,"iommu_group",sizeof(path)-strlen(path) - 1;

    if((ret = readlink(path,iommu_group_path,sizeof(iommu_group_path))) == -1){perror("readlink");return -1;}
    iommu_group_path[len] = "\0";
    char *group_name = basename(iommu_group_path);
    int group_id;
    sscanf(group_name,"%d",&group_id);

    
}
