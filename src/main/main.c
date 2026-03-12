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

#define LINE_INPUT_0 0
#define LINE_INPUT_1 2
#define LINE_INPUT_2 4

#define LINE_OUTPUT_0 0
#define LINE_OUTPUT_1 2
#define LINE_OUTPUT_2 4

#define INPUT_FIFO_PATH "appToServer"
#define OUTPUT_FIFO_PATH "serverToApp"

#define NUMBER_OF_BINARY_INPUT 3
#define NUMBER_OF_BINARY_OUTPUT 3
#define NUMBER_OF_ANALOG_OUTPUT 3
#define NUMBER_OF_SCHEDULE 4

#define NUMBER_OF_INPUT NUMBER_OF_BINARY_INPUT
#define NUMBER_OF_OUTPUT NUMBER_OF_BINARY_OUTPUT + NUMBER_OF_ANALOG_OUTPUT + NUMBER_OF_SCHEDULE

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

static void readNewData(uint8_t __nmemb, struct dataPacket __newData[__nmemb], int __outputFd) {
    for (uint8_t i = 0; i < __nmemb; i++) {
        /* Get new object data */
        if (-1 == read(__outputFd, &__newData[i], sizeof(__newData[i]))) {
            printf("Impossible to read from FIFO %s\r\n", strerror(errno));
        }
    }
}

