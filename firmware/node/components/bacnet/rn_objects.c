// Device object and the room's objects. Implements the Device_* functions
// that bacnet-stack's service handlers call (ReadProperty,
// ReadPropertyMultiple, WriteProperty, Who-Is), backed by the controller's
// state instead of bacnet-stack's generic object implementations, so the
// priority arrays here are the controller's own.

#include <string.h>

#include "bacnet/bacapp.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bacenum.h"
#include "bacnet/bacstr.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/proplist.h"
#include "bacnet/rp.h"
#include "bacnet/wp.h"
#include "rn_internal.h"

#define VENDOR_ID         260 // bacnet-stack's; a product would register its own
#define VENDOR_NAME       "RoomNode (open source)"
#define MODEL_NAME        "RoomNode v1"
#define FIRMWARE_REVISION "1.0"
#define APDU_TIMEOUT_MS   3000
#define APDU_RETRIES      3
#define MODE_STATES       3

typedef struct
{
    BACNET_OBJECT_TYPE type;
    uint32_t instance;
    const char *name;
    const char *description;
} object_t;

static const object_t OBJECTS[] = {
    {OBJECT_ANALOG_INPUT, 1, "Room temperature", "Regulated temperature: TMP117, SHTC3 fallback or room model"},
    {OBJECT_ANALOG_INPUT, 2, "Aux temperature", "SHTC3 temperature"},
    {OBJECT_ANALOG_INPUT, 3, "Relative humidity", "SHTC3 relative humidity"},
    {OBJECT_ANALOG_VALUE, 1, "Setpoint", "Room setpoint; relinquish default follows the slider"},
    {OBJECT_BINARY_OUTPUT, 1, "Heating", "Heating output"},
    {OBJECT_MULTI_STATE_VALUE, 1, "Mode", "Operating mode"},
};
#define OBJECT_COUNT (sizeof(OBJECTS) / sizeof(OBJECTS[0]))

static const char *const MODE_TEXT[MODE_STATES] = {"Auto", "Off", "On"};

// clang-format off
static const int32_t DEVICE_REQUIRED[] = {
    PROP_OBJECT_IDENTIFIER, PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_SYSTEM_STATUS, PROP_VENDOR_NAME,
    PROP_VENDOR_IDENTIFIER, PROP_MODEL_NAME, PROP_FIRMWARE_REVISION, PROP_APPLICATION_SOFTWARE_VERSION,
    PROP_PROTOCOL_VERSION, PROP_PROTOCOL_REVISION, PROP_PROTOCOL_SERVICES_SUPPORTED,
    PROP_PROTOCOL_OBJECT_TYPES_SUPPORTED, PROP_OBJECT_LIST, PROP_MAX_APDU_LENGTH_ACCEPTED,
    PROP_SEGMENTATION_SUPPORTED, PROP_APDU_TIMEOUT, PROP_NUMBER_OF_APDU_RETRIES, PROP_DEVICE_ADDRESS_BINDING,
    PROP_DATABASE_REVISION, -1};
static const int32_t DEVICE_OPTIONAL[] = {PROP_DESCRIPTION, -1};

static const int32_t AI_REQUIRED[] = {
    PROP_OBJECT_IDENTIFIER, PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_PRESENT_VALUE, PROP_STATUS_FLAGS,
    PROP_EVENT_STATE, PROP_OUT_OF_SERVICE, PROP_UNITS, -1};
static const int32_t AI_OPTIONAL[] = {PROP_DESCRIPTION, PROP_RELIABILITY, -1};

static const int32_t AV_REQUIRED[] = {
    PROP_OBJECT_IDENTIFIER, PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_PRESENT_VALUE, PROP_STATUS_FLAGS,
    PROP_EVENT_STATE, PROP_OUT_OF_SERVICE, PROP_UNITS, -1};
static const int32_t AV_OPTIONAL[] = {
    PROP_DESCRIPTION, PROP_PRIORITY_ARRAY, PROP_RELINQUISH_DEFAULT, PROP_CURRENT_COMMAND_PRIORITY, -1};

