/* Audio passthru

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "board.h"

#include "audio_error.h"
#include "audio_element.h"
#include "audio_alc.h"
#include "esp_alc.h"
#include "audio_mem.h"
#include "filter_resample.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "input_key_service.h"

#include "esp_transport_tcp.h"
#include "tcp_duplex.h"
#include "pickup.h"
#include "status.h"


#define CONNECT_TIMEOUT_MS        4000

#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#else
#define ESP_IDF_VERSION_VAL(major, minor, patch) 1
#endif

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "DRING";
static audio_board_handle_t board_handle;
phone_status_t g_state = PHONE_BOOT;

static audio_element_handle_t create_filter(int source_rate, int source_channel, int dest_rate, int dest_channel, int mode)
{
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = source_rate;
    rsp_cfg.src_ch = source_channel;
    rsp_cfg.dest_rate = dest_rate;
    rsp_cfg.dest_ch = dest_channel;
    rsp_cfg.mode = mode;
    rsp_cfg.complexity = 0;
    return rsp_filter_init(&rsp_cfg);
}

static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    audio_element_handle_t http_stream_writer = (audio_element_handle_t)ctx;
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_VOLUP:
                phone_pick_up();
                audio_hal_set_volume(board_handle->audio_hal, 50);
                break;
        }
    } else if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE || evt->type == INPUT_KEY_SERVICE_ACTION_PRESS_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_VOLUP:
                phone_hang_up();
                audio_hal_set_volume(board_handle->audio_hal, 0);
                break;
        }
    }

    return ESP_OK;
}

/**
 * This task handles phone's LED to indicate status (ringing, connecting, etc ...)
 **/

void handlePhoneState(void *pv_parameters)
{
  int count = 0;
  int state = 0;

  while (1)
  {
    switch(g_state)
    {
      case PHONE_BOOT:
        {
          /* LED must be on and still. */
          gpio_set_level(GPIO_NUM_15, 1);
          state = 1;
        }
        break;

      case PHONE_WIFI:
        {
          /* LED must blink at 5Hz. */
          if ((count % 2) == 0)
          {
            if (state)
            {
              gpio_set_level(GPIO_NUM_15, 0);
              state = 0;
            }
            else
            {
              gpio_set_level(GPIO_NUM_15, 1);
              state = 1;
            }

            count = 0;
          }
          count++;
        }
        break;

      case PHONE_CONN:
        {
          /* LED must blink at 1Hz. */
          if ((count % 10) == 0)
          {
            if (state)
            {
              gpio_set_level(GPIO_NUM_15, 0);
              state = 0;
            }
            else
            {
              gpio_set_level(GPIO_NUM_15, 1);
              state = 1;
            }

            count = 0;
          }
          count++;
        }
        break;

      case PHONE_IDLE:
        {
          gpio_set_level(GPIO_NUM_15, 0);
          state = 0;
        }
        break;

      case PHONE_RING:
        {
          /* LED must blink at 2Hz. */
          if ((count % 5) == 0)
          {
            if (state)
            {
              gpio_set_level(GPIO_NUM_15, 0);
              state = 0;
            }
            else
            {
              gpio_set_level(GPIO_NUM_15, 1);
              state = 1;
            }

            count = 0;
          }
          count++;
        }
        break;

      case PHONE_ERROR:
        {
          /* Error, LED is always ON. */
          gpio_set_level(GPIO_NUM_15, 1);
          state = 1;
        }
        break;

      default:
        break;
    }
    vTaskDelay(100/portTICK_RATE_MS);
  }
}

/**
 * Main task.
 **/

