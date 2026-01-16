/**
 * @file
 * @brief Mini BACnet server example for prototyping
 *
 * If no arguments are provided, it defaults to:
 * - Device ID: 260001
 * - Device Name: "MiniServer"
 *
 * Usage on Linux
 * $ ./bacmini 54321 MiniDevice
 *
 * Where:
 * - 54321 is the BACnet Device Instance ID
 * - "MiniDevice" is the BACnet Device Name
 *
 * @date 2025
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* BACnet Stack includes */
#include "bacnet/apdu.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bacdef.h"
#include "bacnet/bactext.h"
#include "bacnet/basic/binding/address.h"

#include "bacnet/basic/object/ao.h"
#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/bo.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/basic/object/device.h"

#include "bacnet/basic/object/schedule.h"
#include "bacnet/basic/object/trendlog.h"

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

/* Buffers */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* Update interval in seconds */
/* Switches read only point values */
#define INTERVAL 5

typedef struct {
    char *binary_state;
    float analog_value;
} TestValue;

static TestValue test_values[] = {
    { "active", 1.0 },
    { "inactive", 2.0 },
    { "active", 3.0 },
    { "inactive", 4.0 },
};

/* Device week day & time */
static BACNET_WEEKDAY actual_day;
static BACNET_TIME actual_time;
static struct tm calendar_time;

