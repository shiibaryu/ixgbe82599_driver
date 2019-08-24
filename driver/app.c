/*
start_ixgbe()
これによって、初期化とvfioのマップとかもできる
そのあとパケット送信のtx・rx batch関数でパケット送信・受信
*/
#include <stdio.h>
#include <unistd.h>

#include "lib.h"
#include "ixgbe.h"
#include "struct.h"
#include "vfio.h"

void pkt_fwd(struct ixgbe_device *ix_dev1,uint32_t rx_num,struct ixgbe_device *ix_dev2,uint32_t tx_num)
{
    //パケットの送受信と可視化
    rx_batch(ix_dev1,,,rx_num);
    tx_batch(ix_dev2,,tx_num);
}
int main(int argc,char *argv[])
{
    if(argc != 3){
            printf("Usage: %s <pci bus id2> <pci bus id1>\n",argv[0]);
            return -1;
    }
    //初期化全部
    struct ixgbe_device *ix_dev1 = go_ixgbe(argv[1],1,1);
    struct ixgbe_device *ix_dev2 = go_ixgbe(argv[2],1,1);

    uint64_t counter = 0;
    while(true){
            pkt_forward(ix_dev1,0,ix_dev2,0);
            forward(ix_dev2,0,ix_dev1,0);
    }
    reutrn 0;
}

