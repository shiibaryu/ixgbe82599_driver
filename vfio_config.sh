#!/usr/bin/bash

PCI_ADDRESS1="0000:03:00.0"
PCI_ADDRESS2="0000:03:00.1"
VENDOR_ID="8086"
DEVICE_ID="154d"
USER="siiba"

lspci -nn | grep Ether
echo $PCI_ADDRESS1 > /sys/bus/pci/devices/$PCI_ADDRESS1/driver/unbind
echo $PCI_ADDRESS2 > /sys/bus/pci/devices/$PCI_ADDRESS2/driver/unbind
modprobe vfio-pci
echo $VENDOR_ID $DEVICE_ID > /sys/bus/pci/drivers/vfio-pci/new_id
chown $USER /dev/vfio/

