windres resource.rc -o resource.o
gcc -Wall -Wextra -std=c99 -finput-charset=UTF-8 -fexec-charset=GBK -Os -s -o tun2socks-gui.exe resource.o main.c  -lgdi32 -lcomctl32 -lshell32 -liphlpapi -lws2_32 -mwindows
pause