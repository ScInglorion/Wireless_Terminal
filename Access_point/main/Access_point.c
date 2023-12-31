#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "driver/uart.h"
#include "driver/gpio.h"
/*Definitions*/
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10
#define SSID "Terminal_AP"
#define PASS "super-strong-password"
#define CHANNEL 11
#define MAX_DEV 4
#define PORT 12345
#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)
#define KEEPALIVE_IDLE              1
#define KEEPALIVE_INTERVAL          1
#define KEEPALIVE_COUNT             1

/*Globals*/
// Tags
static const char*WI_TAG  = "Wifi";
static const char *TCP_TAG  = "TCP";
static const char *UART_TAG  = "UART";
static const int RX_BUF_SIZE = 1024;

// socket definition
int sock;
int sockl;
static char buffer[1024];  
static char read_buffer[255];

// AP event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(WI_TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(WI_TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}


void wifi_init_softap(void)
{
    //initialize the esp network interface
    ESP_ERROR_CHECK(esp_netif_init());

    //initialize default esp event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create AP
    esp_netif_create_default_wifi_ap();

    //setup wifi AP with the default wifi configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   
    // checking for any wifi event with any wifi event type (connect/disconnect), and if it happens call wifi handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SSID,
            .ssid_len = strlen(SSID),
            .channel = CHANNEL,
            .password = PASS,
            .max_connection = MAX_DEV,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WI_TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             SSID, PASS, CHANNEL);
}



void socket_creation(void)
{
	struct sockaddr_in server ; 
    // int keepAlive = 1;
    // int keepIdle = KEEPALIVE_IDLE;
    // int keepInterval = KEEPALIVE_INTERVAL;
    // int keepCount = KEEPALIVE_COUNT;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

    // socket creation
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		ESP_LOGE(TCP_TAG, "Failed to create a socket");
	}
    // bind
    if(bind(sock, (struct sockaddr *) &server, sizeof(server)) != 0){
        ESP_LOGE(TCP_TAG, "Failed binding");
    }
    // listen
    int err = listen(sock, 5);
    if(err !=0){
        ESP_LOGE(TCP_TAG, "Failed listening");
    }

    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    sockl = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sockl < 0) {
        ESP_LOGE(TCP_TAG, "Unable to accept connection: errno %d", errno);                                                                                            
    }

    //bzero(Buffer, sizeof(Buffer));
   // int r = write(sock, Buffer, sizeof(Buffer)-1);

    // accept
    // clilen = sizeof(cli_addr);
    // int newsockfd = accept(sock, (struct sockaddr *) &cli_addr, &clilen);

    // if (newsockfd != 1){
    //     ESP_LOGE(TAG, "btuh");
    // }


//     while (1) {

//         ESP_LOGI(TCP_TAG, "Socket listening");

//         struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
//         socklen_t addr_len = sizeof(source_addr);
//         int sockl = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
//         if (sockl < 0) {
//             ESP_LOGE(TCP_TAG, "Unable to accept connection: errno %d", errno);
//             ESP_LOGE(TCP_TAG, "wagt");
//             break;
//         }

//         // Set tcp keepalive option
//         setsockopt(sockl, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
//         setsockopt(sockl, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
//         setsockopt(sockl, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
//         setsockopt(sockl, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
//         // Convert ip address to string
// // #ifdef CONFIG_EXAMPLE_IPV4
// //         if (source_addr.ss_family == PF_INET) {
// //             inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
// //         }
// // #endif
//         ESP_LOGI(TCP_TAG, "Socket accepted ip address: ");

//         //do_retransmit(sock);
//          ESP_LOGE(TCP_TAG, "wagt");
//         shutdown(sockl, 0);
//         close(sockl);
        
//    }
}

void init(void) 
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    ESP_LOGI("len", "%i", len);
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

static void tx_task(void *arg)
{
    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
    // while (1) {
    //     sendData(TX_TASK_TAG, "Hello world");
    //     vTaskDelay(2000 / portTICK_PERIOD_MS);
    // }
    while(1){
        bzero(read_buffer, sizeof(read_buffer));
        int r = read(sockl, read_buffer, sizeof(read_buffer));
        if (r > 0){
            ESP_LOGI("socket", "%i", r);
            ESP_LOGI("socket", "%s", read_buffer);
            for(int i = 0; i < r; i++) {
                putchar(read_buffer[i]);
            }
            sendData(TX_TASK_TAG, read_buffer);
            // uart_write_bytes(UART_NUM_1, read_buffer, TX_BUF_SIZE);        
        }

    }
}  

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    while (1) {
        bzero(data, 1024);
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            bzero(buffer, sizeof(buffer));   
            int r = write(sockl, data, rxBytes);
            ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);             
        }
    }
    free(data); 
}


void app_main(void)
{
    esp_err_t storage = nvs_flash_init();
    if (storage == ESP_ERR_NVS_NO_FREE_PAGES || storage == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      storage = nvs_flash_init();
    }
    ESP_ERROR_CHECK(storage);
    // Creat AP
    wifi_init_softap();
    // Create socket
    socket_creation();
    ESP_LOGE(TCP_TAG, "here1");
    init();
    ESP_LOGE(TCP_TAG, "here2");
    xTaskCreate(rx_task, "uart_rx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    ESP_LOGE(TCP_TAG, "here3");
    xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);
}   
