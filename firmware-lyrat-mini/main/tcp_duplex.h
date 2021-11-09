#ifndef __INC_AUDIO_STREAM_TCP_H
#define __INC_AUDIO_STREAM_TCP_H

#include <stdio.h>
#include <string.h>

#include "lwip/sockets.h"
#include "esp_transport_tcp.h"
#include "esp_log.h"
#include "esp_err.h"
#include "audio_mem.h"
#include "audio_error.h"
#include "audio_element.h"
#include "esp_transport.h"

#define TCP_DUPLEX_DEFAULT_PORT             (8080)

#define TCP_DUPLEX_TASK_STACK               (6072)
#define TCP_DUPLEX_BUF_SIZE                 (2048)
#define TCP_DUPLEX_TASK_PRIO                (5)
#define TCP_DUPLEX_TASK_CORE                (0)

#define TCP_SERVER_DEFAULT_RESPONSE_LENGTH  (512)

#define TCP_DUPLEX_CFG_DEFAULT() {              \
    .type          = AUDIO_STREAM_READER,       \
    .timeout_ms    = 30 *1000,                  \
    .port          = TCP_DUPLEX_DEFAULT_PORT,   \
    .host          = NULL,                      \
    .task_stack    = TCP_DUPLEX_TASK_STACK,     \
    .task_core     = TCP_DUPLEX_TASK_CORE,      \
    .task_prio     = TCP_DUPLEX_TASK_PRIO,      \
    .ext_stack     = true,                      \
    .event_handler = NULL,                      \
    .event_ctx     = NULL,                      \
}


typedef enum {
    TCP_DUPLEX_STATE_NONE,
    TCP_DUPLEX_STATE_CONNECTED,
} tcp_duplex_status_t;

typedef struct tcp_stream_event_msg {
    void                          *source;          /*!< Element handle */
    void                          *data;            /*!< Data of input/output */
    int                           data_len;         /*!< Data length of input/output */
    esp_transport_handle_t        sock_fd;          /*!< handle of socket*/
} tcp_duplex_event_msg_t;

typedef esp_err_t (*tcp_duplex_event_handle_cb)(tcp_duplex_event_msg_t *msg, tcp_duplex_status_t state, void *event_ctx);


typedef struct {
    audio_stream_type_t         type;               /*!< Type of stream */
    int                         timeout_ms;         /*!< time timeout for read/write*/
    int                         port;               /*!< TCP port> */
    char                        *host;              /*!< TCP host> */
    int                         task_stack;         /*!< Task stack size */
    int                         task_core;          /*!< Task running in core (0 or 1) */
    int                         task_prio;          /*!< Task priority (based on freeRTOS priority) */
    bool                        ext_stack;          /*!< Allocate stack on extern ram */
    tcp_duplex_event_handle_cb  event_handler;      /*!< TCP stream event callback*/
    void                        *event_ctx;         /*!< User context*/
} tcp_duplex_cfg_t;


typedef struct tcp_full_stream {
    esp_transport_handle_t        t;
    audio_stream_type_t           type;
    int                           sock;
    int                           timeout_ms;
    tcp_duplex_event_handle_cb    hook;
    void                          *ctx;
} tcp_duplex_t;


/* Exported functions. */
audio_element_handle_t tcp_duplex_init(tcp_duplex_cfg_t *config, esp_transport_handle_t handle, int sock);


#endif /* __INC_AUDIO_STREAM_TCP_H */