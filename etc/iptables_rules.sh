#!/bin/sh
if [ "$#" -ne 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 [add/remove] [qnum]" >&2
    exit 1
fi

case $2 in
    ''|*[!0-9]*) exit 1 ;;
esac

if [ "$2" -lt 0 ] || [ "$2" -gt 65535 ]; then
    exit 1
fi

ipset="/sbin/ipset"
iptables="/sbin/iptables"
ipset_name="warum_no_localnets"
qnum="$2"

add() {
    "${ipset}" create "${ipset_name}" hash:net
    for i in 127.0.0.1-127.255.255.255 10.0.0.0-10.255.255.255 192.168.0.0-192.168.255.255 172.16.0.0-172.31.255.255 169.254.0.0-169.254.255.255; do
        "${ipset}" add "${ipset_name}" "$i"
    done
    "${iptables}" -A INPUT \
                -p tcp --tcp-flags SYN,ACK SYN,ACK \
                --sport 443 \
                -m set \! --match-set "$ipset_name" src \
                -m u32 --u32 "2&0xFFFF=0x0:0xF" \
                -j NFQUEUE --queue-num "${qnum}" --queue-bypass
}

remove() {
    "${iptables}" -D INPUT \
                -p tcp --tcp-flags SYN,ACK SYN,ACK \
                --sport 443 \
                -m set \! --match-set "$ipset_name" src \
                -m u32 --u32 "2&0xFFFF=0x0:0xF" \
                -j NFQUEUE --queue-num "${qnum}" --queue-bypass
    "${ipset}" destroy "${ipset_name}"
}

if [ "$1" = "add" ]; then
    remove >/dev/null 2>&1
    set -e
    add
elif [ "$1" = "remove" ]; then
    remove
fi
