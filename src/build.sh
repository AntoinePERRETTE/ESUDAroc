#!/usr/bin/sh
#

echo "building BACnet stack & server\r\n"
cd bacnet-stack-bacnet-stack-1.4.2
make server-mini
echo "copying BACnet server into bin\r\n"
cp bin/bacmini ../bin/
cd ..

echo "building app\r\n"
cd main
gcc -Wall -Wextra -Werror -c gestion_gpio.c
gcc --std=23 -Wall -Wextra -Werror main.c gestion_gpio.o -lgpiod
echo "copying app into bin\r\n"
cp a.out ../bin/app.out
cd ..
