/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_light.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

/*******************************************************
 *                Macros
 *******************************************************/

/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE          (1500)
#define TX_SIZE          (1460)

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = {0,};
static uint8_t rx_buf[RX_SIZE] = {0,};
static bool is_running = true;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;
static bool sr_sensor_up = false;
static mesh_addr_t parent_node;
static mesh_addr_t child_node;


mesh_light_ctl_t light_on = {
    .cmd = MESH_CONTROL_CMD,
    .on = 1,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

mesh_light_ctl_t light_off = {
    .cmd = MESH_CONTROL_CMD,
    .on = 0,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

/*******************************************************
 *                Function Declarations
 *******************************************************/

/*******************************************************
 *                Function Definitions
 *******************************************************/
void esp_mesh_p2p_tx_main(void *arg) {
    int i;
    esp_err_t err;
    int send_count = 0;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    mesh_data_t data;
    data.data = tx_buf;
    data.size = sizeof(tx_buf);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;

    while (is_running) {
        /* non-root do nothing but print */
        if (!esp_mesh_is_root()) {
            ESP_LOGI(MESH_TAG, "layer:%d, rtableSize:%d, %s", mesh_layer,
                     esp_mesh_get_routing_table_size(),
                     is_mesh_connected ? "NODE" : "DISCONNECT");
            vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
            continue;
        }
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
        if (send_count && !(send_count % 100)) {
            ESP_LOGI(MESH_TAG, "size:%d/%d,send_count:%d", route_table_size,
                     esp_mesh_get_routing_table_size(), send_count);
        }
        send_count++;
        tx_buf[25] = (send_count >> 24) & 0xff;
        tx_buf[24] = (send_count >> 16) & 0xff;
        tx_buf[23] = (send_count >> 8) & 0xff;
        tx_buf[22] = (send_count >> 0) & 0xff;
        if (send_count % 2) {
            memcpy(tx_buf, (uint8_t *) &light_on, sizeof(light_on));
        } else {
            memcpy(tx_buf, (uint8_t *) &light_off, sizeof(light_off));
        }

        for (i = 0; i < route_table_size; i++) {
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
            if (err) {
                ESP_LOGE(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d]parent:"MACSTR" to "MACSTR", heap:%" PRId32
                         "[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            } else if (!(send_count % 100)) {
                ESP_LOGW(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d][rtableSize:%d]parent:"MACSTR" to "MACSTR", heap:%" PRId32
                         "[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer,
                         esp_mesh_get_routing_table_size(),
                         MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            }
        }
        /* if route_table_size is less than 10, add delay to avoid watchdog in this task. */
        if (route_table_size < 10) {
            vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}


/***********************************************************************************/


#include "esp_log.h"
#include "esp_mesh.h"
#include <string.h>

static const char *TAG = "mesh_node";

void send_message_to_root(char *msg) {
    // Prepare the message
    mesh_data_t data;
    // const char *message = "Hello Root!";
    data.data = (uint8_t *) msg;
    data.size = strlen(msg) + 1;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    // Get the root's address
    mesh_addr_t root_addr;
    esp_err_t err = esp_mesh_get_parent_bssid(&root_addr); // Correct API call

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get root address: %s", esp_err_to_name(err));
        return;
    }

    // Send the message
    err = esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Message sent to root: %s", msg);
    } else {
        ESP_LOGE(TAG, "Failed to send message: %s", esp_err_to_name(err));
    }
}

void send_data_to_root(uint8_t *data_to_send_in_bytes, int size) {
    printf("DATA IN SEND : %.*s\n ", size, data_to_send_in_bytes);
    if (esp_mesh_is_root()) {
        ESP_LOGI(TAG, "I am the root, no need to send to myself.");
        return;
    }

    // Prepare the message
    mesh_data_t data;
    // const char *message = "Hello Root!";
    data.data = data_to_send_in_bytes;
    data.size = size;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    // Get the root's address
    mesh_addr_t root_addr;
    esp_err_t err = esp_mesh_get_parent_bssid(&root_addr); // Correct API call

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get root address: %s", esp_err_to_name(err));
        return;
    }

    // Send the message
    err = esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Message sent to root");
    } else {
        ESP_LOGE(TAG, "Failed to send message: %s", esp_err_to_name(err));
    }
}

void send_task(void *arg) {
    char *msg = "JUST CHECKING";
    while (1) {
        send_message_to_root(msg);
        vTaskDelay(pdMS_TO_TICKS(3000)); // Delay 10 seconds
    }
}


/***********************************************************************************/
#define PIR_GPIO GPIO_NUM_36  // or whatever you're using

