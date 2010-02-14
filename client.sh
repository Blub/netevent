#! /bin/sh

DEVICE=$1
HOST=$2
PORT=$3

usage() {
	cat <<EOF
usage: $0 <device> <host> <port>
EOF
	exit 1
}

[ -n "$HOST" ] || usage
[ -n "$PORT" ] || usage
[ -e "$DEVICE" ] || usage

sleep 1

exec ./netevent -read "$DEVICE" | nc -t $HOST $PORT
