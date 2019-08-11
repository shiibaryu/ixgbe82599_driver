#include "ixgbe_type.h"
#include "ixgbe_lib"

/*
NICへの割り込み禁止
デバイスのリセットとConfigurationレジスタの設定
DMAの初期化を待つ
PHYとLINKの設定
receiveの初期化
transmitの初期化
割り込みを許可
*/

struct ixgbe_tx_queue{
    volatile union ixgbe_adv_tx_desc *descriptors;
    struct mempool *mempool;
    uint16_t num_entries;
    uint16_t clean_index;
    uint16_t tx_index;
    uintptr_t virtual_address[];
}

struct ixgbe_rx_queue{
    volatile union ixgbe_adv_rx_desc *descriptors;
    struct mempool *mempool
    uint16_t num_entries;
    uint16_t rx_index;
    void *virtual_address[];
}

//PHYとLINKの設定
void init_link(struct ixgbe_device *ix_dev)
{
    //BX/SGMII LINKのセットアップ
    //AUTOCのなかのLink mode select fieldを適切な運用モードに
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_LMS_MASK) | IXGBE_AUTOC_LMS_10G_SERIAL);
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_10G_PMA_PMF_MASK) | IXGBE_AUTOC_10G_XAUI);
    set_flag32(ix_dev->addr,IXGBE_AUTOC,IXGBE_AUTOC_AN_RESTART);
}

//特定のレジスタをreadすると初期化されるっぽい？？？
void init_stats(struct ixgbe_device *ix_dev)
{
    //good packet receive and transmit
    uint32_t rx_pkts = get_reg32(ix_dev->addr,IXGBE_GPRC);
    uint32_t tx_pkts = get_reg32(ix_dev->addr,IXGBE_GPTC);
    //good octet receive and transmit
    uint64_t rx_bytes = get_reg32(ix_dev->addr,IXGBE_GORCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GORCH)) << 32);
    uint64_t tx_bytes = get_reg32(ix_dev->addr,IXGBE_GOTCL) + (((uint64_t)get_reg32(ix_dev->addr,IXGBE_GOTCH)) << 32);

}

void init_rx_reg(struct ixgbe_device *ix_dev)
{
    int i;

    set_reg32(ix_dev->addr,IXGBE_RXPBSIZE(0),IXGBE_RXPBSIZE_128KB);
    for(i=0;i<8;i++){
            set_reg32(ix_dev->addr,IXGBE_PXPBSIZE(i),0);
    }

    set_flag32(ix_dev->addr,IXGBE_HLREG0,IXGBE_HLREG0_RXCRCSTRP);
    set_flag32(ix_dev->addr,IXGBE_RDRXCTL,IXGBE_HLREG0_CRCSTRIP);

    //broadcastとjamboフレームのレジスタセット
    //broadcastのセット FCTRL->filter control register(receive register)
    //BAM -> broadcast accept mode
    set_flag32(ix_dev->addr,IXGBE_FCTRL,IXGBE_FCTRL_BAM);
    //jamboフレーム5000?
    //set_reg32(ixgbe->dev,IXGBE_MAXFRS,5000);
}

void init_rx_queue(struct ixgbe_device *ix_dev)
{
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
        uint32_t ring_size = sizeof(union ixgbe_adv_rx_desc) * NUM_RX_QUEUES;
        struct dma_address dma_addr = allocate_dma_addr(ring_size);
        
        //DMAアクティベーション初期の時、メモリにアクセスされることを防ぐために仮想アドレスを初期化?
        memset(dma_addr.virt_addr,-1,ring_size);

        //baseアドレスの計算
        set_reg32(ix_dev->addr,IXGBE_RDBAL(i),(uint32_t)(0xFFFFFFFFull & dma_addr.phy_addr));
        set_reg32(ix_dev->addr,IXGBE_RDBAH(i),(uint32_t)(dma_addr.phy_addr >> 32);

        //長さの計算
        set_reg32(ix_dev->addr,IXGBE_RDLEN(i),ring_size);

        //headもtailも先頭
        set_reg32(ix_dev->addr,IXGBE_RDH(i),0);
        set_reg32(ix_dev->addr,IXGBE_RDT(i),0);
    }

    //rxをenableする前にやっておくこと
    set_flag32(ix_dev->addr,IXGBE_CTRL_EXT,IXGBE_CTRL_EXT_NS_DIS);
    for(uint16_t i=0;i< ix_dev->num_rx_queue;i++){
            //DCA -> direct cache access
            clear_flag32(ix_dev->addr,IXGBE_DCA_RXCTRL(i), 0<<12);
    }
}

void init_rx(struct ixgbe_device *ix_dev)
{
    //receiveを止める
    unset_flag32(ix_dev->addr,IXGBE_RXCTRL,IXGBE_RXCTRL_RXEN);
    //初期化処理
    //receive address registerはEEPROMのなかにあるから初期化処理もうされてる？
    init_rx_reg(ix_dev);
    
    //memoryとかdescriptorとかの初期化というか準備・allocation
    init_rx_queue(ix_dev);
    //receive再開
    set_flag32(ix_dev->addr,IXGBE_RXCTRL,IXGBE_RXCTRL_RXEN);
}

