#include "tcp_duplex.h"
#include "pickup.h"
#include "status.h"

static const char *TAG = "TCP_DUPLEX_STREAM";
extern phone_status_t g_state;

static int _get_socket_error_code_reason(char *str, int sockfd)
{
    uint32_t optlen = sizeof(int);
    int result;
    int err;

    err = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &optlen);
    if (err == -1) {
        ESP_LOGE(TAG, "%s, getsockopt failed: ret=%d", str, err);
        return -1;
    }
    if (result != 0) {
        ESP_LOGW(TAG, "%s error, error code: %d, reason: %s", str, err, strerror(result));
    }
    return result;
}


static esp_err_t _dispatch_event(audio_element_handle_t el, tcp_duplex_t *tcp, void *data, int len, tcp_duplex_status_t state)
{
    if (el && tcp && tcp->hook) {
        tcp_duplex_event_msg_t msg = { 0 };
        msg.data = data;
        msg.data_len = len;
        msg.sock_fd = tcp->t;
        msg.source = el;
        return tcp->hook(&msg, state, tcp->ctx);
    }
    return ESP_FAIL;
}


static esp_err_t _tcpd_open(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    tcp_duplex_t *tcpd = (tcp_duplex_t *)audio_element_getdata(self);
    _dispatch_event(self, tcpd, NULL, 0, TCP_DUPLEX_STATE_CONNECTED);

    return ESP_OK;
}

static esp_err_t _tcpd_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    int rlen = 0, payload_len = 0;
    uint8_t header[5];
    
    tcp_duplex_t *tcpd = (tcp_duplex_t *)audio_element_getdata(self);
    rlen = esp_transport_read(tcpd->t, (char *)header, 4, tcpd->timeout_ms);

    /* Compute payload length. */
    payload_len = header[2] | (header[3] << 8);

    rlen = esp_transport_read(tcpd->t, (char *)buffer, len - 4, tcpd->timeout_ms);
    ESP_LOGD(TAG, "read len=%d, rlen=%d", len, rlen);
    
    if (phone_is_picked_up())
    {
      /* Phone has been picked up, return to normal. */
      g_state = PHONE_IDLE;
    
      if (rlen < 0) {
          _get_socket_error_code_reason("TCP read", tcpd->sock);
          return ESP_FAIL;
      } else if (rlen == 0) {
          ESP_LOGI(TAG, "Get end of the file");
      } else {
          audio_element_update_byte_pos(self, rlen);
      }
      return rlen;
    }
    else
    {
      if (payload_len != 0)
      {
        /* We are receiving data but phone is not picked up, make it ring ! */
        g_state = PHONE_RING;
        memset(buffer, 0, len);
      }
      else
        g_state = PHONE_IDLE;
      return len;
    }
}

static uint8_t _compute_crc(uint8_t *buffer, int len)
{
  uint8_t crc = 0xff;

  for (int i=0; i<len; i++)
  {
    crc = ((crc << 3) + buffer[i])&0xff;
  }

  return crc;
}

static esp_err_t _tcpd_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
  uint8_t header[5];

  if (phone_is_picked_up())
  {
    /* Craft our protocol header. */
    header[0] = 0x76; // protocol magic
    header[1] = 0xD1;
    header[2] = len & 0xff;
    header[3] = (len >> 8) & 0xff;

    tcp_duplex_t *tcpd = (tcp_duplex_t *)audio_element_getdata(self);
    int wlen = esp_transport_write(tcpd->t, (char *)header, 4, tcpd->timeout_ms);
    wlen = esp_transport_write(tcpd->t, (char *)buffer, len, tcpd->timeout_ms);
    if (wlen < 0)
    {
        _get_socket_error_code_reason("TCP write", tcpd->sock);
        return ESP_FAIL;
    }
    return wlen;
  }
  else
  {
    /* Craft our protocol header. */
    header[0] = 0x76; // protocol magic
    header[1] = 0xD1;
    header[2] = 0;
    header[3] = 0;

    tcp_duplex_t *tcpd = (tcp_duplex_t *)audio_element_getdata(self);
    int wlen = esp_transport_write(tcpd->t, (char *)header, 4, tcpd->timeout_ms);
    /* Don't send any audio data. 
    wlen = esp_transport_write(tcpd->t, (char *)buffer, len, tcpd->timeout_ms);
    */
    if (wlen < 0)
    {
        _get_socket_error_code_reason("TCP write", tcpd->sock);
        return ESP_FAIL;
    }
    return wlen;
  }

}

static esp_err_t _tcpd_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    if (r_size > 0) {
        w_size = audio_element_output(self, in_buffer, r_size);
        if (w_size > 0) {
            audio_element_update_byte_pos(self, r_size);
        }
    } else {
        w_size = r_size;
    }
    return w_size;
}

static esp_err_t _tcpd_close(audio_element_handle_t self)
{
    return ESP_OK;
}

static esp_err_t _tcpd_destroy(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    tcp_duplex_t *tcpd = (tcp_duplex_t *)audio_element_getdata(self);
    audio_free(tcpd);

    return ESP_OK;
}



audio_element_handle_t tcp_duplex_init(tcp_duplex_cfg_t *config, esp_transport_handle_t handle, int sock)
{
    AUDIO_NULL_CHECK(TAG, config, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    audio_element_handle_t el;
    cfg.open = _tcpd_open;
    cfg.close = _tcpd_close;
    cfg.process = _tcpd_process;
    cfg.destroy = _tcpd_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->ext_stack;
    cfg.tag = "tcp_duplex";
    if (cfg.buffer_len == 0) {
        cfg.buffer_len = TCP_DUPLEX_BUF_SIZE;
    }

    tcp_duplex_t *tcpd = audio_calloc(1, sizeof(tcp_duplex_t));
    AUDIO_MEM_CHECK(TAG, tcpd, return NULL);

    /* Configure socket. */
    tcpd->t = handle;
    tcpd->sock = sock;

    /* Configure type and handler. */
    tcpd->type = config->type;
    tcpd->timeout_ms = config->timeout_ms;
    if (config->event_handler) {
        tcpd->hook = config->event_handler;
        if (config->event_ctx) {
            tcpd->ctx = config->event_ctx;
        }
    }

    /* Both stream writer AND reader are accepted. */
    if (config->type == AUDIO_STREAM_WRITER) {
        cfg.write = _tcpd_write;
    } else {
        cfg.read = _tcpd_read;
    }

    el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, goto _tcpd_init_exit);
    audio_element_setdata(el, tcpd);

    return el;

_tcpd_init_exit:
    audio_free(tcpd);
    return NULL;
}

