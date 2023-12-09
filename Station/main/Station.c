#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "lvgl.h"
#include "esp_lcd_ili9341.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

/*Definitions*/
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10
#define SSID "Terminal_AP" 
#define PASS "super-strong-password"
#define PORT 12345
#define AP_IP "192.168.4.1"
#define LVGL_TICK_PERIOD_MS 2
#define TFT_BK_LIGHT_ON 1
#define TFT_BK_LIGHT_OFF !TFT_BK_LIGHT_ON

// Defining SPI
#define LCD_HOST  SPI2_HOST

// Pin definitoon
#define SCLK_PIN 18
#define MOSI_PIN 19
#define MISO_PIN 21
#define DC_PIN 5
#define RST_PIN 22
#define TFT_CS_PIN 4
#define BK_LIGHT_PIN 2
#define TOUCH_CS_PIN 15

// Display specs
#define TFT_PIXEL_CLOCK_HZ (20 * 1000* 1000) // 20 MHZ
#define HOR_RES 320           
#define VER_RES 240         

// Bit number used to represent command and parameter
#define CMD_BITS 8
#define PARAM_BITS 8

/*globals*/
// event group to contain status information
static EventGroupHandle_t wifi_event_group;

// number of retires 
static int wifi_try_no = 0;

// socket definition
int soc;
char buffer[1024];
lv_obj_t *label2;

// task tags
static const char *TAG_WI = "WIFI";
static const char *TAG_TCP = "TCP";
static const char *TFT_TAG = "Display";

// event handler for wifi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // behaviour in case of connecting to Acces_point
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI(TAG_WI, "Establishing connection with AP");
		esp_wifi_connect();
    // behaviour in case of disconnecting from Acces_point  
    /**(worked before adding lvgl, gotta fix)*/  
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (wifi_try_no < MAX_FAILURES)
		{
			ESP_LOGI(TAG_WI, "Reconnecting to AP");
			ESP_ERROR_CHECK(esp_wifi_connect());
			wifi_try_no++;
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
		}
	}
}

//event handler for ip events
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // behaviour after receiving IP from Ap
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WI, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_try_no = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
    }

}

