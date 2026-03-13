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

#include "gestion_gpio.h"

#define INPUT_FIFO_PATH "appToServer"
#define OUTPUT_FIFO_PATH "serverToApp"

#define NUMBER_OF_BINARY_INPUT 3
#define NUMBER_OF_BINARY_OUTPUT 3
#define NUMBER_OF_ANALOG_OUTPUT 3
#define NUMBER_OF_ANALOG_INPUT 0
#define NUMBER_OF_SCHEDULE 4

#define NUMBER_OF_OBJECTS NUMBER_OF_SCHEDULE + NUMBER_OF_BINARY_INPUT + NUMBER_OF_BINARY_OUTPUT + NUMBER_OF_ANALOG_OUTPUT + NUMBER_OF_ANALOG_INPUT

void exitWithError(void) {
    perror("\r\nAn error occured");
    exit(EXIT_FAILURE);
}

struct objectData {
    enum {ANALOG_SCHEDULE, BINARY_SCHEDULE, BINARY_OUTPUT, BINARY_INPUT, ANALOG_INPUT, ANALOG_OUTPUT} typeOfObject : 3;
    int instanceOfObject : 28;
    bool CoV : 1;

    union {
        bool binary;
        float analog;
    } value;
};

struct {
    int nObject;
    struct objectData listOfObjectData[NUMBER_OF_OBJECTS];
} BACnetData = {NUMBER_OF_OBJECTS, {0}};

static void getNewData(int __outputFd) {
    for (uint8_t index = 0; index < NUMBER_OF_OBJECTS; index++) {
        /* Get new object data */
        if (-1 == read(__outputFd, &BACnetData.listOfObjectData[index], sizeof(struct objectData))) {
            printf("Impossible to read from FIFO %s\r\n", strerror(errno));
        }
    }
}

static void sendNewData(int __nObjectToWrite, int __intputFd) {
    for (uint8_t index = 0; index < __nObjectToWrite; index++) {
        /* Send new object data */
        if (-1 == write(__intputFd, &BACnetData.listOfObjectData[index], sizeof(struct objectData))) {
            printf("Impossible to write to FIFO %s\r\n", strerror(errno));
        }
    }
}

int main() {
    int inputFd = -1;
    int outputFd = -1;
    if (-1 == (outputFd = open(OUTPUT_FIFO_PATH, O_RDONLY))) {
        exitWithError();
    }
    if (-1 == (inputFd = open(INPUT_FIFO_PATH, O_WRONLY))) {
        exitWithError();
    }

    /* get new data from server */
    getNewData(outputFd);

    for (int index = 0; index < NUMBER_OF_OBJECTS; index++) {
        struct objectData BACnetObject = BACnetData.listOfObjectData[index];
        switch (BACnetObject.typeOfObject) {
            case ANALOG_SCHEDULE:
                printf("Schedule Object %d -> %d\r\n", BACnetObject.instanceOfObject, BACnetObject.value.binary);
                break;
            case BINARY_SCHEDULE:
                printf("Schedule Object %d -> %d\r\n", BACnetObject.instanceOfObject, BACnetObject.value.binary);
                break;
            case ANALOG_OUTPUT:
                printf("Analog Output Object %d -> %f\r\n", BACnetObject.instanceOfObject, BACnetObject.value.analog);
                break;
            case BINARY_OUTPUT:
                gpio_write_output_value(BACnetObject.instanceOfObject, BACnetObject.value.binary);
                printf("Binary Output Object %d -> %d\r\n", BACnetObject.instanceOfObject, BACnetObject.value.binary);
                break;
            case ANALOG_INPUT:
                printf("Analog Input Object %d -> %f\r\n", BACnetObject.instanceOfObject, BACnetObject.value.analog);
                break;
            case BINARY_INPUT:
                bool inputValue = gpio_read_input_value(BACnetObject.instanceOfObject);
                if (inputValue != BACnetObject.value.binary) {
                    BACnetObject.value.binary = inputValue;
                    BACnetObject.CoV = true;
                } else {
                    BACnetObject.CoV = false;
                }
                printf("Binary Input Object %d -> %d\r\n", BACnetObject.instanceOfObject, BACnetObject.value.binary);
                break;
            default:
                break;
        }
    }

    /* send new data to server */
    sendNewData(NUMBER_OF_OBJECTS, inputFd);

    if (-1 == close(inputFd)) {
        exitWithError();
    }
    if (-1 == close(outputFd)) {
        exitWithError();
    }

    exit(EXIT_SUCCESS);
}
