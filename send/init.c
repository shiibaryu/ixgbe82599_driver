#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <linux/limits.h>
#include <linux/vfio.h>

#include "ixgbe.h"
#include "lib.h"
#include "struct.h"
#include "vfio.h"
#include "pci.h"

/*
NICへの割り込み禁止
デバイスのリセットとConfigurationレジスタの設定
DMAの初期化を待つ
PHYとLINKの設定
receiveの初期化
transmitの初期化
割り込みを許可
*/

const int MAX_RX_QUEUE_ENTRIES = 4096;
const int MAX_TX_QUEUE_ENTRIES = 4096;

const int NUM_RX_QUEUE_ENTRIES = 512;
const int NUM_TX_QUEUE_ENTRIES = 512;

const int PKT_BUF_ENTRY_SIZE = 2048;
const int MIN_MEMPOOL_ENTRIES = 4096;

const int TX_CLEAN_BATCH = 64;

volatile int VFIO_CHK = 0;

//PHYとLINKの設定
void init_link(struct ixgbe_device *ix_dev)
{
    info("start: init_link");
    //BX/SGMII LINKのセットアップ
    //AUTOCのなかのLink mode select fieldを適切な運用モードに
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_LMS_MASK) | IXGBE_AUTOC_LMS_10G_SERIAL);
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_10G_PMA_PMD_MASK) | IXGBE_AUTOC_10G_XAUI);
    set_flag32(ix_dev->addr,IXGBE_AUTOC,IXGBE_AUTOC_AN_RESTART);
    info("end: init_link");
}

void init_stats(struct ixgbe_device *ix_dev)
{
    info("start: init_stats");
    //good packet receive and transmit
    uint32_t rx_pkts = get_reg32(ix_dev->addr,IXGBE_GPRC);
    uint32_t tx_pkts = get_reg32(ix_dev->addr,IXGBE_GPTC);
    //good octet receive and transmit
    uint64_t rx_bytes = get_reg32(ix_dev->addr,IXGBE_GORCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GORCH)) << 32);
    uint64_t tx_bytes = get_reg32(ix_dev->addr,IXGBE_GOTCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GOTCH)) << 32);
    info("end: init_stats");
}

void init_tx_reg(struct ixgbe_device *ix_dev)
{
    info("start: init_tx_reg");
    //IXGBE_HLREG0に設定したい内容のフラグを立てるためにorをとって立てる
    //今回だとcrcオフロードとパケットパディング 
    set_flag32(ix_dev->addr,IXGBE_HLREG0,IXGBE_HLREG0_TXCRCEN | IXGBE_HLREG0_TXPADEN);
    set_reg32(ix_dev->addr, IXGBE_TXPBSIZE(0), IXGBE_TXPBSIZE_40KB);
    //tcpのセグメンテーション設定とか言われてるけどまだ喋れないのでパス
    //毎度毎度のpage size初期化
    for(int i=1;i<8;i++){
        set_flag32(ix_dev->addr,IXGBE_TXPBSIZE(i),0);
    }
    //DCB->data center bridging(データセンターのトランスポートのロスレスのイーサネット拡張らしい
    //つまりいらない？
    //DCBとVT(virtualization)なし
    set_reg32(ix_dev->addr,IXGBE_DTXMXSZRQ,0xFFFF);
    //0にしろと書かれていたので
    unset_flag32(ix_dev->addr,IXGBE_RTTDCS,IXGBE_RTTDCS_ARBDIS);
    info("end: init_tx_reg");
}

void init_tx_queue(struct ixgbe_device *ix_dev)
{
    info("start: init_tx_queue");
    uint16_t i;
    for(i=0;i<ix_dev->num_tx_queues;i++){
        //キューの割り当て
        //レジスタの初期化
        uint32_t tx_ring = sizeof(union ixgbe_adv_tx_desc)*NUM_TX_QUEUE_ENTRIES;
        struct dma_address dma_addr = allocate_dma_address(tx_ring,VFIO_CHK);
        
        memset(dma_addr.virt_addr,-1,tx_ring);
        set_reg32(ix_dev->addr,IXGBE_TDBAL(i),(uint32_t)(dma_addr.phy_addr & 0xFFFFFFFFul));
        set_reg32(ix_dev->addr,IXGBE_TDBAH(i),(uint32_t)(dma_addr.phy_addr >> 32));
        set_reg32(ix_dev->addr,IXGBE_TDLEN(i),tx_ring);
        //set_flag32(ix_dev->addr,IXGBE_TXDCTL(i),IXGBE_TXDCRL_ENABLE);
        
        info("tx ring %d phy addr ; 0x%012lX",i,dma_addr.phy_addr);
        info("tx ring %d virt addr ; 0x%012lX",i,(uintptr_t)dma_addr.virt_addr);

        //高い性能とpcieの低レイテンシを得るためのtransmit control descriptorのmagic
        //dpdkで使われてるっぽい
        //transmition latencyを抑えるには、PTHRETHはできるだけ高くして、HTHRETHとWTHRESHはできるだけ低くするっぽい
        //pcieのオーバーヘッドを小さくするには、PtHRETHはできるだけ小さく、HTHRETHとWTHRESHはできるだけ大きくらしい
        uint32_t txdctl = get_reg32(ix_dev->addr,IXGBE_TXDCTL(i));
        txdctl &= ~(0x3F | (0x3F << 8) | (0x3F << 16));
        txdctl |= (36 | (8<<8) | (4<<16));
        set_reg32(ix_dev->addr,IXGBE_TXDCTL(i),txdctl);

        struct tx_queue *txq = ((struct tx_queue*)ix_dev->tx_queues) + i;
        txq->num_entries = NUM_TX_QUEUE_ENTRIES;
        txq->descriptors = (union ixgbe_adv_tx_desc*)dma_addr.virt_addr;
    }
    info("end: init_tx_queue");
}

