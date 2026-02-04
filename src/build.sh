#!/usr/bin/sh
#

echo "building BACnet stack & server\r\n"
cd bacnet
make server-mini
echo "copying BACnet server into /bin\r\n"
cp /bin/bacmini ../bin/bacmini
cd ..

echo "building app\r\n"
cd main
gcc -Wall -Wextra -Werror -c gestion_gpio.c
gcc -Wall -Wextra -Werror main.c gestion_gpio.o -lgpiod
echo "copying app into /bin\r\n"
cp a.out ../bin/app.out
cd ..
