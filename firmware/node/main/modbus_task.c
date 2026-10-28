#include "modbus_task.h"

#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "modbus.h"
#include "room_state.h"

#define PORT            502
#define MAX_CLIENTS     4
#define IDLE_TIMEOUT_US (60LL * 1000 * 1000)
#define TASK_PRIORITY   4
#define TASK_STACK      4096

static const char *TAG = "modbus";

typedef struct
{
    int fd; // -1 when the slot is free
    uint8_t buf[MB_TCP_MAX_ADU];
    size_t len;
    int64_t last_us;
} client_t;

static client_t s_clients[MAX_CLIENTS];

static rc_result_t write_setpoint(void *ctx, float celsius)
{
    (void)ctx;
    ESP_LOGI(TAG, "setpoint override %.2f", celsius);
    return room_state_write_setpoint(celsius);
}

static void relinquish_setpoint(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "setpoint override relinquished");
    room_state_relinquish_setpoint();
}

static rc_result_t set_mode(void *ctx, rc_mode_t mode)
{
    (void)ctx;
    ESP_LOGI(TAG, "mode %s", rc_mode_name(mode));
    return room_state_set_mode(mode);
}

static void drop(client_t *c, const char *why)
{
    ESP_LOGI(TAG, "client %d closed (%s)", c->fd, why);
    close(c->fd);
    c->fd = -1;
}

static bool send_all(int fd, const uint8_t *data, int len)
{
    while (len > 0)
    {
        int n = send(fd, data, len, 0);
        if (n <= 0)
            return false;
        data += n;
        len -= n;
    }
    return true;
}

static void serve_frame(client_t *c)
{
    room_state_t s;
    rc_points_t p;
    room_state_get(&s);
    room_state_points(&s, &p);
    mb_map_t map = {
        .points = &p,
        .write_setpoint = write_setpoint,
        .relinquish_setpoint = relinquish_setpoint,
        .set_mode = set_mode,
    };

    uint8_t resp[MB_TCP_MAX_ADU];
    int n = mb_tcp_handle(&map, c->buf, c->len, resp, sizeof(resp));
    c->len = 0;
    if (n < 0)
        drop(c, "malformed frame");
    else if (n > 0 && !send_all(c->fd, resp, n))
        drop(c, "send failed");
}

// Reads what is available, at most up to the end of the current frame,
// so pipelined requests are handled one by one.
static void receive(client_t *c)
{
    size_t want = MB_TCP_HEADER_LEN;
    if (c->len >= MB_TCP_HEADER_LEN)
    {
        int frame = mb_tcp_frame_length(c->buf);
        if (frame < 0)
        {
            drop(c, "not Modbus TCP");
            return;
        }
        want = frame;
    }
    int n = recv(c->fd, c->buf + c->len, want - c->len, 0);
    if (n <= 0)
    {
        drop(c, n == 0 ? "peer closed" : "receive error");
        return;
    }
    c->len += n;
    c->last_us = esp_timer_get_time();

    if (c->len == MB_TCP_HEADER_LEN && mb_tcp_frame_length(c->buf) < 0)
        drop(c, "not Modbus TCP");
    else if (c->len > MB_TCP_HEADER_LEN && (int)c->len == mb_tcp_frame_length(c->buf))
        serve_frame(c);
}

static void accept_client(int listener)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(listener, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0)
        return;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (s_clients[i].fd < 0)
        {
            s_clients[i] = (client_t){.fd = fd, .len = 0, .last_us = esp_timer_get_time()};
            ESP_LOGI(TAG, "client %d connected from %s", fd, inet_ntoa(addr.sin_addr));
            return;
        }
    }
    ESP_LOGW(TAG, "connection from %s refused, %d clients already connected", inet_ntoa(addr.sin_addr),
             MAX_CLIENTS);
    close(fd);
}

static int open_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 2) != 0)
    {
        ESP_LOGE(TAG, "cannot listen on port %d (errno %d)", PORT, errno);
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return fd;
}

static void modbus_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < MAX_CLIENTS; i++)
        s_clients[i].fd = -1;

    int listener = open_listener();
    configASSERT(listener >= 0);
    ESP_LOGI(TAG, "server listening on port %d", PORT);
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    for (;;)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listener, &fds);
        int max_fd = listener;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (s_clients[i].fd >= 0)
            {
                FD_SET(s_clients[i].fd, &fds);
                if (s_clients[i].fd > max_fd)
                    max_fd = s_clients[i].fd;
            }
        }

        struct timeval timeout = {.tv_sec = 1};
        int ready = select(max_fd + 1, &fds, NULL, NULL, &timeout);
        esp_task_wdt_reset();

        if (ready > 0 && FD_ISSET(listener, &fds))
            accept_client(listener);

        int64_t now = esp_timer_get_time();
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            client_t *c = &s_clients[i];
            if (c->fd < 0)
                continue;
            if (ready > 0 && FD_ISSET(c->fd, &fds))
                receive(c);
            else if (now - c->last_us > IDLE_TIMEOUT_US)
                drop(c, "idle");
        }
    }
}

void modbus_task_start(void)
{
    xTaskCreatePinnedToCore(modbus_task, "modbus_task", TASK_STACK, NULL, TASK_PRIORITY, NULL, 1);
}
