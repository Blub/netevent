#! /bin/sh

DEVICE=$1
HOST=$2
PORT=$3
shift 3
REST=$@

usage() {
	cat <<EOF
usage: $0 <device> <host> <port> [rest...]
EOF
	exit 1
}

die() {
	echo "$@"
	exit 1
}

[ -n "$HOST" ] || usage
[ -n "$PORT" ] || usage
[ -e "$DEVICE" ] || usage

sleep 1

exec ./netevent $REST -read "$DEVICE" | nc -t $HOST $PORT
