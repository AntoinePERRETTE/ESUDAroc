#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_FIFO_PATH "input_from_bacnet"

void exitWithError(void) {
    perror("\r\nAn error occured");
    exit(EXIT_FAILURE);
}

int main() {
    int fd = -1;
    if (-1 == (fd = open(INPUT_FIFO_PATH, O_RDONLY))) {
        exitWithError();
    }

    char msg[10] = {};
    if (-1 == read(fd, msg, 8)) {
        exitWithError();
    }

    if (-1 == close(fd)) {
        exitWithError();
    }

    puts(msg);

    exit(EXIT_SUCCESS);
}
