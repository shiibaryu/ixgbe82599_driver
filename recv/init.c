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

const int TX_CLEAN_BATCH = 32;

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

//特定のレジスタをreadすると初期化されるっぽい？？？
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

void init_rx_reg(struct ixgbe_device *ix_dev)
{
    int i;
    info("start: init_rx_reg");
    set_reg32(ix_dev->addr,IXGBE_RXPBSIZE(0),IXGBE_RXPBSIZE_128KB);
    for(i=1;i<8;i++){
            set_reg32(ix_dev->addr,IXGBE_RXPBSIZE(i),0);
    }

    set_flag32(ix_dev->addr,IXGBE_HLREG0,IXGBE_HLREG0_RXCRCSTRP);
    set_flag32(ix_dev->addr,IXGBE_RDRXCTL,IXGBE_RDRXCTL_CRCSTRIP);

    //broadcastとjamboフレームのレジスタセット
    //broadcastのセット FCTRL->filter control register(receive register)
    //BAM -> broadcast accept mode
    set_flag32(ix_dev->addr,IXGBE_FCTRL,IXGBE_FCTRL_BAM);
    //jamboフレーム5000?
    //set_reg32(ixgbe->dev,IXGBE_MAXFRS,5000);

    info("end: init_tx_reg");
}

void init_rx_queue(struct ixgbe_device *ix_dev)
{
    info("start: init_rx_queue");
    //ring bufferの確保と初期化
    //それに伴うレジスタの初期化
    //それぞれのdescriptorは16bitのサイズらしい
    //receive descriptor listのメモリ割り当て
    uint16_t i;
    for(i=0;i < ix_dev->num_rx_queues;i++){
        //advanced rx descriptorを有効化する
        set_reg32(ix_dev->addr,IXGBE_SRRCTL(i),(get_reg32(ix_dev->addr,IXGBE_SRRCTL(i)) & ~IXGBE_SRRCTL_DESCTYPE_MASK) | IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF);

        //でかいパケットからのoverflowing対策らしい
        set_flag32(ix_dev->addr,IXGBE_SRRCTL(i), IXGBE_SRRCTL_DROP_EN);

        //bufferを割り当てるには、キューのサイズ(descriptor単体)とその個数でかける
        uint32_t ring_size = sizeof(union ixgbe_adv_rx_desc)*NUM_RX_QUEUE_ENTRIES;
        struct dma_address dma_addr = allocate_dma_address(ring_size,VFIO_CHK);
        
       //DMAアクティベーション初期の時、メモリにアクセスされることを防ぐために仮想アドレスを初期化?
        memset(dma_addr.virt_addr,-1,ring_size);

        //baseアドレスの計算
        set_reg32(ix_dev->addr,IXGBE_RDBAL(i),(uint32_t)(0xFFFFFFFFull & dma_addr.phy_addr));
        set_reg32(ix_dev->addr,IXGBE_RDBAH(i),(uint32_t)(dma_addr.phy_addr >> 32));

        //長さの計算
        set_reg32(ix_dev->addr,IXGBE_RDLEN(i),ring_size);

        //headもtailも先頭
        set_reg32(ix_dev->addr,IXGBE_RDH(i),0);
        set_reg32(ix_dev->addr,IXGBE_RDT(i),0);
	struct rx_queue *rxq = ((struct rx_queue*)(ix_dev->rx_queues)) + i;
	rxq->num_entries = NUM_RX_QUEUE_ENTRIES;
	rxq->rx_index = 0;
	rxq->descriptors = (union ixgbe_adv_rx_desc*)dma_addr.virt_addr;
    }

    //rxをenableする前にやっておくこと
    set_flag32(ix_dev->addr,IXGBE_CTRL_EXT,IXGBE_CTRL_EXT_NS_DIS);
    for(uint16_t i=0;i< ix_dev->num_rx_queues;i++){
            //DCA -> direct cache access
            unset_flag32(ix_dev->addr,IXGBE_DCA_RXCTRL(i), 1<<12);
    }
    info("end: init_rx_queue");
}

