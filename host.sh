#! /bin/sh

HOST=$1
PORT=$2
DEVICE=$3

usage() {
	cat <<EOF
usage: $0 <host> <port> <device>
EOF
	exit 1
}

[ -n "$HOST" ] || usage
[ -n "$PORT" ] || usage
[ -e "$DEVICE" ] || usage

exec ./netevent -read "$DEVICE" | nc -t $HOST $PORT
