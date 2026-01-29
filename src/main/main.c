#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define INPUT_FIFO_PATH "appToServer"
#define OUTPUT_FIFO_PATH "serverToApp"

#define NUMBER_OF_INPUT 3
#define NUMBER_OF_OUTPUT 10
#define NUMBER_OF_BINARY_INPUT NUMBER_OF_INPUT
#define NUMBER_OF_BINARY_OUTPUT 3
#define NUMBER_OF_ANALOG_OUTPUT 3
#define NUMBER_OF_SCHEDULE 4

void exitWithError(void) {
    perror("\r\nAn error occured");
    exit(EXIT_FAILURE);
}

struct dataPacket {
    enum {SCHEDULE, BINARY_OUTPUT, BINARY_INPUT, ANALOG_INPUT, ANALOG_OUTPUT} typeOfObject;
    uint32_t instanceOfObject;
    enum {ENUMERATED, REAL} tagOfObject;

    union {
        bool binary;
        float analog;
    } value;
};

int main() {
    int inputFd = -1;
    int outputFd = -1;
    if (-1 == (outputFd = open(OUTPUT_FIFO_PATH, O_RDONLY))) {
        exitWithError();
    }
    if (-1 == (inputFd = open(INPUT_FIFO_PATH, O_WRONLY))) {
        exitWithError();
    }

    struct dataPacket newDataInput[NUMBER_OF_INPUT], newDataOutput[NUMBER_OF_OUTPUT];

    newDataInput[0].typeOfObject = BINARY_INPUT;
    newDataInput[0].instanceOfObject = 0;
    newDataInput[1].typeOfObject = BINARY_INPUT;
    newDataInput[1].instanceOfObject = 1;
    newDataInput[2].typeOfObject = BINARY_INPUT;
    newDataInput[2].instanceOfObject = 2;

    while(1) {
        printf("\r\n------ Output Value ------\r\n");
        for (uint8_t i = 0; i < NUMBER_OF_OUTPUT; i++) {
            /* Get new object data */
            if (-1 == read(outputFd, &newDataOutput[i], sizeof(newDataOutput[i]))) {
                printf("Impossible to read from FIFO %s\r\n", strerror(errno));
            }

            switch (newDataOutput[i].typeOfObject) {
                case BINARY_OUTPUT:
                    printf("Binary Object %d -> %d\r\n", newDataOutput[i].instanceOfObject, newDataOutput[i].value.binary);
                    break;
                case ANALOG_OUTPUT:
                    printf("Analog Object %d -> %f\r\n", newDataOutput[i].instanceOfObject, newDataOutput[i].value.analog);
                    break;
                case SCHEDULE:
                    printf("Schedule Object %d ", newDataOutput[i].instanceOfObject);
                    if (newDataOutput[i].tagOfObject == REAL) {
                        printf("(REAL) -> %f\r\n", newDataOutput[i].value.analog);
                    } else {
                        printf("(ENUMERATED) -> %d\r\n", newDataOutput[i].value.binary);
                    }
                    break;
                default:
                    break;
            }
        }

        /* Update input data */
        newDataInput[0].value.binary ^= true;
        newDataInput[1].value.binary ^= newDataInput[0].value.binary;
        newDataInput[2].value.binary ^= newDataInput[1].value.binary;

        printf("\r\n------ Input Value ------\r\n");
        printf("Binary Object %d -> %d\r\n", newDataInput[0].instanceOfObject, newDataInput[0].value.binary);
        printf("Binary Object %d -> %d\r\n", newDataInput[1].instanceOfObject, newDataInput[1].value.binary);
        printf("Binary Object %d -> %d\r\n", newDataInput[2].instanceOfObject, newDataInput[2].value.binary);

        for (uint8_t i = 0; i < NUMBER_OF_INPUT; i++) {
            /* Send new object data */
            if (-1 == write(inputFd, &newDataInput[i], sizeof(newDataInput[i]))) {
                printf("Impossible to write to FIFO %s\r\n", strerror(errno));
            }
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
