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
#include <time.h>

#include "gestion_gpio.h"
#include "sun.h"

#define INPUT_FIFO_PATH "appToServer"
#define OUTPUT_FIFO_PATH "serverToApp"

#define NUMBER_OF_BINARY_INPUT 8
#define NUMBER_OF_BINARY_OUTPUT 8
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
    enum {BOOLEAN = 1, REAL = 4} tagOfObject;

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
    /* Get dawn & dusk hour of the sun */
    struct sunData sun = compute_sunData();
    double hourOfDawn = sun.dawn;
    double hourOfDusk = sun.dusk;
    double hourOfDawnWithOffset = sun.dawn;
    double hourOfDuskWithOffset = sun.dusk;
    bool isDuskPass = 0;

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

    for (uint8_t i = 0; i < NUMBER_OF_BINARY_INPUT; i++) {
        BinaryInput[i].instanceOfObject = i;
        BinaryInput[i].typeOfObject = BINARY_INPUT;
    }

    static bool scheduleLastValue[NUMBER_OF_SCHEDULE] = {0};
    static bool BinaryLastValue[NUMBER_OF_BINARY_OUTPUT] = {0};

    time_t now = 0;
    struct tm* localTime = NULL;

    while(1) {
        /* get actual time */
        /* compare with Dawn & Dusk */
        now = time(NULL);
        localTime = localtime(&now);

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

        hourOfDawnWithOffset = hourOfDawn + AnalogOutput[0].value.analog;
        hourOfDuskWithOffset = hourOfDusk + AnalogOutput[1].value.analog;

        /* is Dawn pass? or Dusk? */
        if (localTime->tm_hour > hourOfDawnWithOffset && localTime->tm_min > 60*(hourOfDawnWithOffset-(int)hourOfDawnWithOffset)) {
            isDuskPass = 0;
        }
        if (localTime->tm_hour > hourOfDuskWithOffset && localTime->tm_min > 60*(hourOfDuskWithOffset-(int)hourOfDuskWithOffset)) {
            isDuskPass = 1;
        }
        if (localTime->tm_hour >= 23 && localTime->tm_min >= 59 && localTime->tm_sec >= 50) {
            now = time(NULL);
            /* wait 20 sec */
            while(time(NULL)-now < 20000);
            sun = compute_sunData();
            hourOfDawn = sun.dawn;
            hourOfDusk = sun.dusk;
            printf("Dawn at : %f\r\n", hourOfDawnWithOffset);
            printf("Dusk at : %f\r\n", hourOfDuskWithOffset);
        }

        /* Uncomment to read data from gpio */

        for (uint8_t i = 0; i < NUMBER_OF_BINARY_INPUT; i++) {
            BinaryInput[i].value.binary = gpio_read_input_value(i);
        }

        // for testing purpose
        // BinaryInput[0].value.binary ^= 1;
        // BinaryInput[1].value.binary ^= 1;
        // BinaryInput[2].value.binary ^= 1;

        /* Schedule[N]->PresentValue => BinaryOutput[N]->PresentValue */
        /* output set only if Dawn not passed -> it's Dusk -> lamp can be set on*/

        if (BinaryLastValue[0] != BinaryOutput[0].value.binary) {
            gpio_write_output_value(0, BinaryOutput[0].value.binary);
        } else if (scheduleLastValue[0] != Schedule[0].value.binary) {
            if (Schedule[0].tagOfObject == BOOLEAN) {
                gpio_write_output_value(0, Schedule[0].value.binary & isDuskPass);
            } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");
        }

        if (BinaryLastValue[1] != BinaryOutput[1].value.binary) {
            gpio_write_output_value(1, BinaryOutput[1].value.binary);
        } else if (scheduleLastValue[1] != Schedule[1].value.binary) {
            if (Schedule[1].tagOfObject == BOOLEAN) {
                gpio_write_output_value(1, Schedule[1].value.binary & isDuskPass);
            } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");
        }

        if (BinaryLastValue[2] != BinaryOutput[2].value.binary) {
            gpio_write_output_value(2, BinaryOutput[2].value.binary);
        } else if (scheduleLastValue[2] != Schedule[2].value.binary) {
            if (Schedule[2].tagOfObject == BOOLEAN) {
                gpio_write_output_value(2, Schedule[2].value.binary & isDuskPass);
            } else printf("Error : A output value cannot be set with a Schedule of different tag\r\n");
        }

        // Set all remaining output with Binary Output Objects
        for (uint8_t i = 0; i < NUMBER_OF_BINARY_OUTPUT; i++) {
            gpio_write_output_value(i, BinaryOutput[i].value.binary);
        }

        /* for debugging purpose */
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

        printf("\r\n------  is Dusk passed ?  -------\r\n");
        printf("Dawn at : %f\r\n", hourOfDawnWithOffset);
        printf("Dusk at : %f\r\n", hourOfDuskWithOffset);
        if (isDuskPass) printf("Yes !\r\n");
        else printf("No !\r\n");

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