void init_tx_reg(struct ixgbe_device *ix_dev)
{
    //IXGBE_HLREG0に設定したい内容のフラグを立てるためにorをとって立てる
    //今回だとcrcオフロードとパケットパディング 
    set_flag32(ix_dev->addr,IXGBE_HLREG0,IXGBE_HLREG0_TXCRCEN | IXGBE_HLREG0_TXPADEN);
    //tcpのセグメンテーション設定とか言われてるけどまだ喋れないのでパス
    //毎度毎度のpage size初期化
    int i=0;
    for(i=0;i<8;i++){
        set_flag32(ix_dev->addr,IXGBE_TXPBSIZE(i),0);
    }
    //DCB->data center bridging(データセンターのトランスポートのロスレスのイーサネット拡張らしい
    //つまりいらない？
    //DCBとVT(virtualization)なし
    set_reg32(ix_dev->addr,IXGBE_DTMXSZRQ,0xFFFF);
    //0にしろと書かれていたので
    unset_flag32(ix_dev->addr,IXGBE_RTTDCS,IXGBE_RTTDCS_ARBDIS);
}

void init_tx_queue(struct ixgbe_device *ix_dev)
{
    uint16_t i;
    for(i=0;i<ix_dev->num_tx_queues;i++){
        //キューの割り当て
        //レジスタの初期化
        uint32_t tx_ring = sizeof(union ixgbe_adv_tx_desc)*NUM_TX_QUEUES;
        struct dma_address dma_addr = allocate_dma_address(tx_ring);
        
        //rxの時と同様例のテクニック
        memset(dma_addr.virt_addr,-1,tx_ring);
        
        //baseアドレスの設定
        set_reg32(ix_dev->addr,IXGBE_TDBAL(i),(uint32_t)(dma_addr.phy_addr & 0xFFFFFFFFul));
        set_reg32(ix_dev->addr,IXGBE_TDBAH(i),(uint32_t)(dma_addr.phy_addr >> 32));

        //長さセット
        set_reg32(ix_dev->addr,IXGBE_TDLEN(i),tx_ring);
        set_flag32(ix_dev->addr,IXGBE_TXDCTL(i),IXGBE_TXDCRL_ENABLE);

        //高い性能とpcieの低レイテンシを得るためのtransmit control descriptorのmagic
        //dpdkで使われてるっぽい
        //transmition latencyを抑えるには、PTHRETHはできるだけ高くして、HTHRETHとWTHRESHはできるだけ低くするっぽい
        //pcieのオーバーヘッドを小さくするには、PtHRETHはできるだけ小さく、HTHRETHとWTHRESHはできるだけ大きくらしい
        uint32_t txdctl = get_reg32(ix_dev->addr,IXGBE_TXDCTL(i));
        txdctl &= ~(0x3F | (0x3F << 8) | (0x3F << 16));
        txdctl |= (36 | (8<<8) | (4<<16));
        set_reg32(ix_dev->addr,IXGBE_TXDCTL(i),txdctl);
    }
}
void init_tx(struct ixgbe_device *ix_dev)
{
    init_tx_reg(ix_dev);
    init_tx_queue(ix_dev);
    //enable dma
    set_reg32(ix_dev->addr,IXGBE_DMATXCTL,IXGBE_DMATXCTL_TE);
}
/*ban interrupt for nic*/
void do_init_seq(struct ixgbe_device *ix_dev)
{
    //EIMC registerに書き込めば、割り込み禁止
    set_reg32(ix_dev->addr,IXGBE_EIMC,0x7FFFFFFFF);

    //デバイスの初期化,初期化の後は10msec以上待つ必要があるぽい
    set_reg32(ix_dev->addr,IXGBE_CTRL,IXGBE_CTRL_RST_MASK);
    wait_enable_reset(ix_dev->addr,IXGBE_CTRL,IXGBE_CTRL_RST_MASK);
    //usleep(10000);

    //もう一回割り込み禁止しないとダメっぽい
    set_reg32(ix_dev->addr,IXGBE_EIMC,0x7FFFFFFFF);

    //Genaral Configurationレジスタの設定
    //EEPROMがauto completionされるのを待つっぽい(EEPROM->store product configuration information)
    wait_set_reg32(ix_dev->addr,IXGBE_EEC,IXGBE_EEC_ARD);

    //DMAの初期化ができるのを待つ
    wait_set_reg32(ix_dev->addr,IXGBE_RDRXCTL,IXGBE_RDRXCTL,IXGBE_RDRXCTL_DMAIDONE);
    
    //PHYとLINKの10G用の設定
    init_link(ix_dev);

    //Statisticsの初期化
    init_stats(ix_dev);

    //receiveの初期化
    init_rx(ix_dev);

    //transmitの初期化
    init_tx(ix_dev);

    //割り込みの許可
    
}