/* BACnet Object Instances */
static uint32_t ai_instance;
static uint32_t bv_instance;
static uint32_t ao_instance;
static uint32_t bo_instance;

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

    /* Trend Log Object */
    { OBJECT_TRENDLOG,
        Trend_Log_Init,
        Trend_Log_Count,
        Trend_Log_Index_To_Instance,
        Trend_Log_Valid_Instance,
        Trend_Log_Object_Name,
        Trend_Log_Read_Property,
        Trend_Log_Write_Property,
        Trend_Log_Property_Lists,
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

    /* Analog Value (Read-Only) */
    { OBJECT_ANALOG_INPUT,
        Analog_Input_Init,
        Analog_Input_Count,
        Analog_Input_Index_To_Instance,
        Analog_Input_Valid_Instance,
        Analog_Input_Object_Name,
        Analog_Input_Read_Property,
        NULL,
        Analog_Input_Property_Lists,
        NULL,
        NULL,
        Analog_Input_Encode_Value_List,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        Analog_Input_Create,
        Analog_Input_Delete,
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

    /* Binary Value (Read-Only) */
    { OBJECT_BINARY_VALUE,
        Binary_Value_Init,
        Binary_Value_Count,
        Binary_Value_Index_To_Instance,
        Binary_Value_Valid_Instance,
        Binary_Value_Object_Name,
        Binary_Value_Read_Property,
        NULL,
        Binary_Value_Property_Lists,
        NULL,
        NULL,
        Binary_Value_Encode_Value_List,
        Binary_Value_Change_Of_Value,
        Binary_Value_Change_Of_Value_Clear,
        NULL,
        NULL,
        NULL,
        Binary_Value_Create,
        Binary_Value_Delete,
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
 * @brief Function to update AV-0 and BV-0 values.
 */
static void process_task(void)
{
    static size_t test_index = 0;

    TestValue next_value = test_values[test_index];
    test_index = (test_index + 1) % ARRAY_SIZE(test_values);

    if (!Analog_Input_Out_Of_Service(ai_instance)) {
        Analog_Input_Present_Value_Set(
                ai_instance, next_value.analog_value);
        printf("AI-0 updated to: %.1f\n", next_value.analog_value);
    }

    if (!Binary_Value_Out_Of_Service(bv_instance)) {
        Binary_Value_Present_Value_Set(
                bv_instance,
                strcmp(next_value.binary_state, "active") == 0 ? 1 : 0);
        printf("BV-0 updated to: %s\n", next_value.binary_state);
    }

    Schedule_Recalculate_PV(Schedule_Object(0), actual_day, (const BACNET_TIME *) &actual_time);

    if (!Binary_Output_Out_Of_Service(bo_instance)) {
        Binary_Output_Present_Value_Set(
                bo_instance, 
                (BACNET_BINARY_PV) Schedule_Object(0)->Present_Value.type.Enumerated, BACNET_MAX_PRIORITY);
    }

    trend_log_timer(1);
}

/**
 * @brief Initializes the BACnet objects (AV-0, AO-0, BO-0, BV-0).
 */
static void Init_Service_Handlers(void)
{
    Device_Init(My_Object_Table);

    BACNET_DATE start_date = { 1900, 1, 1, 1 };
    BACNET_DATE end_date = { 2030, 1, 1, 1 };

    Schedule_Effective_Period_Set(Schedule_Instance_To_Index(0), &start_date, &end_date);
    Schedule_Object(0)->Present_Value.tag = BACNET_APPLICATION_TAG_ENUMERATED;

    
    /*
    if (Schedule_List_Of_Object_Property_References_Set(0, 0, )) {
        printf("Property References set!\n");
    }
    */

    /*
    BACNET_TIME time = {9, 15, 15, 0};
    
    Daily_Schedule.Time_Values[0].Time = time;
    Daily_Schedule.TV_Count = 1;

    if(Schedule_Weekly_Schedule_Set(Schedule_Instance_To_Index(0), 0, &Daily_Schedule)) {
        printf("Weekly Schedule set!\n");
    } else printf("Error -> cannot set weekly schedule");
    */

    ai_instance = Analog_Input_Create(0);
    ao_instance = Analog_Output_Create(0);
    bo_instance = Binary_Output_Create(0);
    bv_instance = Binary_Value_Create(0);

    /* Configure read-only Analog Value */
    Analog_Input_Name_Set(ai_instance, "AI Read Only");
    Analog_Input_Units_Set(ai_instance, UNITS_DEGREES_CELSIUS);
    Analog_Input_Present_Value_Set(ai_instance, 22.5);

    /* Configure writable Analog Output */
    Analog_Output_Name_Set(ao_instance, "AO Writeable");
    Analog_Output_Units_Set(ao_instance, UNITS_PERCENT);
    Analog_Output_Present_Value_Set(ao_instance, 50.0, BACNET_MAX_PRIORITY);

    /* Configure writable Binary Output */
    Binary_Output_Name_Set(bo_instance, "BO Writeable");
    Binary_Output_Present_Value_Set(bo_instance, 0, BACNET_MAX_PRIORITY);

    /* Configure read-only Binary Value */
    Binary_Value_Name_Set(bv_instance, "BV Read Only");

    printf("Created AI-0 (Read-Only), AO-0 (Commandable), BO-0 (Commandable), "
            "and BV-0 (Read-Only)\n");

    /* BACnet service handlers */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(
            SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(
            SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
}

/**
 * @brief Main entry point for the BACnet server.
 */
int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = { 0 };
    uint16_t pdu_len = 0;
    unsigned timeout = 1000;
    time_t last_update_time = 0;
    time_t current_time;

    const char *device_name = "MiniServer"; /* Default device name */
    uint32_t device_instance = 123456; /* Default device instance ID */

    printf("Starting BACnet Server...\n");

    /* Handle command-line arguments */
    if (argc > 1) {
        device_instance = strtoul(argv[1], NULL, 10);
    }
    Device_Set_Object_Instance_Number(device_instance);
    printf("BACnet Device ID: %u\n", device_instance);

    /* Initialize BACnet stack */
    dlenv_init();
    Init_Service_Handlers();
    atexit(datalink_cleanup);

    if (argc > 2) {
        device_name = argv[2];
    }
    Device_Object_Name_ANSI_Init(device_name);
    printf("BACnet Device Name: %s\n", device_name);

    /* Broadcast an I-Am message */
    Send_I_Am(&Rx_Buf[0]);

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
        if (difftime(current_time, last_update_time) >= INTERVAL) {
            process_task();
            last_update_time = current_time;
        }
    }

    return EXIT_SUCCESS;
}
