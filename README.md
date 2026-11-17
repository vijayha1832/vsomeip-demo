# SOME/IP Client-Server Demo

A small C++ demonstration project for SOME/IP communication using vsomeip3.
The client and service run on separate Linux machines or virtual machines.

The project demonstrates SOME/IP Service Discovery, request/response
communication, field notifications, multicast configuration, a CMake-based
build, and runtime network configuration.

## Architecture

```text
Client (192.168.100.12)          Service (192.168.100.11)
          request 0x0421 ------>
                        <------ SERVICE_ECHO response
                        <------ field 0x8001 every 2 seconds

Service ID 0x1234, instance 0x5678, event group 0x0001
```

The client sends `HELLO_FROM_CLIENT_<counter>` every three seconds while the
service is available. The service replies with `SERVICE_ECHO: <request>`.
The field contains `EVENT_DATA_<counter>`. A new subscriber receives the cached
field value once a value has been published. The client pauses its requests
when the service disappears and resumes when it returns.

The echo reply must fit within vSomeIP's 1400-byte UDP payload limit, including
the 14-byte `SERVICE_ECHO: ` prefix. Larger replies receive an `E_NOT_OK` error.

Discovery uses UDP multicast `224.244.224.245:30490`. Requests, responses, and
field notifications use UDP unicast. The service listens on port `30509` and
the client uses port `30510`. Both machines must have bidirectional IPv4
connectivity and support multicast on the shared network. Separate QEMU user
networking (NAT) instances do not provide this shared multicast network.

## Build on both Linux machines

Install a C++17 compiler, CMake, and vSomeIP.
For an Ubuntu release providing Boost 1.75 or newer, build vSomeIP as follows.
Use the same vSomeIP version on both machines.

```bash
sudo apt update
sudo apt install -y git cmake g++ iproute2 \
    libboost-system-dev libboost-thread-dev libboost-log-dev \
    libboost-filesystem-dev libboost-program-options-dev

git clone --depth 1 --branch 3.8.0 https://github.com/COVESA/vsomeip.git ~/vsomeip
cmake -S ~/vsomeip -B ~/vsomeip/build \
    -DCMAKE_BUILD_TYPE=Release
cmake --build ~/vsomeip/build --parallel 2
sudo cmake --install ~/vsomeip/build
sudo ldconfig

git clone https://github.com/vijayha1832/vsomeip-demo.git
cd vsomeip-demo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build --parallel 2
```

Ubuntu 22.04's default Boost 1.74 is below this vSomeIP version's requirement.
Install a supported newer Boost release or use a Linux release that supplies
it before configuring vSomeIP.

## Run on two machines

Run these commands from the `vsomeip-demo` directory on each machine.
Replace the example addresses with the addresses on your shared network.

On the service machine at `192.168.100.11` run the following.

```bash
./run.sh service 192.168.100.12
```

On the client machine at `192.168.100.12` run the following.

```bash
./run.sh client 192.168.100.11
```

The peer address selects the local source IP and network interface using
`ip -4 route get`. The launcher writes a temporary JSON configuration, routes
only the discovery multicast group through that interface, and enables
multicast on the interface. It uses sudo for these network operations and for
firewall rules when UFW is active. The application runs as the invoking user.
Network routes and firewall rules persist after exit.

With an active UFW firewall, the launcher permits inbound UDP from the peer
on port `30490` and the role's data port. If outgoing traffic is restricted,
also allow discovery traffic to the multicast group and peer, and data traffic
to the peer's data port. Configure equivalent rules for other firewalls.

Press Ctrl+C to stop. Both programs block SIGINT and SIGTERM before creating
vSomeIP threads and wait for them on a dedicated thread that stops the event
loop. They then wake and join their worker threads.
The launcher forwards termination to the application and removes its temporary
configuration. Starting the client before the service is supported.

## Optional QEMU setup

The following example uses two existing VM disk images and cloud-init seed
images on a Linux host. Create the shared bridge before starting either VM.

```bash
sudo ip link add br0 type bridge
sudo ip addr add 192.168.100.1/24 dev br0
sudo ip link set br0 up

sudo ip tuntap add tap0 mode tap
sudo ip link set tap0 master br0
sudo ip link set tap0 up

sudo ip tuntap add tap1 mode tap
sudo ip link set tap1 master br0
sudo ip link set tap1 up
```

Start the service VM in one host terminal.

```bash
sudo qemu-system-x86_64 \
    -enable-kvm -cpu host -machine q35 -m 10G -smp 10 \
    -drive if=virtio,file=vm1.img \
    -drive if=virtio,format=raw,file=seed.img \
    -netdev tap,id=lan,ifname=tap0,script=no,downscript=no \
    -device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:11 \
    -netdev user,id=internet \
    -device virtio-net-pci,netdev=internet,mac=52:54:00:00:01:11 \
    -nographic
```

Start the client VM in another host terminal.

```bash
sudo qemu-system-x86_64 \
    -enable-kvm -cpu host -machine q35 -m 10G -smp 10 \
    -drive if=virtio,file=vm2.img \
    -drive if=virtio,format=raw,file=seed_vm2.img \
    -netdev tap,id=lan,ifname=tap1,script=no,downscript=no \
    -device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:12 \
    -netdev user,id=internet \
    -device virtio-net-pci,netdev=internet,mac=52:54:00:00:01:12 \
    -nographic
```

Inside each guest, identify the LAN interface using `ip -br link`. The examples
below assume it is `enp0s2`. Assign one address per guest.

On the service VM run the following.

```bash
sudo ip addr add 192.168.100.11/24 dev enp0s2
sudo ip link set enp0s2 up
ping -c 3 192.168.100.12
```

On the client VM run the following.

```bash
sudo ip addr add 192.168.100.12/24 dev enp0s2
sudo ip link set enp0s2 up
ping -c 3 192.168.100.11
```

Configure DHCP on each guest's internet interface through its network manager
if internet access is required for installation. The launcher selects the LAN
interface from the route to the peer. These `ip` commands configure runtime
addresses, so use the guest's network configuration for persistence.
