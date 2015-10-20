tranche
===

Quick Start
---

```bash
git clone https://github.com/abeconnelly/tranche
cd tranche
./cmp.sh
./bin/tranche -b 15 -s 17 -f example/hello.txt -m example/mnt -p
./example/mnt/fileUKKCfH
```

```bash
cat hello.txt
0123456789012345
hello, friend...

cat example/mnt/fileUKKCfH
5
hello, friend..

cat example/mnt/2+5
hello

cat example/mnt/2:15
hello, friend

cat example/mnt/2:16
hello, friend.

cat example/mnt/2:17
hello, friend..

cat example/mnt/2:18
hello, friend..

touch example/mnt/kill
```

Exposed through the mount point you should have access to a slice of bytes
from the original file.  You can further access subsections of the file through
the mount point via range queries of the form `start:end`, `start-end` or `start+len`
with `end` being non-inclusive.

License
---

AGPLv3
