#include <asm-generic/errno-base.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* BACnet Stack includes */
#include "bacnet/apdu.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bacdef.h"
#include "bacnet/bacenum.h"
#include "bacnet/bactext.h"
#include "bacnet/basic/binding/address.h"

#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/ao.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bo.h"
#include "bacnet/basic/object/device.h"

#include "bacnet/basic/object/schedule.h"
/* #include "bacnet/basic/object/trendlog.h"
*/

#include "bacnet/basic/services.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacnet/dcc.h"
#include "bacnet/getevent.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/version.h"

#include "bacnet/basic/service/h_apdu.h"
#include "bacnet/basic/service/h_rp.h"
#include "bacnet/basic/service/h_whois.h"
#include "bacnet/basic/service/h_wp.h"
#include "bacnet/basic/service/s_iam.h"
#include "bacnet/basic/sys/platform.h"

#define INPUT_FIFO_PATH "appToServer"
#define OUTPUT_FIFO_PATH "serverToApp"
#define SCHEDULESAVEFILE "ScheduleSaveFile"

/* Buffers */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* Device week day & time */
static BACNET_WEEKDAY actual_day;
static BACNET_TIME actual_time;
static struct tm calendar_time;

/* BACnet Object Instances */
static uint32_t ao_instance[3] = {0};
static uint32_t bo_instance[3] = {0};
static uint32_t bi_instance[3] = {0};

/* Custom Object Table */
static object_functions_t My_Object_Table[] = {
    /* device object required for all devices */
    { OBJECT_DEVICE,
        NULL,
        Device_Count,
        Device_Index_To_Instance,
        Device_Valid_Object_Instance_Number,
        Device_Object_Name,
        Device_Read_Property_Local,
        Device_Write_Property_Local,
        Device_Property_Lists,
        DeviceGetRRInfo,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL },

    /* Schedule Object */
    { OBJECT_SCHEDULE,
        Schedule_Init,
        Schedule_Count,
        Schedule_Index_To_Instance,
        Schedule_Valid_Instance,
        Schedule_Object_Name,
        Schedule_Read_Property,
        Schedule_Write_Property,
        Schedule_Property_Lists,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL },

    /* Analog Output (Commandable) */
    { OBJECT_ANALOG_OUTPUT,
        Analog_Output_Init,
        Analog_Output_Count,
        Analog_Output_Index_To_Instance,
        Analog_Output_Valid_Instance,
        Analog_Output_Object_Name,
        Analog_Output_Read_Property,
        Analog_Output_Write_Property, /* Allow writes */
        Analog_Output_Property_Lists,
        NULL,
        NULL,
        Analog_Output_Encode_Value_List,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        Analog_Output_Create,
        Analog_Output_Delete,
        NULL },

   /* Binary Input (Read-Only) */
    { OBJECT_BINARY_INPUT,
        Binary_Input_Init,
        Binary_Input_Count,
        Binary_Input_Index_To_Instance,
        Binary_Input_Valid_Instance,
        Binary_Input_Object_Name,
        Binary_Input_Read_Property,
        NULL,                       /* Read-Only */
        Binary_Input_Property_Lists,
        NULL,
        NULL,
        Binary_Input_Encode_Value_List,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        Binary_Input_Create,
        Binary_Input_Delete,
        NULL },

    /* Binary Output (Commandable) */
    { OBJECT_BINARY_OUTPUT,
        Binary_Output_Init,
        Binary_Output_Count,
        Binary_Output_Index_To_Instance,
        Binary_Output_Valid_Instance,
        Binary_Output_Object_Name,
        Binary_Output_Read_Property,
        Binary_Output_Write_Property, /* Allow writes */
        Binary_Output_Property_Lists,
        NULL,
        NULL,
        Binary_Output_Encode_Value_List,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        Binary_Output_Create,
        Binary_Output_Delete,
        NULL },

    { MAX_BACNET_OBJECT_TYPE,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL }
};

