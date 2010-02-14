#! /bin/sh

PORT=$1

usage() {
	cat <<EOF
usage: $0 <port>
EOF
	exit 1
}

[ -n "$PORT" ] || usage

exec nc -t -l -p $PORT | ./netevent -write