static const int32_t BO_REQUIRED[] = {
    PROP_OBJECT_IDENTIFIER, PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_PRESENT_VALUE, PROP_STATUS_FLAGS,
    PROP_EVENT_STATE, PROP_OUT_OF_SERVICE, PROP_POLARITY, PROP_PRIORITY_ARRAY, PROP_RELINQUISH_DEFAULT,
    PROP_CURRENT_COMMAND_PRIORITY, -1};
static const int32_t BO_OPTIONAL[] = {PROP_DESCRIPTION, PROP_MINIMUM_ON_TIME, PROP_MINIMUM_OFF_TIME, -1};

static const int32_t MSV_REQUIRED[] = {
    PROP_OBJECT_IDENTIFIER, PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_PRESENT_VALUE, PROP_STATUS_FLAGS,
    PROP_EVENT_STATE, PROP_OUT_OF_SERVICE, PROP_NUMBER_OF_STATES, -1};
static const int32_t MSV_OPTIONAL[] = {PROP_DESCRIPTION, PROP_STATE_TEXT, -1};

static const int32_t NONE[] = {-1};
// clang-format on

// Snapshot taken once per request; the array element encoders below have no
// context argument, so they read it from here. Requests are handled one at a
// time by the BACnet task.
typedef struct
{
    rc_points_t p;
    priority_array_t heating;
    priority_array_t setpoint;
} snapshot_t;

static snapshot_t s_snap;

static void take_snapshot(void)
{
    rn_access->read(&s_snap.p, &s_snap.heating, &s_snap.setpoint);
}

static const object_t *find(BACNET_OBJECT_TYPE type, uint32_t instance)
{
    for (size_t i = 0; i < OBJECT_COUNT; i++)
        if (OBJECTS[i].type == type && OBJECTS[i].instance == instance)
            return &OBJECTS[i];
    return NULL;
}

// --- Device functions required by bacnet-stack -----------------------------

uint32_t Device_Object_Instance_Number(void)
{
    return rn_config.device_instance;
}

uint16_t Device_Vendor_Identifier(void)
{
    return VENDOR_ID;
}

BACNET_SEGMENTATION Device_Segmentation_Supported(void)
{
    return SEGMENTATION_NONE;
}

const char *Device_Model_Name(void)
{
    return MODEL_NAME;
}

const char *Device_Serial_Number(void)
{
    return "";
}

// The RP/RPM handlers resolve the Network Port wildcard instance through this.
// There is no Network Port object, so the wildcard never matches one.
uint32_t Network_Port_Index_To_Instance(unsigned index)
{
    (void)index;
    return BACNET_MAX_INSTANCE;
}

bool Device_Valid_Object_Id(BACNET_OBJECT_TYPE type, uint32_t instance)
{
    return (type == OBJECT_DEVICE && instance == rn_config.device_instance) || find(type, instance) != NULL;
}

static void property_lists(BACNET_OBJECT_TYPE type, const int32_t **required, const int32_t **optional)
{
    switch (type)
    {
    case OBJECT_DEVICE: *required = DEVICE_REQUIRED, *optional = DEVICE_OPTIONAL; break;
    case OBJECT_ANALOG_INPUT: *required = AI_REQUIRED, *optional = AI_OPTIONAL; break;
    case OBJECT_ANALOG_VALUE: *required = AV_REQUIRED, *optional = AV_OPTIONAL; break;
    case OBJECT_BINARY_OUTPUT: *required = BO_REQUIRED, *optional = BO_OPTIONAL; break;
    case OBJECT_MULTI_STATE_VALUE: *required = MSV_REQUIRED, *optional = MSV_OPTIONAL; break;
    default: *required = *optional = NONE; break;
    }
}

void Device_Objects_Property_List(BACNET_OBJECT_TYPE type, uint32_t instance, struct special_property_list_t *list)
{
    (void)instance;
    const int32_t *required, *optional;
    property_lists(type, &required, &optional);
    list->Required.pList = required;
    list->Required.count = property_list_count(required);
    list->Optional.pList = optional;
    list->Optional.count = property_list_count(optional);
    list->Proprietary.pList = NONE;
    list->Proprietary.count = 0;
}