/* read every dailyschedule & start/end date for each schedule, from file */
void readScheduleFrom(const int __scheduleSaveFile_fd) {
    BACNET_DAILY_SCHEDULE week[7] = {0};
    BACNET_DATE start_date = {0};
    BACNET_DATE end_date = {0};
    uint8_t instance = 0;
    ssize_t status;

    /* go to start of file */
    lseek(__scheduleSaveFile_fd, 0, SEEK_SET);

    uint8_t i;
    for (i = 0; i < 4; i++) {
        printf("get schedule %d\n", i);

        /* get instance */
        status = read(__scheduleSaveFile_fd, &instance, 1);
        if (-1 == status) {
            printf("Error occured while reading schedule instance in save file : %s\n", strerror(errno));
            return;
        } else if (1 != status) {
            printf("Error occured while reading schedule instance in save file : get incorrect amount of data\n");
            return;
        }

        /* get start & end dates */
        status = read(__scheduleSaveFile_fd, &start_date, sizeof(BACNET_DATE));
        if (-1 == status) {
            printf("Error occured while reading start date of schedule : %s\n", strerror(errno));
            return;
        } else if (sizeof(BACNET_DATE) != status) {
            printf("Error occured while reading start date of schedule : get incorrect amount of data\n");
            return;
        }
        status = read(__scheduleSaveFile_fd, &end_date, sizeof(BACNET_DATE));
        if (-1 == status) {
            printf("Error occured while reading start date of schedule : %s\n", strerror(errno));
            return;
        } else if (sizeof(BACNET_DATE) != status) {
            printf("Error occured while reading end date of schedule : get incorrect amount of data\n");
            return;
        }

        /* set start & end dates */
        if (Schedule_Effective_Period_Set(Schedule_Instance_To_Index(instance), &start_date, &end_date)) {
            printf("Start & end dates set !\n");
        } else {
            printf("Unable to set start & end dates !\n");
        }

        /* get weekly schedule */
        status = read(__scheduleSaveFile_fd, week, 7*sizeof(BACNET_DAILY_SCHEDULE));
        if (-1 == status) {
            printf("Error occured while reading weekly schedule in save file : %s\n", strerror(errno));
            return;
        } else if (7*sizeof(BACNET_DAILY_SCHEDULE) != status) {
            printf("Error occured while reading weekly schedule in save file : get incorrect amount of data\n");
            return;
        }

        /* set weekly schedule */
        uint8_t day;
        for (day = 0; day < 7; day++) {
            if (Schedule_Weekly_Schedule_Set(instance, day, &week[day])) {
                printf("day %d schedule set !\n", day);
            } else {
                printf("Unable to set new schedule !\n");
            }
        }
    }
}

/* write dailyschedule & start/end date for each schedule to a file */
void writeScheduleTo(const int __scheduleSaveFile_fd) {
    /* go to start of file */
    lseek(__scheduleSaveFile_fd, 0, SEEK_SET);


    uint8_t instance;
    for (instance = 0; instance < 4; instance++) {
        /* write instance */
        if (!write(__scheduleSaveFile_fd, &instance, 1)) {
            printf("unable to write instance\n");
        }

        /* get start & end date to write in save file */
        BACNET_DATE start_date, end_date = {0};
        if (!Schedule_Effective_Period(instance, &start_date, &end_date)) {
            printf("wrong schedule instance, unable to get start & end date\n");
        } else {
            /* write start & end date */
            if (write(__scheduleSaveFile_fd, &start_date, sizeof(BACNET_DATE))) {
                printf("schedule %d start date saved !\n", instance);
            } else {
                printf("unable to save start date\n");
            }
            if (write(__scheduleSaveFile_fd, &end_date, sizeof(BACNET_DATE))) {
                printf("schedule %d end date saved !\n", instance);
            } else {
                printf("unable to save end date\n");
            }
        }

        /* write daily schedule of each day */
        uint8_t day;
        for (day = 0; day < 7; day++) {
            /* get daily schedule */
            BACNET_DAILY_SCHEDULE *dailyScheduleToWrite = Schedule_Weekly_Schedule(instance, day);
            if (NULL == dailyScheduleToWrite) {
                printf("unable to get daily schedule for day %d of schedule %d", day, instance);
            } else {
                if (write(__scheduleSaveFile_fd, dailyScheduleToWrite, sizeof(BACNET_DAILY_SCHEDULE))) {
                    printf("schedule %d day %d written on save file !\n", instance, day);
                } else {
                    printf("unable to write day %d\n", day);
                }
            }
        }
    }
}