void init_tx(struct ixgbe_device *ix_dev)
{
    info("start: init_tx");
    init_tx_reg(ix_dev);
    init_tx_queue(ix_dev);
    //enable dma
    set_reg32(ix_dev->addr,IXGBE_DMATXCTL,IXGBE_DMATXCTL_TE);
    info("end: init_tx");
}

void start_tx_queue(struct ixgbe_device *ix_dev,int i)
{
        info("start: start_tx_queue %d",i);
        struct tx_queue *txq = ((struct tx_queue*)(ix_dev->tx_queues)) + i;

        set_reg32(ix_dev->addr,IXGBE_TDH(i),0);
        set_reg32(ix_dev->addr,IXGBE_TDT(i),0);

        set_flag32(ix_dev->addr,IXGBE_TXDCTL(i),IXGBE_TXDCTL_ENABLE);
        wait_set_reg32(ix_dev->addr,IXGBE_TXDCTL(i),IXGBE_TXDCTL_ENABLE);
        info("end: start_tx_queue %d",i);
}


#define wrap_ring(index,ring_size) (uint16_t)((index+1)&(ring_size-1))

/*void tx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_bufs)
{
    struct tx_queue *txq = ((struct tx_queue*)(ix_dev->tx_queues)) + queue_id;
    //uint16_t tx_index = txq->tx_index;
    uint16_t clean_index = txq->clean_index;

    while(true){
            int64_t cleanable = txq->tx_index - clean_index;
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
                    //int32_t i = clean_index;
                    while(true){
                            struct pkt_buf *buf = txq->virtual_address[clean_index];
                            pkt_buf_free(buf);
                            if(clean_index==cleanup_to){
                                    break;
                            }
                            clean_index = wrap_ring(clean_index,txq->num_entries);
                    }
                    clean_index = wrap_ring(cleanup_to,txq->num_entries);
            }
            else{break;}
    }
    txq->clean_index = clean_index;
    uint32_t sent;
    for(sent=0;sent<num_bufs;sent++){
            uint32_t next_index = wrap_ring(txq->tx_index,txq->num_entries);
            if(clean_index == next_index){
                    break;
            }
            struct pkt_buf *buf = bufs[sent];
            txq->virtual_address[txq->tx_index] = (void *)buf;
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + txq->tx_index;
            txq->tx_index = next_index;
            txd->read.buffer_addr = buf->buf_addr_phy + offsetof(struct pkt_buf,data);
            txd->read.cmd_type_len = IXGBE_ADVTXD_DCMD_EOP | IXGBE_ADVTXD_DCMD_RS | IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_DATA | buf->size;
            txd->read.olinfo_status = buf->size << IXGBE_ADVTXD_PAYLEN_SHIFT;
    }
    set_reg32(ix_dev->addr,IXGBE_TDT(queue_id),txq->tx_index);
    //return sent;
}*/

uint32_t ixgbe_get_link_speed(struct ixgbe_device *ix_dev)
{
        info("start: ixgbe_get_link_speed");
        uint32_t link_speed = get_reg32(ix_dev->addr,IXGBE_LINKS);
        if(!(link_speed & IXGBE_LINKS_UP)){
                return 0;
    	}    
        switch(link_speed & IXGBE_LINKS_SPEED_82599){
                case IXGBE_LINKS_SPEED_100_82599:
		    info("100M");
                    return 100;
                case IXGBE_LINKS_SPEED_1G_82599:
                    return 1000;
		    info("1G");
                case IXGBE_LINKS_SPEED_10G_82599:
		    info("10G");		
                    return 10000;
                default:
                    return 0;
        }
        info("end: ixgbe_get_link_speed");
}

