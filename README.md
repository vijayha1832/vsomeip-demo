
# SOME/IP Client-Server Demo

A small C++ demonstration project for SOME/IP communication using
vsomeip3.

## Overview

This project demonstrates communication between a SOME/IP client
and SOME/IP service running on separate Linux virtual machines.

The project demonstrates:

- SOME/IP Service Discovery
- Request/Response communication
- Event notification
- SOME/IP client/server communication
- Multicast configuration
- CMake-based build
- Runtime network configuration

## Architecture

```text
                 SOME/IP
+----------------------+       +----------------------+
|     SOME/IP Client   |       |    SOME/IP Service   |
|                      |       |                      |
| Service ID: 0x1234   | <---> | Service ID: 0x1234   |
| Instance:   0x5678   |       | Instance:   0x5678   |
| Method:     0x0421   |       | Method:     0x0421   |
| Event:      0x8001   | <---- | Event:      0x8001   |
+----------------------+       +----------------------+



sudo qemu-system-x86_64 \
-enable-kvm \
-cpu host \
-machine q35 \
-m 10G \
-smp 10 \
-drive if=virtio,file=vm1.img \
-drive if=virtio,format=raw,file=seed.img \
-netdev tap,id=lan,ifname=tap0,script=no,downscript=no \
-device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:11 \
-netdev user,id=internet \
-device virtio-net-pci,netdev=internet \
-nographic



sudo qemu-system-x86_64 \
-enable-kvm \
-cpu host \
-machine q35 \
-m 10G \
-smp 10 \
-drive if=virtio,file=vm2.img \
-drive if=virtio,format=raw,file=seed_vm2.img \
-netdev tap,id=lan,ifname=tap1,script=no,downscript=no \
-device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:12 \
-netdev user,id=internet \
-device virtio-net-pci,netdev=internet \
-nographic


sudo brctl addbr br0
sudo ip link set br0 up

sudo ip tuntap add tap0 mode tap
sudo ip link set tap0 master br0
sudo ip link set tap0 up

sudo ip tuntap add tap1 mode tap
sudo ip link set tap1 master br0
sudo ip link set tap1 up

sudo ip addr add 192.168.100.1/24 dev br0

sudo ip addr add 192.168.100.11/24 dev enp0s2
sudo ip addr add 192.168.100.12/24 dev enp0s2
sudo ip link set enp0s2 up
sudo ip link set enp0s3 up
ip link show enp0s3
sudo dhclient enp0s3
ip addr show enp0s3



sudo apt update && sudo apt install -y \
    git cmake g++ \
    libboost-system-dev libboost-thread-dev libboost-log-dev \
    libboost-filesystem-dev libboost-program-options-dev
    
    
git clone https://github.com/COVESA/vsomeip.git ~/vsomeip
cd ~/vsomeip
# Lower Boost version requirement for Ubuntu 22.04
sed -i 's/find_package( Boost 1\.75/find_package( Boost 1.74/' CMakeLists.txt
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_SIGNAL_HANDLING=1
make -j$(nproc)
sudo make install && sudo ldconfig 


cd ~/DEMO/c++
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/usr/local
make -j$(nproc)

# Service machine
./run.sh service <CLIENT_IP>

# Client machine
./run.sh client <SERVICE_IP>

 sudo ufw allow 30490/udp
 sudo ufw allow 30509/udp