/**
 * @brief Initializes the BACnet objects.
 */
static void initServiceHandlers(void) {
    Device_Init(My_Object_Table);

    /* open file with reading and writing rights.
     * used for saving schedule on file system
     */
    int scheduleSaveFile_fd = open(SCHEDULESAVEFILE, O_RDONLY);
    if (-1 == scheduleSaveFile_fd) {
        printf("Error occured while creating schedule save file : %s\r\n", strerror(errno));
    } else {
        readScheduleFrom(scheduleSaveFile_fd);
        close(scheduleSaveFile_fd);
    }

<<<<<<< HEAD
    Schedule_Object(0)->Schedule_Default.type.Boolean = 0;
    Schedule_Object(0)->Schedule_Default.tag = BACNET_APPLICATION_TAG_BOOLEAN;

    Schedule_Object(1)->Schedule_Default.type.Boolean = 0;
    Schedule_Object(1)->Schedule_Default.tag = BACNET_APPLICATION_TAG_BOOLEAN;

    Schedule_Object(2)->Schedule_Default.type.Boolean = 0;
    Schedule_Object(2)->Schedule_Default.tag = BACNET_APPLICATION_TAG_BOOLEAN;

    Schedule_Object(3)->Schedule_Default.type.Boolean = 0;
    Schedule_Object(3)->Schedule_Default.tag = BACNET_APPLICATION_TAG_BOOLEAN;

    Schedule_Object(0)->Present_Value.tag = BACNET_APPLICATION_TAG_BOOLEAN;
    Schedule_Object(1)->Present_Value.tag = BACNET_APPLICATION_TAG_BOOLEAN;
    Schedule_Object(2)->Present_Value.tag = BACNET_APPLICATION_TAG_BOOLEAN;
    Schedule_Object(3)->Present_Value.tag = BACNET_APPLICATION_TAG_BOOLEAN;
=======
    Schedule_Object(0)->Present_Value.type.Boolean = true;
    Schedule_Object(1)->Present_Value.type.Boolean = true;
    Schedule_Object(2)->Present_Value.type.Boolean = true;
    Schedule_Object(3)->Present_Value.type.Boolean = true;

    Schedule_Object(0)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(1)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(2)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(3)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
>>>>>>> dev_AstroCalculation

    uint8_t i;
    for(i = 0; i < 3; i++) {
        ao_instance[i] = Analog_Output_Create(i);
        bo_instance[i] = Binary_Output_Create(i);
        bi_instance[i] = Binary_Input_Create(i);

        Binary_Input_Name_Set(bi_instance[i], "BI Read-Only");

        Analog_Output_Name_Set(ao_instance[i], "AO Writeable");
        Analog_Output_Units_Set(ao_instance[i], UNITS_PERCENT);
        Analog_Output_Present_Value_Set(ao_instance[i], 50.0, BACNET_MAX_PRIORITY);

        Binary_Output_Name_Set(bo_instance[i], "BO Writeable");
        Binary_Output_Present_Value_Set(bo_instance[i], 0, BACNET_MAX_PRIORITY);
    }

    /* BACnet service handlers */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(
            SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(
            SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
}

static void initBacnetStack() {
    const char *device_name = "ESUDAroc - Device"; /* Default device name */
    uint32_t device_instance = 123456; /* Default device instance ID */

    printf("Starting BACnet Server...\n");

    Device_Set_Object_Instance_Number(device_instance);
    printf("BACnet Device ID: %u\n", device_instance);

    /* Initialize BACnet stack */
    dlenv_init();
    initServiceHandlers();
    atexit(datalink_cleanup);

    Device_Object_Name_ANSI_Init(device_name);
    printf("BACnet Device Name: %s\n", device_name);

    /* Broadcast an I-Am message */
    Send_I_Am(&Rx_Buf[0]);
}

struct dataPacket {
    enum {SCHEDULE, BINARY_OUTPUT, BINARY_INPUT, ANALOG_INPUT, ANALOG_OUTPUT} typeOfObject;
    uint32_t instanceOfObject;
    enum {BOOLEAN, REAL} tagOfObject;

    union {
        bool binary;
        float analog;
    } value;
};

/**
 * @brief send data of an object through Output FIFO
 */
static void sendObjectDataToFifo(enum BACnetObjectType __objectType, uint32_t __objectInstance, int __fifoFd) {
    struct dataPacket dataToSend;

    /* serialyse data */

    dataToSend.instanceOfObject = __objectInstance;
    switch (__objectType) {
        case OBJECT_SCHEDULE:
            dataToSend.typeOfObject = SCHEDULE;
            if (Schedule_Object(__objectInstance)->Present_Value.tag == BACNET_APPLICATION_TAG_REAL) {
                dataToSend.tagOfObject = REAL;
                dataToSend.value.analog = Schedule_Object(__objectInstance)->Present_Value.type.Real;
            } else if (Schedule_Object(__objectInstance)->Present_Value.tag == BACNET_APPLICATION_TAG_BOOLEAN) {
                dataToSend.tagOfObject = BOOLEAN;
                dataToSend.value.binary = Schedule_Object(__objectInstance)->Present_Value.type.Enumerated;
            }
            break;
        case OBJECT_ANALOG_INPUT:
            dataToSend.typeOfObject = ANALOG_INPUT;
            dataToSend.tagOfObject = REAL;
            dataToSend.value.analog = Analog_Input_Present_Value(__objectInstance);
            break;
        case OBJECT_ANALOG_OUTPUT:
            dataToSend.typeOfObject = ANALOG_OUTPUT;
            dataToSend.tagOfObject = REAL;
            dataToSend.value.analog = Analog_Output_Present_Value(__objectInstance);
            break;
        case OBJECT_BINARY_OUTPUT:
            dataToSend.typeOfObject = BINARY_OUTPUT;
            dataToSend.tagOfObject = BOOLEAN;
            if(Binary_Output_Present_Value(__objectInstance) == BINARY_ACTIVE) {
                dataToSend.value.binary = true;
            } else {
                dataToSend.value.binary = false;
            }
            break;
        case OBJECT_BINARY_INPUT:
            dataToSend.typeOfObject = BINARY_INPUT;
            dataToSend.tagOfObject = BOOLEAN;
            if(Binary_Input_Present_Value(__objectInstance) == BINARY_ACTIVE) {
                dataToSend.value.binary = true;
            } else {
                dataToSend.value.binary = false;
            }
            break;
        default:
            break;
    }

    /* send the datagramm */
    if (-1 == write(__fifoFd, &dataToSend, sizeof(dataToSend))) {
        printf("impossible to write to FIFO %s\r\n", strerror(errno));
    }
}

/**
 * @brief read data of an object through Input FIFO
 */
static struct dataPacket receiveObjectDataFromFifo(int __fifoFd) {
    struct dataPacket dataReceived;
    if (-1 == read(__fifoFd, &dataReceived, sizeof(dataReceived))) {
        printf("impossible to read from FIFO %s\r\n", strerror(errno));
    }
    return dataReceived;
}

static void sendDataToApp(int __outputFd) {
    if (-1 != __outputFd) {
        sendObjectDataToFifo(OBJECT_BINARY_OUTPUT, bo_instance[0], __outputFd);
        sendObjectDataToFifo(OBJECT_BINARY_OUTPUT, bo_instance[1], __outputFd);
        sendObjectDataToFifo(OBJECT_BINARY_OUTPUT, bo_instance[2], __outputFd);

        sendObjectDataToFifo(OBJECT_ANALOG_OUTPUT, ao_instance[0], __outputFd);
        sendObjectDataToFifo(OBJECT_ANALOG_OUTPUT, ao_instance[1], __outputFd);
        sendObjectDataToFifo(OBJECT_ANALOG_OUTPUT, ao_instance[2], __outputFd);

        sendObjectDataToFifo(OBJECT_SCHEDULE, 0, __outputFd);
        sendObjectDataToFifo(OBJECT_SCHEDULE, 1, __outputFd);
        sendObjectDataToFifo(OBJECT_SCHEDULE, 2, __outputFd);
        sendObjectDataToFifo(OBJECT_SCHEDULE, 3, __outputFd);
    } else {
        printf("output fifo not openned !! %s\r\n", strerror(errno));
    }
}

static void readDataFromApp(int __inputFd) {
    if (-1 == __inputFd) {
        printf("input fifo not openned !! %s\r\n", strerror(errno));
        return;
    }

    /* TODO : Check object type & object tag validity between received data and local object */
    uint8_t i;
    for (i = 0; i < 3; i++) {
        struct dataPacket newData = receiveObjectDataFromFifo(__inputFd);
        switch (newData.typeOfObject) {
            case BINARY_INPUT:
                if (newData.value.binary != Binary_Input_Present_Value(newData.instanceOfObject)) {
                    Binary_Input_Present_Value_Set(newData.instanceOfObject, (BACNET_BINARY_PV) newData.value.binary);
                }
                break;
            case ANALOG_INPUT:
                Analog_Input_Present_Value_Set(newData.instanceOfObject, newData.value.analog);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief Output the present value if possible (FIFO openned from both side)
 * Update present value from value received by FIFO
 */
static void process(int __inputFd, int __outputFd) {
    static enum {START, SEND, RECEIVE} processState;

    if (processState == START) processState = SEND;
    else if (processState == SEND) {
        /* envoi des valeurs d'objet sur la fifo */
        sendDataToApp(__outputFd);
        processState = RECEIVE;
    } else if (processState == RECEIVE) {
        /* reception des valeurs, mise a jour des objets */
        readDataFromApp(__inputFd);
        processState = SEND;
    } else processState = START;
}

/**
 * @brief Main entry point for the BACnet server.
 */
int main() {
    BACNET_ADDRESS src = { 0 };
    uint16_t pdu_len = 0;
    unsigned timeout = 1000;
    time_t current_time;

    int inFifoFd = -1;
    int outFifoFd = -1;

    /* if fifo don't exist, create new one
     * if fifo exist already, just pass
     */
    if (-1 == mkfifo(OUTPUT_FIFO_PATH, 0666) && errno != EEXIST) {
        perror("An error occured when creating FIFO");
        exit(EXIT_FAILURE);
    }
    if (-1 == mkfifo(INPUT_FIFO_PATH, 0666) && errno != EEXIST) {
        perror("An error occured when creating FIFO");
        exit(EXIT_FAILURE);
    }

    initBacnetStack();

    /* open file with reading and writing rights.
     * used for saving schedule on file system
     */
    int scheduleSaveFile_fd = open(SCHEDULESAVEFILE, O_WRONLY);
    if (-1 == scheduleSaveFile_fd) {
        printf("Error occured while openning schedule save file : %s\r\n", strerror(errno));
    }

    /* Main loop */
    while (1) {
        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);
        if (pdu_len) {
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }

        current_time = time(NULL);
        calendar_time = *localtime((const time_t *) &current_time);

        actual_day = calendar_time.tm_wday;
        actual_time.hour = calendar_time.tm_hour;
        actual_time.min = calendar_time.tm_min;
        actual_time.sec = calendar_time.tm_sec;

        /* mise a jour schedule */
        Schedule_Recalculate_PV(Schedule_Object(0), actual_day, (const BACNET_TIME *) &actual_time);
        Schedule_Recalculate_PV(Schedule_Object(1), actual_day, (const BACNET_TIME *) &actual_time);
        Schedule_Recalculate_PV(Schedule_Object(2), actual_day, (const BACNET_TIME *) &actual_time);
        Schedule_Recalculate_PV(Schedule_Object(3), actual_day, (const BACNET_TIME *) &actual_time);

        if (outFifoFd == -1) {
            outFifoFd = open(OUTPUT_FIFO_PATH, O_WRONLY);
        }

        if (inFifoFd == -1) {
            inFifoFd = open(INPUT_FIFO_PATH, O_RDONLY);
        }

        /* save schedule on file */

        writeScheduleTo(scheduleSaveFile_fd);

        process(inFifoFd, outFifoFd);
    }

    close(inFifoFd);
    close(outFifoFd);
    close(scheduleSaveFile_fd);

    unlink(OUTPUT_FIFO_PATH);
    unlink(INPUT_FIFO_PATH);

    return EXIT_SUCCESS;
}
