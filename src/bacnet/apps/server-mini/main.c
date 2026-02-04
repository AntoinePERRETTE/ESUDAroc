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


/**
 * @brief Initializes the BACnet objects.
 */
static void initServiceHandlers(void) {
    Device_Init(My_Object_Table);

    BACNET_DATE start_date = { 1900, 1, 1, 1 };
    BACNET_DATE end_date = { 2130, 1, 1, 1 };

    Schedule_Effective_Period_Set(Schedule_Instance_To_Index(0), &start_date, &end_date);
    Schedule_Effective_Period_Set(Schedule_Instance_To_Index(1), &start_date, &end_date);
    Schedule_Effective_Period_Set(Schedule_Instance_To_Index(2), &start_date, &end_date);
    Schedule_Effective_Period_Set(Schedule_Instance_To_Index(3), &start_date, &end_date);
    Schedule_Object(0)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(1)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(2)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;
    Schedule_Object(3)->Present_Value.tag = BACNET_APPLICATION_TAG_REAL;

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
    enum {ENUMERATED, REAL} tagOfObject;

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
            } else if (Schedule_Object(__objectInstance)->Present_Value.tag == BACNET_APPLICATION_TAG_ENUMERATED) {
                dataToSend.tagOfObject = ENUMERATED;
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
            dataToSend.tagOfObject = ENUMERATED;
            if(Binary_Output_Present_Value(__objectInstance) == BINARY_ACTIVE) {
                dataToSend.value.binary = true;
            } else {
                dataToSend.value.binary = false;
            }
            break;
        case OBJECT_BINARY_INPUT:
            dataToSend.typeOfObject = BINARY_INPUT;
            dataToSend.tagOfObject = ENUMERATED;
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

    /* !!! on ne reçoie par les schedules (aucune raison d'être modifié par l'app) !!! */
    /* TODO : Check object type & object tag validity between received data and local object */
    uint8_t i = 0;
    for (; i < 9; i++) {
        struct dataPacket newData = receiveObjectDataFromFifo(__inputFd);
        switch (newData.typeOfObject) {
            case BINARY_INPUT:
                if (newData.value.binary != Binary_Input_Present_Value(newData.instanceOfObject)) {
                    Binary_Input_Present_Value_Set(newData.instanceOfObject, (BACNET_BINARY_PV) newData.value.binary);
                }
                break;
            case BINARY_OUTPUT:
                if (newData.value.binary != Binary_Output_Present_Value(newData.instanceOfObject)) {
                    Binary_Output_Present_Value_Set(newData.instanceOfObject, (BACNET_BINARY_PV) newData.value.binary, BACNET_MAX_PRIORITY);
                }
                break;
            case ANALOG_OUTPUT:
                Analog_Output_Present_Value_Set(newData.instanceOfObject, newData.value.analog, BACNET_MAX_PRIORITY);
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
    time_t last_update_time = 0;
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

        if (outFifoFd == -1) {
            outFifoFd = open(OUTPUT_FIFO_PATH, O_WRONLY);
        }

        if (inFifoFd == -1) {
            inFifoFd = open(INPUT_FIFO_PATH, O_RDONLY);
        }

        process(inFifoFd, outFifoFd);
    }

    close(inFifoFd);
    close(outFifoFd);

    unlink(OUTPUT_FIFO_PATH);
    unlink(INPUT_FIFO_PATH);

    return EXIT_SUCCESS;
}