void app_main(void)
{
  int sock;
  esp_transport_handle_t t;
  audio_pipeline_handle_t pipeline_tx, pipeline_rx;
  audio_element_handle_t i2s_stream_writer, i2s_stream_reader;
  audio_element_handle_t debugger;
  audio_element_handle_t tcp_sender;
  audio_element_handle_t tcp_reader;

  //esp_log_level_set(TAG, ESP_LOG_DEBUG);

  /* Booting ... */
  g_state = PHONE_BOOT;

  /* Configure LED. */
  gpio_pad_select_gpio(GPIO_NUM_15);
  gpio_set_direction(GPIO_NUM_15, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_15, 0);

  /* Start phone status manager. */
  xTaskCreate(handlePhoneState, "phone_led", 10000, NULL, 12, NULL);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  }


  //tcpip_adapter_init();
  esp_netif_init();

  ESP_LOGI(TAG, "[ 1 ] Start codec chip");
  board_handle = audio_board_init();

  audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

  ESP_LOGI(TAG, "[ 2 ] Create audio pipeline for playback");
  audio_pipeline_cfg_t pipeline_tx_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
  pipeline_tx = audio_pipeline_init(&pipeline_tx_cfg);

  audio_pipeline_cfg_t pipeline_rx_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
  pipeline_rx = audio_pipeline_init(&pipeline_rx_cfg);

  ESP_LOGI(TAG, "[3.2] Create i2s stream to read data from codec chip");
  i2s_stream_cfg_t i2s_cfg_read = I2S_STREAM_CFG_DEFAULT();
  i2s_cfg_read.type = AUDIO_STREAM_READER;
  i2s_cfg_read.i2s_port = 1;
  i2s_stream_reader = i2s_stream_init(&i2s_cfg_read);

  ESP_LOGI(TAG, "[3.1] Create i2s stream to write data to codec chip");
  i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
  i2s_cfg.type = AUDIO_STREAM_WRITER;
  i2s_stream_writer = i2s_stream_init(&i2s_cfg);


  /* Connecting to WiFi network ... */
  g_state = PHONE_WIFI;

  /* Connect to WiFi */
  ESP_LOGI(TAG, "Connecting to WiFi Network ...");
  esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
  esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);


  audio_board_key_init(set);
  input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
  input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
  input_cfg.handle = set;
  periph_service_handle_t input_ser = input_key_service_create(&input_cfg);
  input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
  periph_service_set_callback(input_ser, input_key_service_cb, NULL);

  periph_wifi_cfg_t wifi_cfg = {
      .ssid = CONFIG_WIFI_SSID,
      .password = CONFIG_WIFI_PASSWORD,
  };
  esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
  esp_periph_start(set, wifi_handle);
  periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

  /* We are connected. */
  ESP_LOGI(TAG, "Connected to network");


  /* Connecting to VoIP service ... */
  g_state = PHONE_CONN;

  ESP_LOGI(TAG, "Connecting to %s:%d", CONFIG_TCP_URL, CONFIG_TCP_PORT);

  /* Connect to our server. */
  esp_transport_list_handle_t transport_list = esp_transport_list_init();
  t = esp_transport_tcp_init();
  
  AUDIO_NULL_CHECK(TAG, t, return ESP_FAIL);
  if (t == NULL)
    ESP_LOGE(TAG, "Cannot init ESP Transport");
  else
    ESP_LOGI(TAG, "ESP Transport successfully init");

  esp_transport_list_add(transport_list, t, "tcp");

  sock = esp_transport_connect(t, CONFIG_TCP_URL, CONFIG_TCP_PORT, CONNECT_TIMEOUT_MS);
  if (sock < 0) {
    ESP_LOGE(TAG, "Cannot connect to server");
    tcp_sender = NULL;
    tcp_reader = NULL;
  }
  else
  {
    /* Connected, idle. */
    g_state = PHONE_IDLE;

    ESP_LOGI(TAG, "#[3.2] Create tcp sender");
    tcp_duplex_cfg_t tcp_cfg = TCP_DUPLEX_CFG_DEFAULT();
    tcp_cfg.type = AUDIO_STREAM_WRITER;
    tcp_sender = tcp_duplex_init(&tcp_cfg, t, sock);
    AUDIO_NULL_CHECK(TAG, tcp_sender, return);

    ESP_LOGI(TAG, "#[3.2] Create tcp sender");
    tcp_duplex_cfg_t tcp2_cfg = TCP_DUPLEX_CFG_DEFAULT();
    tcp2_cfg.type = AUDIO_STREAM_READER;
    tcp_reader = tcp_duplex_init(&tcp2_cfg, t, sock);
    AUDIO_NULL_CHECK(TAG, tcp_reader, return);


    audio_element_handle_t resampler =  create_filter(44100, 2, 44100, 1, RESAMPLE_ENCODE_MODE);

    /* Create TX pipeline. */
    ESP_LOGI(TAG, "[3.3] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_tx, i2s_stream_reader, "i2s_read");
    audio_pipeline_register(pipeline_tx, resampler, "resampler");
    audio_pipeline_register(pipeline_tx, tcp_sender, "tcp_sender");

    

    ESP_LOGI(TAG, "[3.4] Link it together [adc]-->i2s_stream_reader-->resampler-->tcp_duplex]");
    const char *link_tag[3] = {"i2s_read", "resampler", "tcp_sender"};
    audio_pipeline_link(pipeline_tx, &link_tag[0], 3);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);


    audio_pipeline_register(pipeline_rx, tcp_reader, "tcp_reader");
    audio_pipeline_register(pipeline_rx, i2s_stream_writer, "i2s_write");
    
    ESP_LOGI(TAG, "[3.5] Link it together tcp_duplex-->i2s_stream_writer-->[codec_chip]");
    const char *link_tag_[2] = {"tcp_reader", "i2s_write"};
    audio_pipeline_link(pipeline_rx, &link_tag_[0], 2);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline_tx, evt);

    audio_hal_set_volume(board_handle->audio_hal, 50);

    i2s_stream_set_clk(i2s_stream_writer, 44100*2, 16, 4);

    /* Start pipeline. */
    ESP_LOGI(TAG, "[ 5 ] Start TX pipeline");
    audio_pipeline_run(pipeline_tx);

    ESP_LOGI(TAG, "[ 5 ] Start RX pipeline");
    audio_pipeline_run(pipeline_rx);


    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline_tx);
    audio_pipeline_wait_for_stop(pipeline_tx);
    audio_pipeline_terminate(pipeline_tx);

    audio_pipeline_stop(pipeline_rx);
    audio_pipeline_wait_for_stop(pipeline_rx);
    audio_pipeline_terminate(pipeline_rx);


    audio_pipeline_unregister(pipeline_tx, i2s_stream_reader);
    audio_pipeline_unregister(pipeline_rx, i2s_stream_writer);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline_tx);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline_tx);
    audio_pipeline_deinit(pipeline_rx);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(i2s_stream_writer);
  }
}
