# BACnet/IP: encoding, services and BVLC from bacnet-stack (git submodule,
# GPL-2.0 with the GCC linking exception); Device object, room objects and
# the UDP datalink are our own.

set(STACK bacnet-stack/src/bacnet)

file(GLOB STACK_CORE ${STACK}/*.c)
list(FILTER STACK_CORE EXCLUDE REGEX "secure_connect\\.c$")

set(STACK_SRCS
    ${STACK_CORE}
    ${STACK}/basic/service/h_apdu.c
    ${STACK}/basic/service/h_noserv.c
    ${STACK}/basic/service/h_rp.c
    ${STACK}/basic/service/h_rpm.c
    ${STACK}/basic/service/h_wp.c
    ${STACK}/basic/service/h_whois.c
    ${STACK}/basic/service/s_iam.c
    ${STACK}/basic/npdu/h_npdu.c
    ${STACK}/basic/tsm/tsm.c
    ${STACK}/basic/binding/address.c
    ${STACK}/basic/bbmd/h_bbmd.c
    ${STACK}/basic/sys/bigend.c
    ${STACK}/basic/sys/days.c
    ${STACK}/basic/sys/debug.c
    ${STACK}/basic/sys/dst.c
    ${STACK}/basic/sys/fifo.c
    ${STACK}/basic/sys/keylist.c
    ${STACK}/basic/sys/linear.c
    ${STACK}/basic/sys/color_rgb.c
    ${STACK}/basic/sys/lighting_command.c
    ${STACK}/basic/sys/mstimer.c
    ${STACK}/basic/sys/ringbuf.c
    ${STACK}/basic/sys/sbuf.c
    ${STACK}/datalink/bvlc.c
)

idf_component_register(
    SRCS "rn_bacnet.c" "rn_objects.c" "bip_esp.c" ${STACK_SRCS}
    INCLUDE_DIRS "include" "bacnet-stack/src"
    REQUIRES roomcore lwip esp_netif log
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    BACDL_BIP
    BACNET_STACK_STATIC_DEFINE
    BACNET_PROTOCOL_REVISION=28
    PRINT_ENABLED=0
    # Sized for a server: the defaults reserve ~400 KB of RAM, mostly for 255
    # client transactions (TSM) that a node which never initiates confirmed
    # requests does not need. It is not a BBMD either.
    MAX_TSM_TRANSACTIONS=0
    MAX_ADDRESS_CACHE=16
    BBMD_ENABLED=0
)

# Third-party code: keep its warnings out of our build log
set_source_files_properties(${STACK_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