static void writeNewData(uint8_t __nmemb, struct dataPacket __newData[__nmemb], int __intputFd) {
    for (uint8_t i = 0; i < __nmemb; i++) {
        /* Send new object data */
        if (-1 == write(__intputFd, &__newData[i], sizeof(__newData[i]))) {
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

    struct dataPacket newDataInput[NUMBER_OF_INPUT], newDataOutput[NUMBER_OF_OUTPUT];
    struct dataPacket BinaryOutput[NUMBER_OF_BINARY_OUTPUT], BinaryInput[NUMBER_OF_BINARY_INPUT],
                        AnalogOutput[NUMBER_OF_ANALOG_OUTPUT], Schedule[NUMBER_OF_SCHEDULE];

    BinaryInput[0].typeOfObject = BINARY_INPUT;
    BinaryInput[1].typeOfObject = BINARY_INPUT;
    BinaryInput[2].typeOfObject = BINARY_INPUT;
    BinaryInput[0].instanceOfObject = 0;
    BinaryInput[1].instanceOfObject = 1;
    BinaryInput[2].instanceOfObject = 2;
    BinaryInput[0].value.binary = true;
    BinaryInput[1].value.binary = true;
    BinaryInput[2].value.binary = true;


    BinaryOutput[0].typeOfObject = BINARY_OUTPUT;
    BinaryOutput[1].typeOfObject = BINARY_OUTPUT;
    BinaryOutput[2].typeOfObject = BINARY_OUTPUT;

    AnalogOutput[0].typeOfObject = ANALOG_OUTPUT;
    AnalogOutput[1].typeOfObject = ANALOG_OUTPUT;
    AnalogOutput[2].typeOfObject = ANALOG_OUTPUT;

    Schedule[0].typeOfObject = SCHEDULE;
    Schedule[1].typeOfObject = SCHEDULE;
    Schedule[2].typeOfObject = SCHEDULE;
    Schedule[3].typeOfObject = SCHEDULE;

    while(1) {
        printf("\r\n------ Received new data from the server ------\r\n");
        /* get newData for output object */
        readNewData(NUMBER_OF_OUTPUT, newDataOutput, outputFd);

        /* update object with new data from server*/
        /* array index == instance */
        uint32_t instance = 0;
        for (uint8_t i = 0; i < NUMBER_OF_OUTPUT; i++) {
            switch (newDataOutput[i].typeOfObject) {
                case BINARY_OUTPUT:
                    instance = newDataOutput[i].instanceOfObject;
                    BinaryOutput[instance].instanceOfObject = instance;
                    BinaryOutput[instance].tagOfObject = newDataOutput[i].tagOfObject;
                    BinaryOutput[instance].value.binary = newDataOutput[i].value.binary;

                    break;
                case ANALOG_OUTPUT:
                    instance = newDataOutput[i].instanceOfObject;
                    AnalogOutput[instance].instanceOfObject = instance;
                    AnalogOutput[instance].tagOfObject = newDataOutput[i].tagOfObject;
                    AnalogOutput[instance].value.analog = newDataOutput[i].value.analog;

                    break;
                case SCHEDULE:
                    instance = newDataOutput[i].instanceOfObject;
                    Schedule[instance].instanceOfObject = instance;
                    Schedule[instance].tagOfObject = newDataOutput[i].tagOfObject;
                    if (Schedule[instance].tagOfObject == REAL) {
                        Schedule[instance].value.analog = newDataOutput[i].value.analog;
                    } else {
                        Schedule[instance].value.binary = newDataOutput[i].value.binary;
                    }

                    break;
                default:
                    break;
            }
        }

        /* Update Value*/
        /* read data from gpio */
        BinaryInput[0].value.binary = gpio_read_input_value(LINE_INPUT_0);
        BinaryInput[1].value.binary = gpio_read_input_value(LINE_INPUT_1);
        BinaryInput[2].value.binary = gpio_read_input_value(LINE_INPUT_2);

        /* Update output value */
        /* Schedule[4] == Astro Calendar */
        /* Schedule[N]->PresentValue => BinaryOutput[N]->PresentValue */
        if (Schedule[0].tagOfObject == ENUMERATED) {
            gpio_write_output_value(LINE_OUTPUT_0, (Schedule[3].value.binary & Schedule[0].value.binary) | BinaryOutput[0].value.binary);
        } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");

        if (Schedule[1].tagOfObject == ENUMERATED) {
            gpio_write_output_value(LINE_OUTPUT_1, (Schedule[3].value.binary & Schedule[1].value.binary) | BinaryOutput[1].value.binary);
        } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");

        if (Schedule[2].tagOfObject == ENUMERATED) {
            gpio_write_output_value(LINE_OUTPUT_2, (Schedule[3].value.binary & Schedule[2].value.binary) | BinaryOutput[2].value.binary);
        } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");

        printf("\r\n------ output value ------\r\n");
        printf("Binary Output Object %d -> %d\r\n", BinaryOutput[0].instanceOfObject, BinaryOutput[0].value.binary);
        printf("Binary Output Object %d -> %d\r\n", BinaryOutput[1].instanceOfObject, BinaryOutput[1].value.binary);
        printf("Binary Output Object %d -> %d\r\n", BinaryOutput[2].instanceOfObject, BinaryOutput[2].value.binary);

        printf("Analog Output Object %d -> %f\r\n", AnalogOutput[0].instanceOfObject, AnalogOutput[0].value.analog);
        printf("Analog Output Object %d -> %f\r\n", AnalogOutput[1].instanceOfObject, AnalogOutput[1].value.analog);
        printf("Analog Output Object %d -> %f\r\n", AnalogOutput[2].instanceOfObject, AnalogOutput[2].value.analog);

        printf("Schedule Object %d -> %d\r\n", Schedule[0].instanceOfObject, Schedule[0].value.binary);
        printf("Schedule Object %d -> %d\r\n", Schedule[1].instanceOfObject, Schedule[1].value.binary);
        printf("Schedule Object %d -> %d\r\n", Schedule[2].instanceOfObject, Schedule[2].value.binary);
        printf("Schedule Object %d -> %d\r\n", Schedule[3].instanceOfObject, Schedule[2].value.binary);

        printf("\r\n------ New value as input ------\r\n");
        printf("Binary Input Object %d -> %d\r\n", BinaryInput[0].instanceOfObject, BinaryInput[0].value.binary);
        printf("Binary Input Object %d -> %d\r\n", BinaryInput[1].instanceOfObject, BinaryInput[1].value.binary);
        printf("Binary Input Object %d -> %d\r\n", BinaryInput[2].instanceOfObject, BinaryInput[2].value.binary);

        /* prepare new data for server */
        for (uint8_t i = 0; i < NUMBER_OF_BINARY_INPUT; i++) {
            newDataInput[i].instanceOfObject = BinaryInput[i].instanceOfObject;
            newDataInput[i].typeOfObject = BinaryInput[i].typeOfObject;
            newDataInput[i].tagOfObject = BinaryInput[i].tagOfObject;
            newDataInput[i].value.binary = BinaryInput[i].value.binary;
        }

        /* send new data to server */
        writeNewData(NUMBER_OF_INPUT, newDataInput, inputFd);
    }

    if (-1 == close(inputFd)) {
        exitWithError();
    }
    if (-1 == close(outputFd)) {
        exitWithError();
    }

    exit(EXIT_SUCCESS);
}
