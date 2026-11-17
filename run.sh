#!/usr/bin/env bash
# Usage on each Linux machine: ./run.sh service|client <PEER_IPV4>
set -euo pipefail

if [[ $# -ne 2 || ( "$1" != service && "$1" != client ) ]]; then
    echo "Usage: $0 service|client <PEER_IPV4>" >&2
    exit 1
fi

ROLE=$1
PEER_IP=$2
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
BINARY="$SCRIPT_DIR/build/someip_$ROLE"
SD_GROUP=224.244.224.245

if [[ ! "$PEER_IP" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
    echo "ERROR: peer must be an IPv4 address." >&2
    exit 1
fi
IFS=. read -r -a OCTETS <<< "$PEER_IP"
for OCTET in "${OCTETS[@]}"; do
    if (( 10#$OCTET > 255 )); then
        echo "ERROR: invalid IPv4 address: $PEER_IP" >&2
        exit 1
    fi
done
if (( 10#${OCTETS[0]} == 0 || 10#${OCTETS[0]} >= 224 )); then
    echo "ERROR: peer must be a unicast IPv4 address." >&2
    exit 1
fi

for COMMAND in ip awk sed mktemp; do
    if ! command -v "$COMMAND" >/dev/null; then
        echo "ERROR: required command is missing: $COMMAND" >&2
        exit 1
    fi
done
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: executable missing: $BINARY. Build the project first (see README.md)." >&2
    exit 1
fi

if ! ROUTE_INFO=$(ip -4 route get "$PEER_IP" 2>/dev/null); then
    echo "ERROR: cannot determine route to $PEER_IP." >&2
    exit 1
fi
MY_IFACE=$(awk 'NR==1{for(i=1;i<NF;i++) if($i=="dev") print $(i+1)}' <<< "$ROUTE_INFO")
MY_IP=$(awk 'NR==1{for(i=1;i<NF;i++) if($i=="src") print $(i+1)}' <<< "$ROUTE_INFO")
if [[ -z "$MY_IFACE" || -z "$MY_IP" || "$MY_IFACE" == lo || "$MY_IP" == "$PEER_IP" ]]; then
    echo "ERROR: expected a route to a different machine with a source IP and interface." >&2
    exit 1
fi
echo "Detected: IP=$MY_IP interface=$MY_IFACE role=$ROLE"

PRIVILEGE=()
if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null; then
        echo "ERROR: sudo is required to configure the network." >&2
        exit 1
    fi
    PRIVILEGE=(sudo)
fi

# Route only the discovery group through the NIC that reaches the peer.
"${PRIVILEGE[@]}" ip route replace "$SD_GROUP/32" dev "$MY_IFACE"
"${PRIVILEGE[@]}" ip link set dev "$MY_IFACE" multicast on

if command -v ufw >/dev/null; then
    UFW_STATUS=$("${PRIVILEGE[@]}" ufw status)
    if [[ "$UFW_STATUS" == *"Status: active"* ]]; then
        DATA_PORT=30509
        if [[ "$ROLE" == client ]]; then
            DATA_PORT=30510
        fi
        "${PRIVILEGE[@]}" ufw allow in on "$MY_IFACE" proto udp from "$PEER_IP" to any port 30490
        "${PRIVILEGE[@]}" ufw allow in on "$MY_IFACE" proto udp from "$PEER_IP" to any port "$DATA_PORT"
    fi
fi

TMP_CFG=$(mktemp "${TMPDIR:-/tmp}/vsomeip_${ROLE}.XXXXXX.json")
CHILD_PID=
# Called indirectly by the EXIT trap.
# shellcheck disable=SC2329
cleanup() {
    if [[ -n "$CHILD_PID" ]]; then
        kill -TERM "$CHILD_PID" 2>/dev/null || true
        wait "$CHILD_PID" 2>/dev/null || true
    fi
    rm -f -- "$TMP_CFG"
}
trap 'cleanup' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

sed -e "s/SERVICE_VM_IP/$MY_IP/g" -e "s/CLIENT_VM_IP/$MY_IP/g" \
    "$SCRIPT_DIR/config/$ROLE.json" > "$TMP_CFG"

# Keep the wrapper alive to forward termination and remove the temporary config.
VSOMEIP_CONFIGURATION="$TMP_CFG" "$BINARY" &
CHILD_PID=$!
STATUS=0
wait "$CHILD_PID" || STATUS=$?
CHILD_PID=
exit "$STATUS"