void init_rx(struct ixgbe_device *ix_dev)
{
    info("start: init_rx");
    //receiveを止める
    unset_flag32(ix_dev->addr,IXGBE_RXCTRL,IXGBE_RXCTRL_RXEN);
    //初期化処理
    //receive address registerはEEPROMのなかにあるから初期化処理もうされてる？
    init_rx_reg(ix_dev);
    
    //memoryとかdescriptorとかの初期化というか準備・allocation
    init_rx_queue(ix_dev);
    //receive再開
    set_flag32(ix_dev->addr,IXGBE_RXCTRL,IXGBE_RXCTRL_RXEN);
    info("end: init_rx");
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
        
        //rxの時と同様例のテクニック
        memset(dma_addr.virt_addr,-1,tx_ring);
        
        //baseアドレスの設定
        set_reg32(ix_dev->addr,IXGBE_TDBAL(i),(uint32_t)(dma_addr.phy_addr & 0xFFFFFFFFul));
        set_reg32(ix_dev->addr,IXGBE_TDBAH(i),(uint32_t)(dma_addr.phy_addr >> 32));

        //長さセット
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

void start_rx_queue(struct ixgbe_device *ix_dev,uint16_t queue)
{
    info("start rx queue %d",queue);

    struct rx_queue *rxq = ((struct rx_queue*)(ix_dev->rx_queues)) + queue;
    int mempool_size = NUM_RX_QUEUE_ENTRIES + NUM_TX_QUEUE_ENTRIES;

    rxq->mempool = allocate_mempool_mem(mempool_size < MIN_MEMPOOL_ENTRIES ? MIN_MEMPOOL_ENTRIES : mempool_size,PKT_BUF_ENTRY_SIZE);
    for(int i=0;i<rxq->num_entries;i++){
            volatile union ixgbe_adv_rx_desc *rxd = rxq->descriptors + i;
            struct pkt_buf *buf = alloc_pkt_buf(rxq->mempool);
            if(!buf){
                  debug("failed to allocate buf");
            }
            //offsetof(型,メンバ指示子); メンバ指示子までのオフセット得られる
            rxd->read.pkt_addr = buf->buf_addr_phy + offsetof(struct pkt_buf,data);
            rxd->read.hdr_addr = 0;
            rxq->virtual_address[i] = buf;
    }
    //rx queueをenable
    set_flag32(ix_dev->addr,IXGBE_RXDCTL(queue),IXGBE_RXDCTL_ENABLE);
    wait_set_reg32(ix_dev->addr,IXGBE_RXDCTL(queue),IXGBE_RXDCTL_ENABLE);
    set_reg32(ix_dev->addr,IXGBE_RDH(queue),0);
    set_reg32(ix_dev->addr,IXGBE_RDT(queue),rxq->num_entries - 1);
    
    info("start rx queue %d",queue);
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

uint32_t rx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_buf)
{
    //datasheet 7.1
    //パケットの確認
    //addressのフィルタリング(今回はなし)
    //DMA queue assignment
    //paketをstore
    //ホストメモリーのreceive queueにデータをおくつ
    //receive descriptorの状態更新(RDH,RDTを動かしたりとか)

    struct rx_queue *rxq = ((struct rx_queue *)(ix_dev->rx_queues)) + queue_id;
    uint16_t rx_index = rxq->rx_index;
    uint16_t prev_rx_index = rxq->rx_index;
    uint32_t i;
    for(i=0;i<num_buf;i++){
            volatile union ixgbe_adv_rx_desc *rxd = rxq->descriptors + rx_index;
	    uint32_t status = rxd->wb.upper.status_error;
            if(status & IXGBE_RXDADV_STAT_DD){
                    if(!(status & IXGBE_RXDADV_STAT_EOP)){
                            debug("multi_segment packt not supported!");
                    }
                union ixgbe_adv_rx_desc desc = *rxd;
	       	uint32_t data = desc.wb.lower.lo_dword.data;
		//info("%d",data);
                struct pkt_buf *buf = (struct pkt_buf *)rxq->virtual_address[rx_index];
                buf->size = desc.wb.upper.length;
                //そのポインタをread.pkt_addrに登録
                struct pkt_buf *p_buf = alloc_pkt_buf(rxq->mempool);
                if(!p_buf){
                    perror("failed to allocate pkt_buf");
                    return -1;
                }
                rxd->read.pkt_addr = p_buf->buf_addr_phy + offsetof(struct pkt_buf,data);
                rxd->read.hdr_addr = 0;
                rxq->virtual_address[rx_index] = p_buf;
                bufs[i]=buf;
                prev_rx_index = rx_index;
                rx_index = wrap_ring(rx_index,rxq->num_entries);
           } 
    }
    if(rx_index != prev_rx_index){
            set_reg32(ix_dev->addr,IXGBE_RDT(queue_id),prev_rx_index);
            rxq->rx_index = rx_index;
    }
    return i;
}


uint32_t tx_batch(struct ixgbe_device *ix_dev,uint16_t queue_id,struct pkt_buf *bufs[],uint32_t num_bufs)
{
    info("tx_batch");
    struct tx_queue *txq = ((struct tx_queue*)(ix_dev->tx_queues)) + queue_id;
    uint16_t tx_index = txq->tx_index;
    uint16_t clean_index = txq->clean_index;

    while(true){
            int32_t cleanable = tx_index - clean_index;
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
                    int32_t i = clean_index;
                    while(true){
                            struct pkt_buf *buf = txq->virtual_address[i];
                            pkt_buf_free(buf);
                            if(i==cleanup_to){
                                    break;
                            }
                            i = wrap_ring(i,txq->num_entries);
                    }
                    clean_index = wrap_ring(cleanup_to,txq->num_entries);
            }
            else{break;}
    }
    txq->clean_index = clean_index;
    uint32_t sent;
    for(sent=0;sent<num_bufs;sent++){
            uint32_t next_index = wrap_ring(tx_index,txq->num_entries);
            if(clean_index == next_index){
                    break;
            }
            struct pkt_buf *buf = bufs[sent];
            txq->virtual_address[tx_index] = (void *)buf;
            volatile union ixgbe_adv_tx_desc *txd = txq->descriptors + tx_index;
            txq->tx_index = next_index;
            txd->read.buffer_addr = buf->buf_addr_phy + offsetof(struct pkt_buf,data);
            txd->read.cmd_type_len = IXGBE_ADVTXD_DCMD_EOP | IXGBE_ADVTXD_DCMD_RS | IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_DATA | buf->size;
            txd->read.olinfo_status = buf->size << IXGBE_ADVTXD_PAYLEN_SHIFT;
    }
    set_reg32(ix_dev->addr,IXGBE_TDT(queue_id),tx_index);
    info("end: tx_batch");
    return sent;
}

