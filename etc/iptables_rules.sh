#!/bin/sh
if [ "$#" -gt 3 ] || [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 [add/remove] [qnum] [--firewalld]" >&2
    exit 1
fi

case $2 in
    ''|*[!0-9]*) exit 1 ;;
esac

if [ "$2" -lt 0 ] || [ "$2" -gt 65535 ]; then
    exit 1
fi

qnum="$2"

if [ "$3" = "--firewalld" ]; then
	firewalld="/usr/bin/firewall-cmd"
fi

ipset="/sbin/ipset"
ipset_name="warum_no_localnets"
ipset_type="hash:net"
ipset_ips="127.0.0.1-127.255.255.255 10.0.0.0-10.255.255.255 192.168.0.0-192.168.255.255 172.16.0.0-172.31.255.255 169.254.0.0-169.254.255.255"

iptables="/sbin/iptables"
iptables_args="INPUT \
               -p tcp --tcp-flags SYN,ACK SYN,ACK \
               --sport 443 \
               -m set ! --match-set "${ipset_name}" src \
               -m u32 --u32 2&0xFFFF=0x0:0xF \
               -j NFQUEUE --queue-num "${qnum}" --queue-bypass"

add() {
	if [ -n "$firewalld" ]; then
		"${firewalld}" --permanent --new-ipset="${ipset_name}" --type="${ipset_type}" --family=inet >/dev/null
		for i in $ipset_ips; do
			"${firewalld}" --permanent --ipset="${ipset_name}" --add-entry="$i" >/dev/null
		done
		"${firewalld}" --permanent --direct --add-passthrough ipv4 -A ${iptables_args} >/dev/null
	else
		"${ipset}" create "${ipset_name}" "${ipset_type}"
		for i in $ipset_ips; do
			"${ipset}" add "${ipset_name}" "$i"
		done
		"${iptables}" -A ${iptables_args}
	fi
}

remove() {
	if [ -n "$firewalld" ]; then
		"${firewalld}" --permanent --direct --remove-passthrough ipv4 -A ${iptables_args}
		"${firewalld}" --permanent --delete-ipset="${ipset_name}"
	else
		"${iptables}" -D ${iptables_args}
		"${ipset}" destroy "${ipset_name}"
	fi
}

if [ "$1" = "add" ]; then
	remove >/dev/null 2>&1
	set -e
	add
elif [ "$1" = "remove" ]; then
	remove
fi