void read_sr505_task(void *arg) {
    gpio_set_direction(PIR_GPIO, GPIO_MODE_INPUT);

    while (true) {
        int motion = gpio_get_level(PIR_GPIO);
        short layer = esp_mesh_get_layer();
        char layer_string[8];

        snprintf(layer_string, sizeof(layer_string), "%d", layer);
        if (motion == 1) {
            ESP_LOGI(TAG, "Motion detected!");
            send_message_to_root(layer_string); // your mesh send function
            vTaskDelay(pdMS_TO_TICKS(100)); // prevent spamming (cooldown)
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // polling delay
    }
}


void mesh_root_receive_task(void *arg) {
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    esp_err_t err;

    uint8_t rx_buf[256];
    data.data = rx_buf;
    data.size = sizeof(rx_buf);

    while (true) {
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err == ESP_OK) {
            printf("DATA : %.*s\n", 256, data.data);
            send_data_to_root(data.data, data.size);
        } else {
            ESP_LOGE("ROOT_RECV", "Receive error: %s", esp_err_to_name(err));
        }


        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_wifi.h"

#define MESH_TAG "MESH_DEBUG"

void print_mesh_connections() {
    mesh_addr_t parent;
    mesh_addr_t *route_table = NULL;

    // Print parent MAC
    if (esp_mesh_get_parent_bssid(&parent) == ESP_OK) {
        ESP_LOGI(MESH_TAG, "Parent MAC: " MACSTR, MAC2STR(parent.addr));
    } else {
        ESP_LOGI(MESH_TAG, "This node has no parent (likely the root).");
    }

    // Get routing table size
    int route_table_size = esp_mesh_get_routing_table_size();
    if (route_table_size == 0) {
        ESP_LOGI(MESH_TAG, "No child nodes connected.");
        return;
    }

    // Allocate memory for table
    route_table = malloc(sizeof(mesh_addr_t) * route_table_size);
    if (!route_table) {
        ESP_LOGE(MESH_TAG, "Failed to allocate memory for routing table.");
        return;
    }

    // Retrieve the routing table
    int table_size = 0;
    esp_mesh_get_routing_table(route_table, route_table_size * sizeof(mesh_addr_t), &table_size);

    ESP_LOGI(MESH_TAG, "Connected children: %d", table_size);
    for (int i = 0; i < table_size; ++i) {
        ESP_LOGI(MESH_TAG, "Child %d MAC: " MACSTR, i + 1, MAC2STR(route_table[i].addr));
    }

    free(route_table);
}

/***********/ //the func to print child and parent
bool is_mac_set(uint8_t mac[6]) {
    for (int i = 0; i < 6; ++i)
        if (mac[i] != 0)
            return true;
    return false;
}

void print_child_and_parent(void *arg) {


    while (true) {
        if (is_mac_set(child_node.addr)) {
            printf("CHILD OF THIS NODE : " MACSTR "\n", MAC2STR(child_node.addr));
        } else {
            printf("No child connected.\n");
        }

        if (is_mac_set(parent_node.addr)) {
            printf("PARENT OF THIS NODE : " MACSTR "\n", MAC2STR(parent_node.addr));
        } else {
            printf("No parent connected.\n");
        }


        vTaskDelay(pdMS_TO_TICKS(3000));


    }

}


/***********/


/***********************************************************************************/
void esp_mesh_p2p_rx_main(void *arg) {
    int recv_count = 0;
    esp_err_t err;
    mesh_addr_t from;
    int send_count = 0;
    mesh_data_t data;
    int flag = 0;
    data.data = rx_buf;
    data.size = RX_SIZE;
    is_running = true;

    while (is_running) {
        data.size = RX_SIZE;
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err != ESP_OK || !data.size) {
            ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
            continue;
        }
        /* extract send count */
        if (data.size >= sizeof(send_count)) {
            send_count = (data.data[25] << 24) | (data.data[24] << 16)
                         | (data.data[23] << 8) | data.data[22];
        }
        recv_count++;
        /* process light control */
        mesh_light_process(&from, data.data, data.size);
        if (!(recv_count % 1)) {
            ESP_LOGW(MESH_TAG,
                     "[#RX:%d/%d][L:%d] parent:"MACSTR", receive from "MACSTR", size:%d, heap:%" PRId32
                     ", flag:%d[err:0x%x, proto:%d, tos:%d]",
                     recv_count, send_count, mesh_layer,
                     MAC2STR(mesh_parent_addr.addr), MAC2STR(from.addr),
                     data.size, esp_get_minimum_free_heap_size(), flag, err, data.proto,
                     data.tos);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void) {
    static bool is_comm_p2p_started = false;
    if (!is_comm_p2p_started) {
        is_comm_p2p_started = true;
        xTaskCreate(esp_mesh_p2p_tx_main, "MPTX", 3072, NULL, 5, NULL);
        xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 3072, NULL, 5, NULL);
    }
    return ESP_OK;
}

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;

    switch (event_id) {
        case MESH_EVENT_STARTED: {
            esp_mesh_get_id(&id);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
        }
        break;
        case MESH_EVENT_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
        }
        break;
        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                     child_connected->aid,
                     MAC2STR(child_connected->mac));
            // print_mesh_connections();
            memcpy(&child_node, child_connected->mac, sizeof(child_node));
        }
        break;
        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                     child_disconnected->aid,
                     MAC2STR(child_disconnected->mac));
            // print_mesh_connections();
            memset(&child_node.addr, 0, sizeof(child_node));
        }
        break;
        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *) event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                     routing_table->rt_size_change,
                     routing_table->rt_size_new, mesh_layer);
        }
        break;
        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *) event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                     routing_table->rt_size_change,
                     routing_table->rt_size_new, mesh_layer);
        }
        break;
        case MESH_EVENT_NO_PARENT_FOUND: {
            mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                     no_parent->scan_times);
        }
        /* TODO handler for the failure */
        break;
        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t *connected = (mesh_event_connected_t *) event_data;
            esp_mesh_get_id(&id);
            mesh_layer = connected->self_layer;
            memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR", duty:%d",
                     last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                     esp_mesh_is_root() ? "<ROOT>" :
                     (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
            is_mesh_connected = true;
            if (esp_mesh_is_root()) {
                esp_netif_dhcpc_stop(netif_sta);
                esp_netif_dhcpc_start(netif_sta);
            }
            esp_mesh_comm_p2p_start();
            if (!sr_sensor_up) {
                xTaskCreate(read_sr505_task, "sr505_task", 4096, NULL, 5, NULL);
                sr_sensor_up = true;
            }
            // print_mesh_connections();
            memcpy(&parent_node, connected->connected.bssid, 6);
        }
        break;
        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *) event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                     disconnected->reason);
            is_mesh_connected = false;
            mesh_disconnected_indicator();
            mesh_layer = esp_mesh_get_layer();
            // print_mesh_connections();
        }
        break;
        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *) event_data;
            mesh_layer = layer_change->new_layer;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                     last_layer, mesh_layer,
                     esp_mesh_is_root() ? "<ROOT>" :
                     (mesh_layer == 2) ? "<layer2>" : "");
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
            print_mesh_connections();
        }
        break;
        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                     MAC2STR(root_addr->addr));
        }
        break;
        case MESH_EVENT_VOTE_STARTED: {
            mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *) event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                     vote_started->attempts,
                     vote_started->reason,
                     MAC2STR(vote_started->rc_addr.addr));
        }
        break;
        case MESH_EVENT_VOTE_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
            break;
        }
        case MESH_EVENT_ROOT_SWITCH_REQ: {
            mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *) event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                     switch_req->reason,
                     MAC2STR( switch_req->rc_addr.addr));
        }
        break;
        case MESH_EVENT_ROOT_SWITCH_ACK: {
            /* new root */
            mesh_layer = esp_mesh_get_layer();
            esp_mesh_get_parent_bssid(&mesh_parent_addr);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer,
                     MAC2STR(mesh_parent_addr.addr));
        }
        break;
        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
        }
        break;
        case MESH_EVENT_ROOT_FIXED: {
            mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                     root_fixed->is_fixed ? "fixed" : "not fixed");
        }
        break;
        case MESH_EVENT_ROOT_ASKED_YIELD: {
            mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *) event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                     MAC2STR(root_conflict->addr),
                     root_conflict->rssi,
                     root_conflict->capacity);
        }
        break;
        case MESH_EVENT_CHANNEL_SWITCH: {
            mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
        }
        break;
        case MESH_EVENT_SCAN_DONE: {
            mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                     scan_done->number);
        }
        break;
        case MESH_EVENT_NETWORK_STATE: {
            mesh_event_network_state_t *network_state = (mesh_event_network_state_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                     network_state->is_rootless);
        }
        break;
        case MESH_EVENT_STOP_RECONNECTION: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
        }
        break;
        case MESH_EVENT_FIND_NETWORK: {
            mesh_event_find_network_t *find_network = (mesh_event_find_network_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                     find_network->channel, MAC2STR(find_network->router_bssid));
        }
        break;
        case MESH_EVENT_ROUTER_SWITCH: {
            mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                     router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
        }
        break;
        case MESH_EVENT_PS_PARENT_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
        }
        break;
        case MESH_EVENT_PS_CHILD_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *) event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
                     MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
        }
        break;
        default:
            ESP_LOGI(MESH_TAG, "unknown id:%" PRId32 "", event_id);
            break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
}


void app_main(void) {
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("mesh", ESP_LOG_NONE);

    ESP_ERROR_CHECK(mesh_light_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#endif
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    esp_mesh_set_ap_connections(1); //TODO : TEST

    /* mesh start */
    /******/


    xTaskCreate(mesh_root_receive_task, "mesh_rx", 4096, NULL, 5, NULL);
    xTaskCreate(print_child_and_parent, "mesh_rx", 4096, NULL, 5, NULL);

    /******/


    ESP_ERROR_CHECK(esp_mesh_start());
    // xTaskCreate(send_task, "send_task", 4096, NULL, 5, NULL);
#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(
        esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION,
            CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d", esp_get_minimum_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());
}
