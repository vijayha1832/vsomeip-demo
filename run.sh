#!/bin/bash
# Usage:
#   On the service VM:  ./run.sh service <CLIENT_VM_IP>
#   On the client VM:   ./run.sh client  <SERVICE_VM_IP>
#
# The peer IP is used to find the correct NIC when the VM has multiple interfaces
# (e.g. a QEMU NAT NIC alongside the shared network NIC).

set -e

ROLE=$1
PEER_IP=$2
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [[ "$ROLE" != "service" && "$ROLE" != "client" ]]; then
    echo "Usage: $0 service|client <PEER_VM_IP>"
    exit 1
fi

if [[ -z "$PEER_IP" ]]; then
    echo "ERROR: peer VM IP is required as the second argument."
    echo "  Service VM example: ./run.sh service 192.168.100.12"
    echo "  Client VM example:  ./run.sh client  192.168.100.11"
    exit 1
fi

# Use ip route get to find exactly which NIC and src IP reach the peer —
# this avoids picking a QEMU NAT interface (10.0.2.x) when two NICs exist.
ROUTE_INFO=$(ip route get "$PEER_IP" 2>/dev/null)
if [[ -z "$ROUTE_INFO" ]]; then
    echo "ERROR: cannot determine route to $PEER_IP — check network connectivity."
    exit 1
fi
MY_IFACE=$(echo "$ROUTE_INFO" | awk 'NR==1{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')
MY_IP=$(echo "$ROUTE_INFO"    | awk 'NR==1{for(i=1;i<=NF;i++) if($i=="src") print $(i+1)}')

echo "Detected: IP=$MY_IP  interface=$MY_IFACE  role=$ROLE"

# Add multicast route on the correct interface — remove any existing one first
if ip route show | grep -q "224\.0\.0\.0/4"; then
    EXISTING_IFACE=$(ip route show | awk '/224\.0\.0\.0\/4/{print $3}')
    if [[ "$EXISTING_IFACE" != "$MY_IFACE" ]]; then
        echo "Removing multicast route from wrong interface ($EXISTING_IFACE)..."
        sudo ip route del 224.0.0.0/4 dev "$EXISTING_IFACE"
        sudo ip route add 224.0.0.0/4 dev "$MY_IFACE"
    fi
else
    echo "Adding multicast route on $MY_IFACE..."
    sudo ip route add 224.0.0.0/4 dev "$MY_IFACE"
fi

# Enable multicast on the interface
sudo ip link set "$MY_IFACE" multicast on

# Open required UDP ports (30490 = SD multicast, 30509 = service data)
if command -v ufw &>/dev/null && sudo ufw status | grep -q "Status: active"; then
    echo "Opening firewall ports..."
    sudo ufw allow 30490/udp 2>/dev/null || true
    sudo ufw allow 30509/udp 2>/dev/null || true
fi

# Build a patched config in a temp file
TMP_CFG="/tmp/vsomeip_${ROLE}_$$.json"
if [[ "$ROLE" == "service" ]]; then
    sed "s/SERVICE_VM_IP/$MY_IP/g" "$SCRIPT_DIR/config/service.json" > "$TMP_CFG"
    BINARY="$BUILD_DIR/someip_service"
else
    sed -e "s/CLIENT_VM_IP/$MY_IP/g" \
        -e "s/SERVICE_VM_IP/$PEER_IP/g" \
        "$SCRIPT_DIR/config/client.json" > "$TMP_CFG"
    BINARY="$BUILD_DIR/someip_client"
fi

trap "rm -f $TMP_CFG" EXIT

VSOMEIP_CONFIGURATION="$TMP_CFG" "$BINARY"
