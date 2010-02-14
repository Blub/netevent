#! /bin/sh

DEVICE=$1
HOST=$2
PORT=$3
shift 3

ONTOG="-ontoggle"
ONTOGARG=""
TOG=""
TOGARG=""
while [ $# -gt 1 ]; do
	if [ "$1" = "-ontoggle" ]; then
		ONTOG="-ontoggle"
		ONTOGARG="$2"
		shift 2
	fi
	if [ "$1" = "-toggler" ]; then
		TOG="-toggler"
		TOGARG="$2"
		shift 2
	fi
done

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

exec ./netevent $TOG $TOGARG $ONTOG "$ONTOGARG" -read "$DEVICE" | nc -t $HOST $PORT
