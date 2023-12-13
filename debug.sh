mkdir /tmp/rufs42/
mkdir /tmp/rufs42/mountdir

make

gdb ./rufs 

# run -s -d -f /tmp/rufs42/mountdir
# fusermount -u /tmp/rufs42/mountdir
# rm -r /tmp/rufs42/