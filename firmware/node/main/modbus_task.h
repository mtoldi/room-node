#pragma once

// Modbus TCP server on port 502 (up to 4 clients, 60 s idle timeout).
// Protocol handling and the register map are in roomcore/modbus.c;
// this task only moves frames between sockets and that layer.
void modbus_task_start(void);
