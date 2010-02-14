#! /bin/sh

DEVICE=$1
HOST=$2
PORT=$3
TOGFILE=$4
ONTOG=$5

usage() {
	cat <<EOF
usage: $0 <device> <host> <port>
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

TOGARG=""
if [ -n "$TOGFILE" ]; then
  TOGARG="-toggler \"$TOGFILE\""
  mkfifo "$TOGFILE" || die "Failed to create fifo"
fi

ONTOGARG=""
if [ -n "$ONTOG" ]; then
  ONTOGARG="-ontoggle \"$ONTOG\""
fi

sleep 1

exec ./netevent $TOGARG $ONTOGARG -read "$DEVICE" | nc -t $HOST $PORT