bool Device_Objects_Property_List_Member(BACNET_OBJECT_TYPE type, uint32_t instance, BACNET_PROPERTY_ID property)
{
    (void)instance;
    const int32_t *required, *optional;
    property_lists(type, &required, &optional);
    return property_list_member(required, property) || property_list_member(optional, property);
}

// --- Encoding helpers ------------------------------------------------------

static int encode_string(uint8_t *apdu, const char *text)
{
    BACNET_CHARACTER_STRING s;
    characterstring_init_ansi(&s, text);
    return encode_application_character_string(apdu, &s);
}

static int encode_status_flags(uint8_t *apdu, bool fault)
{
    BACNET_BIT_STRING bits;
    bitstring_init(&bits);
    bitstring_set_bit(&bits, STATUS_FLAG_IN_ALARM, false);
    bitstring_set_bit(&bits, STATUS_FLAG_FAULT, fault);
    bitstring_set_bit(&bits, STATUS_FLAG_OVERRIDDEN, false);
    bitstring_set_bit(&bits, STATUS_FLAG_OUT_OF_SERVICE, false);
    return encode_application_bitstring(apdu, &bits);
}

static int encode_object_list_element(uint32_t device, BACNET_ARRAY_INDEX index, uint8_t *apdu)
{
    if (index == 0)
        return encode_application_object_id(apdu, OBJECT_DEVICE, device);
    if (index > OBJECT_COUNT)
        return 0;
    return encode_application_object_id(apdu, OBJECTS[index - 1].type, OBJECTS[index - 1].instance);
}

static int encode_setpoint_pa_element(uint32_t instance, BACNET_ARRAY_INDEX index, uint8_t *apdu)
{
    (void)instance;
    int priority = (int)index + 1;
    if (!pa_is_set(&s_snap.setpoint, priority))
        return encode_application_null(apdu);
    return encode_application_real(apdu, s_snap.setpoint.value[index]);
}

static int encode_heating_pa_element(uint32_t instance, BACNET_ARRAY_INDEX index, uint8_t *apdu)
{
    (void)instance;
    int priority = (int)index + 1;
    if (!pa_is_set(&s_snap.heating, priority))
        return encode_application_null(apdu);
    return encode_application_enumerated(apdu, s_snap.heating.value[index] > 0.5f ? BINARY_ACTIVE : BINARY_INACTIVE);
}

static int encode_state_text_element(uint32_t instance, BACNET_ARRAY_INDEX index, uint8_t *apdu)
{
    (void)instance;
    return index < MODE_STATES ? encode_string(apdu, MODE_TEXT[index]) : 0;
}

static int encode_command_priority(uint8_t *apdu, const priority_array_t *pa)
{
    int p = pa_active_priority(pa);
    return p ? encode_application_unsigned(apdu, p) : encode_application_null(apdu);
}

static int array(BACNET_READ_PROPERTY_DATA *rp, uint32_t instance, bacnet_array_property_element_encode_function enc,
                 unsigned size)
{
    int len = bacnet_array_encode(instance, rp->array_index, enc, size, rp->application_data,
                                  rp->application_data_len);
    if (len == BACNET_STATUS_ERROR)
    {
        rp->error_class = ERROR_CLASS_PROPERTY;
        rp->error_code = ERROR_CODE_INVALID_ARRAY_INDEX;
    }
    return len;
}

static bool is_array(BACNET_PROPERTY_ID property)
{
    return property == PROP_OBJECT_LIST || property == PROP_PRIORITY_ARRAY || property == PROP_STATE_TEXT;
}

static int unknown_property(BACNET_READ_PROPERTY_DATA *rp)
{
    rp->error_class = ERROR_CLASS_PROPERTY;
    rp->error_code = ERROR_CODE_UNKNOWN_PROPERTY;
    return BACNET_STATUS_ERROR;
}

// --- ReadProperty ----------------------------------------------------------

