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

//PHYとLINKの設定
void init_link(struct ixgbe_device *ix_dev)
{
    //BX/SGMII LINKのセットアップ
    //AUTOCのなかのLink mode select fieldを適切な運用モードに
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_LMS_MASK) | IXGBE_AUTOC_LMS_10G_SERIAL);
    set_reg32(ix_dev->addr,IXGBE_AUTOC,(get_reg32(ix_dev->addr,IXGBE_AUTOC) & ~IXGBE_AUTOC_10G_PMA_PMF_MASK) | IXGBE_AUTOC_10G_XAUI);
    set_reg32(ix_dev->addr,IXGBE_AUTOC,get_reg32(ix_dev->addr,IXGBE_AUTOC) | IXGBE_AUTOC_AN_RESTART);
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
     //rxのreceiveを最初に無効化
    unset_reg32(ix_dev->addr,IXGBE_RXCTRL,IXGBE_RXCTRL_RXEN);

    //wait_set_reg32(ix_dev->addr,IXGBE_EEC,IXGBE_EEC_ARD);
    //Receive DMA Control Register,Mac Core Control 0 register
    if(get_reg32(ix_dev->addr,IXGBE_RDRXCTL) !=  get_reg32(ix_dev->addr,IXGBE_HLREG0)){
            set_reg32(ix_dev->addr,IXGBE_RDRXCTL,IXGBE_RDRXCTL_CRCSTRIP);
            set_reg32(ix_dev->addr,IXGBE_HLREG0,IXGBE_HLREG0_RXCRCSTRP);
    }

    set_reg32(ix_dev->addr,IXGBE_RXPBSIZE(0),IXGBE_RXPBSIZE_128KB);
    for(i=0;i<8;i++){
            set_reg32(ix_dev->dev,IXGBE_PXPBSIZE(i),0);
    }

    //broadcastとjamboフレームのレジスタセット
    //broadcastのセット FCTRL->filter control register(receive register)
    //BAM -> broadcast accept mode
    set_reg32(ix_dev->dev,IXGBE_FCTRL,IXGBE_FCTRL_BAM);
    //jamboフレーム5000?
    //set_reg32(ixgbe->dev,IXGBE_MAXFRS,5000);
}

void init_rx_queue(struct ixgbe_device *ix_dev)
{
    //ring bufferの確保と初期化
    //それに伴うレジスタの初期化
    //それぞれのdescriptorは16bitのサイズらしい
    //receive descriptor listのメモリ割り当て
    
    //bufferの割り当てとそこへのポインタ
    //Allocate a region of memory for the descriptor list
    //ということはbufferを割り当て、そこのアドレスをpkt_buffのポインタに入れる
    //じゃあまずはbufferをわりあてようか
    //bufferを割り当てるには、キューのサイズと個数でかける？
     
    
    //baseアドレスの計算
    set_reg32(ix_dev->addr,IXGBE_RDH(),);
    set_reg32(ix_dev->addr,IXGBE_RDT(),);

    //長さの計算
    set_reg32(ix_dev->addr,IXGBE_RDLEN(),);

    //headもtailも先頭
    set_reg32(ix_dev->addr,IXGBE_RDH(0),0);
    set_reg32(ix_dev->addr,IXGBE_RDT(0),0);


}

void init_rx(struct ixgbe_device *ix_dev)
{
    //初期化処理
    //receive address registerはEEPROMのなかにあるから初期化処理もうされてる？
    init_rx_reg(ix_dev);
    
    //memoryとかdescriptorとかの初期化というか準備・allocation
    init_rx_queue(ix_dev);
}

void init_tx(struct ixgbe_device *ix_dev)
{
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
    init_receive(ix_dev);

    //transmitの初期化
    init_receive(ix_dev);

    //割り込みの許可
    
}