void wait_for_link(struct ixgbe_device *ix_dev)
{
        info("start: wait_for_link");
        info("wait for link ...");
        uint32_t max_wait = 10000000;
        uint32_t poll_int = 100000;
        uint32_t speed;
        
        while(!(speed = ixgbe_get_link_speed(ix_dev)) && max_wait > 0){
                usleep(poll_int);
                max_wait -= poll_int;
        }
        info("Link speed is %d Mbit/s",speed);
        info("end: wait_for_link");
}

/*initialization sequence*/
void do_init_seq(struct ixgbe_device *ix_dev)
{
        
 	info("disable interruption");
    	set_reg32(ix_dev->addr,IXGBE_EIMC,0x7FFFFFFF);

        info("initialize IXGBE_CTRL");
	set_reg32(ix_dev->addr, IXGBE_CTRL, IXGBE_CTRL_RST_MASK);
	wait_clear_reg32(ix_dev->addr, IXGBE_CTRL, IXGBE_CTRL_RST_MASK);
	usleep(10000);

    	info("re-disable interruption");
    	set_reg32(ix_dev->addr,IXGBE_EIMC,0x7FFFFFFF);

    	info("setting General Configuration register");
	wait_set_reg32(ix_dev->addr, IXGBE_EEC, IXGBE_EEC_ARD);

    	info("wait dma initilization");
	wait_set_reg32(ix_dev->addr, IXGBE_RDRXCTL, IXGBE_RDRXCTL_DMAIDONE);

	//linkの初期化
        info("initialize link");
	init_link(ix_dev);

    	//Statisticsの初期化
    	info("initialize statistics");
    	init_stats(ix_dev);

        //transmitの初期化
    	init_tx(ix_dev);

    	uint16_t i;

    	for(i=0;i<ix_dev->num_tx_queues;i++){
        	start_tx_queue(ix_dev,i);
    	}

    	//割り込みの許可
    	set_flag32(ix_dev->addr,IXGBE_FCTRL,IXGBE_FCTRL_MPE | IXGBE_FCTRL_UPE);  

    	//リンク待ち
    	wait_for_link(ix_dev);

    	info("end: do_init_sequence");
}

struct ixgbe_device *start_ixgbe(const char *pci_addr,uint16_t rx_queues,uint16_t tx_queues)
{
   if(getuid()){
       debug("Not running as root,this will probably fail");
   }
   if(tx_queues > MAX_QUEUES){
      debug("Tx queues %d exceed MAX_QUEUES",tx_queues);
   }

   struct ixgbe_device *ix_dev = (struct ixgbe_device*)malloc(sizeof(struct ixgbe_device));
   //strdup()->文字列をコピーして返す
   ix_dev->pci_addr = strdup(pci_addr);
   info("pci addr %s",ix_dev->pci_addr);
   ix_dev->num_tx_queues = tx_queues;

   char path[PATH_MAX];
   snprintf(path,PATH_MAX,"/sys/bus/pci/devices/%s/iommu_group",pci_addr);

   struct stat buf;
   ix_dev->vfio = stat(path,&buf) == 0;
   ix_dev->vfio = false;
   if(ix_dev->vfio){
           info("initialize vfio");
           ix_dev->vfio_fd = init_vfio(pci_addr);
           info("get vfio_fd %d",ix_dev->vfio_fd);
           if(ix_dev->vfio_fd < 0){
                   debug("faled to get vfio_fd");
           }
	   ix_dev->addr = vfio_map_region(ix_dev->vfio_fd,VFIO_PCI_BAR0_REGION_INDEX);
	   //ix_dev->iovamask = ix_dev->addr-1;
  	   
           info("done vfio_map_region()");
   }
   else{
	   info("use pci_map_resource");
	   ix_dev->addr = pci_map_resource(pci_addr);
   }
    
    ix_dev->tx_queues = calloc(tx_queues,sizeof(struct tx_queue) + sizeof(void *) * MAX_TX_QUEUE_ENTRIES);
    do_init_seq(ix_dev);

    info("finished all initilization process");
    return ix_dev;
}

struct ixgbe_device *do_ixgbe(const char *pci_addr,uint16_t rx_queue,uint16_t tx_queue)
{
    info("start do_ixgbe()");
    char path[PATH_MAX];
    snprintf(path,PATH_MAX,"/sys/bus/pci/devices/%s/config",pci_addr);
    int config_fd = open(path,O_RDONLY);
    if(config_fd < 0){
	debug("failed to open config_fd");
    }
    uint16_t vendor_id = read_io16(config_fd,0);
    uint16_t device_id = read_io16(config_fd,2);
    uint32_t class_id  = read_io32(config_fd,8) >> 24;

    info("vendor id: %x  device_id: %x",vendor_id,device_id);
    info("go start_ixgbe()");

    return start_ixgbe(pci_addr,rx_queue,tx_queue);
}