static int read_device(BACNET_READ_PROPERTY_DATA *rp, uint8_t *apdu)
{
    BACNET_BIT_STRING bits;
    switch (rp->object_property)
    {
    case PROP_OBJECT_IDENTIFIER: return encode_application_object_id(apdu, OBJECT_DEVICE, rn_config.device_instance);
    case PROP_OBJECT_NAME: return encode_string(apdu, rn_config.device_name);
    case PROP_OBJECT_TYPE: return encode_application_enumerated(apdu, OBJECT_DEVICE);
    case PROP_DESCRIPTION: return encode_string(apdu, "Room controller: climate, setpoint and heating");
    case PROP_SYSTEM_STATUS: return encode_application_enumerated(apdu, STATUS_OPERATIONAL);
    case PROP_VENDOR_NAME: return encode_string(apdu, VENDOR_NAME);
    case PROP_VENDOR_IDENTIFIER: return encode_application_unsigned(apdu, VENDOR_ID);
    case PROP_MODEL_NAME: return encode_string(apdu, MODEL_NAME);
    case PROP_FIRMWARE_REVISION:
    case PROP_APPLICATION_SOFTWARE_VERSION: return encode_string(apdu, FIRMWARE_REVISION);
    case PROP_PROTOCOL_VERSION: return encode_application_unsigned(apdu, BACNET_PROTOCOL_VERSION);
    case PROP_PROTOCOL_REVISION: return encode_application_unsigned(apdu, BACNET_PROTOCOL_REVISION);
    case PROP_PROTOCOL_SERVICES_SUPPORTED:
        bitstring_init(&bits);
        for (int i = 0; i < MAX_BACNET_SERVICES_SUPPORTED; i++)
            bitstring_set_bit(&bits, i, false);
        bitstring_set_bit(&bits, SERVICE_SUPPORTED_WHO_IS, true);
        bitstring_set_bit(&bits, SERVICE_SUPPORTED_I_AM, true);
        bitstring_set_bit(&bits, SERVICE_SUPPORTED_READ_PROPERTY, true);
        bitstring_set_bit(&bits, SERVICE_SUPPORTED_READ_PROP_MULTIPLE, true);
        bitstring_set_bit(&bits, SERVICE_SUPPORTED_WRITE_PROPERTY, true);
        return encode_application_bitstring(apdu, &bits);
    case PROP_PROTOCOL_OBJECT_TYPES_SUPPORTED:
        bitstring_init(&bits);
        for (int i = 0; i < MAX_ASHRAE_OBJECT_TYPE; i++)
            bitstring_set_bit(&bits, i, false);
        bitstring_set_bit(&bits, OBJECT_DEVICE, true);
        bitstring_set_bit(&bits, OBJECT_ANALOG_INPUT, true);
        bitstring_set_bit(&bits, OBJECT_ANALOG_VALUE, true);
        bitstring_set_bit(&bits, OBJECT_BINARY_OUTPUT, true);
        bitstring_set_bit(&bits, OBJECT_MULTI_STATE_VALUE, true);
        return encode_application_bitstring(apdu, &bits);
    case PROP_OBJECT_LIST:
        return array(rp, rn_config.device_instance, encode_object_list_element, OBJECT_COUNT + 1);
    case PROP_MAX_APDU_LENGTH_ACCEPTED: return encode_application_unsigned(apdu, MAX_APDU);
    case PROP_SEGMENTATION_SUPPORTED: return encode_application_enumerated(apdu, SEGMENTATION_NONE);
    case PROP_APDU_TIMEOUT: return encode_application_unsigned(apdu, APDU_TIMEOUT_MS);
    case PROP_NUMBER_OF_APDU_RETRIES: return encode_application_unsigned(apdu, APDU_RETRIES);
    case PROP_DEVICE_ADDRESS_BINDING: return 0; // empty list
    case PROP_DATABASE_REVISION: return encode_application_unsigned(apdu, 1);
    default: return unknown_property(rp);
    }
}