uint32_t ixgbe_get_link_speed(struct ixgbe_device *ix_dev)
{
        info("start: ixgbe_get_link_speed");
        uint32_t link_speed = get_reg32(ix_dev->addr,IXGBE_LINKS);
        if(!(link_speed & IXGBE_LINKS_UP)){
                return 0;
        }
        switch(link_speed & IXGBE_LINKS_SPEED_82599){
                case IXGBE_LINKS_SPEED_100_82599:
                    return 100;
                case IXGBE_LINKS_SPEED_1G_82599:
                    return 1000;
                case IXGBE_LINKS_SPEED_10G_82599:
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

    	//receiveの初期化
   	 init_rx(ix_dev);

         //transmitの初期化
    	init_tx(ix_dev);

    	uint16_t i;
    	//rx and tx queueの初期化
    	for(i=0;i<ix_dev->num_rx_queues;i++){
        	start_rx_queue(ix_dev,i);
    	}

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
   if(rx_queues > MAX_QUEUES){
       debug("Rx queues %d exceed MAX_QUEUES",rx_queues);
   }
   if(tx_queues > MAX_QUEUES){
      debug("Tx queues %d exceed MAX_QUEUES",tx_queues);
   }

   struct ixgbe_device *ix_dev = (struct ixgbe_device*)malloc(sizeof(struct ixgbe_device));
   //strdup()->文字列をコピーして返す
   ix_dev->pci_addr = strdup(pci_addr);
   info("pci addr %s",ix_dev->pci_addr);
   ix_dev->num_rx_queues = rx_queues;
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
    
    ix_dev->rx_queues = calloc(rx_queues,sizeof(struct rx_queue) + sizeof(void *) * MAX_RX_QUEUE_ENTRIES);
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

