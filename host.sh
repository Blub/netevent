#! /bin/sh

PORT=$1
DEVICE=$2

usage() {
	cat <<EOF
usage: $0 <port> <device>
EOF
}

[ -n "$PORT" ] || usage
[ -e "$DEVICE" ] || usage

exec nc -c -t -l -p $PORT -e "./netevent -read $DEVICE"
