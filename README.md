# ixgbe82599_driver
This is a userspace driver of intel 10G NIC (ixgbe 82599)

## how to use
<br>
1. Configure 2MB hugepages in /mnt/huge
<br>
2<br>
.tx
sudo make <br>
./through <bus number><size of tx(bytes)>
<br>
.rx
sudo make<br>
./recv <bus number>
