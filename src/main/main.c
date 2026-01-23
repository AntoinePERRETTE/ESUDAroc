#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_FIFO_PATH "serverToApp_bacnet"
#define OUTPUT_FIFO_PATH "appToServer_bacnet"

void exitWithError(void) {
    perror("\r\nAn error occured");
    exit(EXIT_FAILURE);
}

struct dataPacket {
    enum {SCHEDULE, BINARY_OUTPUT, BINARY_INPUT, ANALOG_INPUT, ANALOG_OUTPUT} typeOfObject;
    uint32_t instanceOfObject;
    enum {ENUMERATED, REAL} tagOfObject;

    
    union sendablePresentValue {
        bool binary;
        struct sendableFloat {
            uint32_t mantissa;
            uint32_t exponent;
        } analog;
    } value;
};

int main() {
    int inputFd = -1;
    int outputFd = -1;
    if (-1 == (inputFd = open(INPUT_FIFO_PATH, O_RDONLY))) {
        exitWithError();
    }
    if (-1 == (outputFd = open(OUTPUT_FIFO_PATH, O_WRONLY))) {
        exitWithError();
    }

    struct dataPacket newBinaryInput, newAnalogOutput;
    newBinaryInput.typeOfObject = BINARY_INPUT;
    newBinaryInput.instanceOfObject = 0;
    newBinaryInput.tagOfObject = ENUMERATED;

    while(1) {
        /* Get new object data */
        if (-1 == read(inputFd, &newAnalogOutput, sizeof(newAnalogOutput))) {
            exitWithError();
        }

        /* Update Analog Output */
        /* Update Binary Input */
        newBinaryInput.value.binary ^= true;

        /* Send new object data */
        if (-1 == write(outputFd, &newBinaryInput, sizeof(newBinaryInput))) {
            exitWithError();
        }
    }
    if (-1 == close(inputFd)) {
        exitWithError();
    }
    if (-1 == close(outputFd)) {
        exitWithError();
    }

    exit(EXIT_SUCCESS);
}
