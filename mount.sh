mkdir /tmp/rufs42/
mkdir /tmp/rufs42/mountdir

rm DISKFILE # manually removing diskfile here

make

./rufs -s -f /tmp/rufs42/mountdir
findmnt | grep 'rufs42'