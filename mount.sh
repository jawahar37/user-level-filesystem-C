mkdir /tmp/rufs42/
mkdir /tmp/rufs42/mountdir

make

./rufs -s -d /tmp/rufs42/mountdir
findmnt | grep 'rufs42'