static int read_common(BACNET_READ_PROPERTY_DATA *rp, const object_t *o, uint8_t *apdu, bool fault)
{
    switch (rp->object_property)
    {
    case PROP_OBJECT_IDENTIFIER: return encode_application_object_id(apdu, o->type, o->instance);
    case PROP_OBJECT_NAME: return encode_string(apdu, o->name);
    case PROP_OBJECT_TYPE: return encode_application_enumerated(apdu, o->type);
    case PROP_DESCRIPTION: return encode_string(apdu, o->description);
    case PROP_STATUS_FLAGS: return encode_status_flags(apdu, fault);
    case PROP_EVENT_STATE: return encode_application_enumerated(apdu, EVENT_STATE_NORMAL);
    case PROP_OUT_OF_SERVICE: return encode_application_boolean(apdu, false);
    default: return -1; // not a common property
    }
}

static int read_object(BACNET_READ_PROPERTY_DATA *rp, const object_t *o, uint8_t *apdu)
{
    const rc_points_t *p = &s_snap.p;
    bool fault = false;
    float value = 0.0f;

    if (o->type == OBJECT_ANALOG_INPUT)
    {
        fault = o->instance == 1 ? !p->t_ref_valid : !p->t_aux_valid;
        value = o->instance == 1 ? p->t_ref : o->instance == 2 ? p->t_aux : p->rh;
    }
    int len = read_common(rp, o, apdu, fault);
    if (len >= 0)
        return len;

    switch (o->type)
    {
    case OBJECT_ANALOG_INPUT:
        switch (rp->object_property)
        {
        case PROP_PRESENT_VALUE: return encode_application_real(apdu, value);
        case PROP_UNITS:
            return encode_application_enumerated(apdu, o->instance == 3 ? UNITS_PERCENT_RELATIVE_HUMIDITY
                                                                        : UNITS_DEGREES_CELSIUS);
        case PROP_RELIABILITY:
            return encode_application_enumerated(apdu, fault ? RELIABILITY_NO_SENSOR : RELIABILITY_NO_FAULT_DETECTED);
        default: break;
        }
        break;

    case OBJECT_ANALOG_VALUE:
        switch (rp->object_property)
        {
        case PROP_PRESENT_VALUE: return encode_application_real(apdu, pa_value(&s_snap.setpoint));
        case PROP_UNITS: return encode_application_enumerated(apdu, UNITS_DEGREES_CELSIUS);
        case PROP_PRIORITY_ARRAY:
            return array(rp, o->instance, encode_setpoint_pa_element, PA_LEVELS);
        case PROP_RELINQUISH_DEFAULT: return encode_application_real(apdu, s_snap.setpoint.relinquish_default);
        case PROP_CURRENT_COMMAND_PRIORITY: return encode_command_priority(apdu, &s_snap.setpoint);
        default: break;
        }
        break;

    case OBJECT_BINARY_OUTPUT:
        switch (rp->object_property)
        {
        case PROP_PRESENT_VALUE: return encode_application_enumerated(apdu, p->heating ? BINARY_ACTIVE : BINARY_INACTIVE);
        case PROP_POLARITY: return encode_application_enumerated(apdu, POLARITY_NORMAL);
        case PROP_PRIORITY_ARRAY: return array(rp, o->instance, encode_heating_pa_element, PA_LEVELS);
        case PROP_RELINQUISH_DEFAULT: return encode_application_enumerated(apdu, BINARY_INACTIVE);
        case PROP_CURRENT_COMMAND_PRIORITY: return encode_command_priority(apdu, &s_snap.heating);
        case PROP_MINIMUM_ON_TIME:
        case PROP_MINIMUM_OFF_TIME: return encode_application_unsigned(apdu, rn_config.min_switch_s);
        default: break;
        }
        break;

    case OBJECT_MULTI_STATE_VALUE:
        switch (rp->object_property)
        {
        case PROP_PRESENT_VALUE: return encode_application_unsigned(apdu, (unsigned)p->mode + 1);
        case PROP_NUMBER_OF_STATES: return encode_application_unsigned(apdu, MODE_STATES);
        case PROP_STATE_TEXT: return array(rp, o->instance, encode_state_text_element, MODE_STATES);
        default: break;
        }
        break;

    default:
        break;
    }
    return unknown_property(rp);
}

