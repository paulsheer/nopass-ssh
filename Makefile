

nopass-ssh: nopass-ssh.c
	gcc -Os -static -s -fomit-frame-pointer -o nopass-ssh nopass-ssh.c -Wall -Wextra -lutil
	@echo Success
	@echo ''
	@echo Run:
	@echo '   ./nopass-ssh -h'


README: nopass-ssh
	./nopass-ssh -h | sed -e 's/^.*/    \0/' > README