esp_err_t connect_wifi()
{
	int status = WIFI_FAILURE;

	//initialize the esp network interface
	ESP_ERROR_CHECK(esp_netif_init());

	//initialize default esp event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	//create wifi station
	esp_netif_create_default_wifi_sta();

	//setup wifi station with the default wifi configuration
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Start of connection event loops
	wifi_event_group = xEventGroupCreate(); // <- output of wifi event lands here (fuction inits place where it lands)

    esp_event_handler_instance_t wifi_handler_event_instance;
    // checking for any wifi event with any wifi event type (connect/disconnect), and if it happens call wifi handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &wifi_handler_event_instance));

    esp_event_handler_instance_t got_ip_event_instance;
    // checking for any ip event and if it's getting ip, call ip event handler
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        &got_ip_event_instance));

    /** START THE WIFI DRIVER **/
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASS,
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    // set the wifi controller to be a station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );

    // set the wifi config
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

    // start the wifi driver
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WI, "STA initialization complete");

    /** NOW WE WAIT **/
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_SUCCESS | WIFI_FAILURE,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_SUCCESS) {
        ESP_LOGI(TAG_WI, "Connected to ap");
        status = WIFI_SUCCESS;
    } else if (bits & WIFI_FAILURE) {
        ESP_LOGI(TAG_WI, "Failed to connect to ap");
        status = WIFI_FAILURE;
    } else {
        ESP_LOGE(TAG_WI, "UNEXPECTED EVENT");
        status = WIFI_FAILURE;
    }

    /* The event wi ll not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_event_instance));
    vEventGroupDelete(wifi_event_group);

    return status;
}

// Connect to the socket of ap
esp_err_t socket_connection(void){
    struct sockaddr_in ap_info = {0};
    ap_info.sin_family = AF_INET;
    ap_info.sin_port = htons(PORT);
    // puting IP into address
    inet_pton(AF_INET, AP_IP, &ap_info.sin_addr);

    // socket creation
    soc = socket(AF_INET, SOCK_STREAM, 0);
    if(soc < 0){
        ESP_LOGI(TAG_TCP, "Socket creation Failed");
        return TCP_FAILURE;
    }
    ESP_LOGI(TAG_TCP, "Socket created succesfully");

    // creating socket connection
    if(connect(soc, (struct sockaddr *)&ap_info, sizeof(ap_info)) != 0){
        ESP_LOGI(TAG_TCP, "Unable to to connect to %s", inet_ntoa(ap_info.sin_addr.s_addr));
        close(soc);
        return TCP_FAILURE;
    }
    ESP_LOGI(TAG_TCP, "Connected to TCP server");
    return TCP_SUCCESS;
}

static void socket_read(void *arg){    
    while(1){
        bzero(buffer, sizeof(buffer));
        int r = read(soc, buffer, sizeof(buffer)-1);
        lv_label_set_text(label2, buffer);
        for(int i = 0; i < r; i++) {
            putchar(buffer[i]);
        }
    }
}

static bool lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}


void app_main(void)
{
    esp_err_t status = WIFI_FAILURE;
    // Initialize Non-volatile memory
    esp_err_t storage = nvs_flash_init();
    if(storage == ESP_ERR_NVS_NO_FREE_PAGES || storage == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        storage = nvs_flash_init();
    }
    ESP_ERROR_CHECK(storage);

    /*Probably gcould colse it inside a functon up untill lv_iit*/
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer
    static lv_disp_drv_t disp_drv;      // contains callback functions

    ESP_LOGI(TFT_TAG, " TFR backlight off");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BK_LIGHT_PIN
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));    

    // Creating SPI bus
    ESP_LOGI(TFT_TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = SCLK_PIN,
        .mosi_io_num = MOSI_PIN,
        .miso_io_num = MISO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HOR_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
 
    // Allocating LCD IO device handle
    ESP_LOGI(TFT_TAG, "Installing IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = DC_PIN,
        .cs_gpio_num = TFT_CS_PIN,
        .pclk_hz = TFT_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = CMD_BITS,
        .lcd_param_bits = PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    
    // TFT attachment to SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // Installing LCD controller drive
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = RST_PIN,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    // turing backlight on after initialization
    ESP_LOGI(TFT_TAG, "Turn on LCD backlight");
    gpio_set_level(BK_LIGHT_PIN, TFT_BK_LIGHT_ON);

    // Initialization of LVGL
    ESP_LOGI(TFT_TAG, "Initialize LVGL library");
    lv_init();

    // buffer allocation
    lv_color_t *buf1 = heap_caps_malloc(HOR_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(HOR_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers and ddriver
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, HOR_RES * 20);

    // display characteristics
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = HOR_RES;
    disp_drv.ver_res = VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };

    // time handler so that dispy knows passed time for operations or something
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Create display interface 
    lv_obj_t *label1 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP); 
    lv_label_set_text_static(label1, "Text received from paired device:");
    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 10);
 
    lv_obj_t *obj1 = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj1, 280, 60);
    lv_obj_align(obj1, LV_ALIGN_TOP_MID, 0, 30);

    label2 = lv_label_create(obj1);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_WRAP); 
    lv_label_set_text(label2, "Waiting for message");
    lv_obj_set_width(label2, 240);
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *label3 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label3, LV_LABEL_LONG_WRAP); 
    lv_label_set_text_static(label3, "Text input that will be \nsent to paired device:");
    lv_obj_align(label3, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *txt_area = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(txt_area, 280, 60);
    lv_obj_align(txt_area, LV_ALIGN_CENTER, 0, 65);
   

    // Connect to AP
    status = connect_wifi();
    if(status != WIFI_SUCCESS){
        ESP_LOGE(TAG_WI, "Failed to connect to AP");
    }

    // Connect to socket
    status = socket_connection();
    if(status != TCP_SUCCESS){
        ESP_LOGE(TAG_TCP, "Failed socket connection");
    }
    ESP_LOGE(TAG_WI, "Breaks here");
    if(status == TCP_SUCCESS){
        xTaskCreate(socket_read, "Socket receive task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    }
    ESP_LOGE(TAG_WI, "Breaks here 23123213");
    /*Try to find a way to put it inside a task withouti tcrashing the wgle programm*/
    while(1){
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    } 
    
}