int Device_Read_Property(BACNET_READ_PROPERTY_DATA *rp)
{
    uint8_t *apdu = rp->application_data;
    if (!apdu || rp->application_data_len == 0)
        return 0;

    const object_t *o = NULL;
    if (!(rp->object_type == OBJECT_DEVICE && rp->object_instance == rn_config.device_instance) &&
        !(o = find(rp->object_type, rp->object_instance)))
    {
        rp->error_class = ERROR_CLASS_OBJECT;
        rp->error_code = ERROR_CODE_UNKNOWN_OBJECT;
        return BACNET_STATUS_ERROR;
    }
    if (rp->array_index != BACNET_ARRAY_ALL && !is_array(rp->object_property))
    {
        rp->error_class = ERROR_CLASS_PROPERTY;
        rp->error_code = ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY;
        return BACNET_STATUS_ERROR;
    }

    take_snapshot();
    return o ? read_object(rp, o, apdu) : read_device(rp, apdu);
}

// --- WriteProperty ---------------------------------------------------------

static bool deny(BACNET_WRITE_PROPERTY_DATA *wp, BACNET_ERROR_CLASS cls, BACNET_ERROR_CODE code)
{
    wp->error_class = cls;
    wp->error_code = code;
    return false;
}

bool Device_Write_Property(BACNET_WRITE_PROPERTY_DATA *wp)
{
    const object_t *o = find(wp->object_type, wp->object_instance);
    if (!o && !(wp->object_type == OBJECT_DEVICE && wp->object_instance == rn_config.device_instance))
        return deny(wp, ERROR_CLASS_OBJECT, ERROR_CODE_UNKNOWN_OBJECT);

    bool writable = o && wp->object_property == PROP_PRESENT_VALUE &&
                    (o->type == OBJECT_ANALOG_VALUE || o->type == OBJECT_BINARY_OUTPUT ||
                     o->type == OBJECT_MULTI_STATE_VALUE);
    if (!writable)
        return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_WRITE_ACCESS_DENIED);
    if (wp->array_index != BACNET_ARRAY_ALL)
        return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY);

    // Commandable objects: only the operator level. A write without priority
    // means 16 (clause 15.9.1.1.3), which belongs to the thermostat.
    bool commandable = o->type != OBJECT_MULTI_STATE_VALUE;
    if (commandable && wp->priority != RN_BACNET_WRITE_PRIORITY)
        return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_WRITE_ACCESS_DENIED);

    BACNET_APPLICATION_DATA_VALUE value;
    if (bacapp_decode_application_data(wp->application_data, wp->application_data_len, &value) <= 0)
        return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_INVALID_DATA_TYPE);

    switch (o->type)
    {
    case OBJECT_ANALOG_VALUE:
        if (value.tag == BACNET_APPLICATION_TAG_NULL)
        {
            rn_access->relinquish_setpoint();
            return true;
        }
        if (value.tag != BACNET_APPLICATION_TAG_REAL)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_INVALID_DATA_TYPE);
        if (rn_access->write_setpoint(value.type.Real) != RC_OK)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_VALUE_OUT_OF_RANGE);
        return true;

    case OBJECT_BINARY_OUTPUT:
        if (value.tag == BACNET_APPLICATION_TAG_NULL)
            return rn_access->set_mode(RC_MODE_AUTO) == RC_OK;
        if (value.tag != BACNET_APPLICATION_TAG_ENUMERATED)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_INVALID_DATA_TYPE);
        if (value.type.Enumerated > BINARY_ACTIVE)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_VALUE_OUT_OF_RANGE);
        return rn_access->set_mode(value.type.Enumerated == BINARY_ACTIVE ? RC_MODE_ON : RC_MODE_OFF) == RC_OK;

    default: // OBJECT_MULTI_STATE_VALUE
        if (value.tag != BACNET_APPLICATION_TAG_UNSIGNED_INT)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_INVALID_DATA_TYPE);
        if (value.type.Unsigned_Int < 1 || value.type.Unsigned_Int > MODE_STATES)
            return deny(wp, ERROR_CLASS_PROPERTY, ERROR_CODE_VALUE_OUT_OF_RANGE);
        return rn_access->set_mode((rc_mode_t)(value.type.Unsigned_Int - 1)) == RC_OK;
    }
}
