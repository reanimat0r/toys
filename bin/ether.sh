#!/bin/sh
# etherpad-lite command line utility
# author pancake<nopcode.org>
# date 2011-11-16

DB=var/dirty.sqlite
LOG=var/log.txt

q() {
	echo "$@" | sqlite3 ${DB}
}

case "$1" in
u|users)
	for a in $(sh $0 ls) ; do
		echo "=> $a"
		q "select value from store where key='pad:$a';" | \
			sed -e 's,\[",\n,g' -e 's,},\n,g' | grep -e '^author' | \
			cut -d '"' -f 3
	done
	;;
clean-users)
	used=$(
	for a in $(sh $0 ls) ; do
		q "select value from store where key='pad:$a';" | \
			sed -e 's,\[",\n,g' -e 's,},\n,g' | grep -e '^author' | \
			cut -d '"' -f 3
	done | sort | uniq)
	echo "==== users ===="
	q "select * from store where key like '%uthor:%';" | cut -d : -f 2- | sed -e 's,|, -> ,'
	all=$(q "select key from store where key like 'globalAuthor:%';" | cut -d : -f 2)
	for a in $all ; do
		f=0
		for b in $used ; do
			if [ "$a" = "$b" ]; then
				f=1
				break
			fi
		done
		if [ $f = 0 ]; then
			sh $0 rmuser $a
		fi
	done
	;;
rmuser)
	if [ -n "$2" ]; then
		q "delete from store where value = '\"$2\"';"
		q "delete from store where key = 'globalAuthor:$2';"
		echo "user $2 removed"
	else
		echo "Usage: $0 rmuser [userid]"
	fi
	;;
start)
	if (sh $0 check > /dev/null); then
		echo already running
		exit 1
	else
		echo starting...
		sed -e 's,bin/install,#bin/install,' bin/run.sh > bin/run.sh.x
		sh bin/run.sh.x > ${LOG} &
		sleep 2
	fi
	;;
run)
	sed -e 's,bin/install,#bin/install,' bin/run.sh > bin/run.sh.x
	sh bin/run.sh.x
	;;
stop)
	if (sh $0 check > /dev/null); then
		echo stopping...
		pkill -INT -f "node server.js"
		sleep 2
	else
		echo already running
		exit 1
	fi
	;;
check)
	pgrep -f "node server.js" > /dev/null
	r=$?
	if [ $r = 0 ]; then
		echo "running"
	else
		echo "stopped"
	fi
	exit $r
	;;
restart)
	sh $0 stop
	sh $0 start
	;;
ls|list)
	q "select key from store where key like 'pad2readonly:%';" | cut -d : -f 2
	;;
revs)
	n="$2"
	if [ -n "$n" ]; then
		q "select key from store where key like 'pad:$n:revs:%';" 
	else
		echo "Usage $0 revs [padname]"
	fi
	;;
rm)
	n="$2"
	if [ -n "$n" ]; then
		q "delete from store where key like 'pad:$n%';" 
		q "delete from store where key like 'pad2readonly:$n';" 
	else
		echo "Usage $0 rm [padname]"
	fi
	;;
cat)
	q "select value from store where key='pad:$2';" | \
		sed -e 's,\\n,\n,g' | \
		sed -e 's,\\\\,\\,g' | \
		sed -e 's,{"atext":{"text":",,' | \
		sed -e 's/","attribs":.*//'
	;;
clean)
	sh $0 check >/dev/null
	r=$?
	[ $r = 0 ] && sh $0 stop
	printf " + removed pad revisions: "
	q "select count(key) from store where key like 'pad:%:revs:%';"
	q "delete from store where key like 'pad:%:revs:%';"

	printf " + removed chat entries: "
	q "select count(key) from store where key like 'pad:%:chat:%';"
	q "delete from store where key like 'pad:%:chat:%';"

	printf " + remove empty pads: "
	q "select count(key) from store where value like '%Welcome to Etherpad Lite%';"
	p=$(q "select key from store where value like '%Welcome to Etherpad Lite%';" | cut -d : -f 2)
	for a in $p ; do
		q "delete from store where key = 'pad2readonly:$a';"
	done
	q "delete from store where value like '%Welcome to Etherpad Lite%';"
	[ $r = 0 ] && sh $0 start
	# we cant remove the author information
	#q "delete from store where key like '%uthor:%';"
	;;
*)
	echo "Usage: $0 [cmd] [arg]"
	echo "   start/stop  start or stop etherpad-lite server"
	echo "   restart     stop and start the server"
	echo "   ls          list all pads"
	echo "   revs [pad]  list revisions of given pad"
	echo "   cat [pad]   show contents of pad"
	echo "   rm [pad]    remove pad by name"
	echo "   rmuser [id] remove specific user"
	echo "   users       list all users used in pads"
	echo "   clean       remove all revisions, author and chats"
	echo "   clean-users remove all lost users # TODO: merge into clean"
	;;
esac
