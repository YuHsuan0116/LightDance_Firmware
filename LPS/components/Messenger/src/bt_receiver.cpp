/*
 * bt_receiver.cpp
 */

#include "bt_receiver.h"
#include <stdlib.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "player.hpp"

// --- HCI Command Definitions ---
// Macros and constants for building raw HCI packets sent directly to the Bluetooth controller.
#define HCI_H4_CMD_PREAMBLE_SIZE (4)
#define HCI_GRP_HOST_CONT_BASEBAND_CMDS (0x03 << 10)
#define HCI_GRP_BLE_CMDS (0x08 << 10)
#define HCI_RESET (0x0003 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)
#define HCI_SET_EVT_MASK (0x0001 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)
#define HCI_BLE_WRITE_SCAN_PARAM (0x000B | HCI_GRP_BLE_CMDS)
#define HCI_BLE_WRITE_SCAN_ENABLE (0x000C | HCI_GRP_BLE_CMDS)
#define HCIC_PARAM_SIZE_SET_EVENT_MASK (8)
#define HCIC_PARAM_SIZE_BLE_WRITE_SCAN_PARAM (7)
#define HCIC_PARAM_SIZE_BLE_WRITE_SCAN_ENABLE (2)
#define CMD_TYPE_ACK 0x07
#define UUID1 -1 // change into real uuid
#define UUID2 -1 // change into real uuid

// Helper macros to serialize data into the HCI buffer (Little-Endian format)
#define UINT16_TO_STREAM(p, u16)        \
    {                                   \
        *(p)++ = (uint8_t)(u16);        \
        *(p)++ = (uint8_t)((u16) >> 8); \
    }
#define UINT8_TO_STREAM(p, u8) \
    { *(p)++ = (uint8_t)(u8); }
#define ARRAY_TO_STREAM(p, a, len)     \
    {                                  \
        int ijk;                       \
        for(ijk = 0; ijk < len; ijk++) \
            *(p)++ = (uint8_t)a[ijk];  \
    }

enum { H4_TYPE_COMMAND = 1, H4_TYPE_ACL = 2, H4_TYPE_SCO = 3, H4_TYPE_EVENT = 4 };

static const char* TAG = "BT_RECEIVER";

// --- Internal Static Variables ---
static bt_receiver_config_t s_config;       // Stores initialization configs (e.g., player ID, window size)
static QueueHandle_t s_adv_queue = NULL;    // FreeRTOS Queue bridging the ISR and the sync processing task
static TaskHandle_t s_task_handle = NULL;   // Handle for the sync_process_task
static bool s_is_running = false;           // Indicates if the receiver is currently scanning
static uint8_t hci_cmd_buf[128];            // Buffer for formatting HCI commands

// Context structure for a specific scheduled command mapped from the BLE payload
typedef struct {
    volatile uint8_t target_cmd;    // Command action type (e.g., PLAY, PAUSE)
    volatile uint64_t target_mask;  // Target device bitmask (8 bytes)
    volatile uint8_t data[3];       // Extra parameters (e.g., RGB values for TEST)
} bt_action_context_t;

#define MAX_CONCURRENT_ACTIONS 16

// --- Timer Slot Structure ---
// Represents a single execution slot in the hardware timer queue
typedef struct {
    esp_timer_handle_t timer_handle; // Timer to trigger the actual action after the calculated delay
    bt_action_context_t ctx;         // Command context bound to this timer
} action_slot_t;

static action_slot_t s_slots[MAX_CONCURRENT_ACTIONS];            // Array of timer slots mapping to CMD IDs
static bool s_visual_ack_done[MAX_CONCURRENT_ACTIONS] = {false}; // Prevents duplicate visual ACKs for the same command ID
static esp_timer_handle_t s_led_timer = NULL;                    // Global timer to turn off the prep LED

// Caches the latest valid command info to build the payload for an ACK (CHECK) response
static struct {
    uint8_t cmd_id;
    uint8_t cmd_type;
    uint32_t original_delay;
    int64_t lock_timestamp;
} s_last_locked_cmd = {0, 0, 0, 0};

// ==========================================
// Part 1: HCI Helper Functions
// Formats raw HCI commands to control the BLE controller directly, bypassing the standard host stack.
// ==========================================

static uint16_t make_cmd_reset(uint8_t* buf) {
    UINT8_TO_STREAM(buf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(buf, HCI_RESET);
    UINT8_TO_STREAM(buf, 0);
    return HCI_H4_CMD_PREAMBLE_SIZE;
}

static uint16_t make_cmd_set_evt_mask(uint8_t* buf, uint8_t* evt_mask) {
    UINT8_TO_STREAM(buf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(buf, HCI_SET_EVT_MASK);
    UINT8_TO_STREAM(buf, HCIC_PARAM_SIZE_SET_EVENT_MASK);
    ARRAY_TO_STREAM(buf, evt_mask, HCIC_PARAM_SIZE_SET_EVENT_MASK);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_SET_EVENT_MASK;
}

static uint16_t make_cmd_ble_set_scan_params(uint8_t* buf,
                                             uint8_t scan_type,
                                             uint16_t scan_interval,
                                             uint16_t scan_window,
                                             uint8_t own_addr_type,
                                             uint8_t filter_policy) {
    UINT8_TO_STREAM(buf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(buf, HCI_BLE_WRITE_SCAN_PARAM);
    UINT8_TO_STREAM(buf, HCIC_PARAM_SIZE_BLE_WRITE_SCAN_PARAM);
    UINT8_TO_STREAM(buf, scan_type);
    UINT16_TO_STREAM(buf, scan_interval);
    UINT16_TO_STREAM(buf, scan_window);
    UINT8_TO_STREAM(buf, own_addr_type);
    UINT8_TO_STREAM(buf, filter_policy);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_BLE_WRITE_SCAN_PARAM;
}

static uint16_t make_cmd_ble_set_scan_enable(uint8_t* buf, uint8_t scan_enable, uint8_t filter_duplicates) {
    UINT8_TO_STREAM(buf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(buf, HCI_BLE_WRITE_SCAN_ENABLE);
    UINT8_TO_STREAM(buf, HCIC_PARAM_SIZE_BLE_WRITE_SCAN_ENABLE);
    UINT8_TO_STREAM(buf, scan_enable);
    UINT8_TO_STREAM(buf, filter_duplicates);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_BLE_WRITE_SCAN_ENABLE;
}

// Packages custom data into a BLE Advertising packet
static uint16_t make_cmd_ble_set_adv_data(uint8_t *buf, uint8_t data_len, uint8_t *p_data) {
    UINT8_TO_STREAM(buf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(buf, 0x2008); // HCI_BLE_WRITE_ADV_DATA
    UINT8_TO_STREAM(buf, 32);      // HCI Param Length (Fixed 32)
    UINT8_TO_STREAM(buf, data_len); // Adv Data Length
    ARRAY_TO_STREAM(buf, p_data, data_len);
    uint8_t pad_len = 31 - data_len; // Pad the rest of the 31-byte limit with zeros
    for (int i = 0; i < pad_len; i++) {
        UINT8_TO_STREAM(buf, 0); 
    }
    return HCI_H4_CMD_PREAMBLE_SIZE + 1 + 31;
}

static void hci_cmd_send_ble_adv_enable(uint8_t enable) {
    uint8_t buf[128];
    uint8_t *p = buf;
    UINT8_TO_STREAM(p, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(p, 0x200A); // HCI_BLE_WRITE_ADV_ENABLE
    UINT8_TO_STREAM(p, 1);
    UINT8_TO_STREAM(p, enable);
    esp_vhci_host_send_packet(buf, p - buf);
}

static void hci_cmd_send_ble_set_adv_param_ack(void) {
    uint8_t buf[128];
    uint8_t *p = buf;
    uint16_t interval = 32; 
    
    UINT8_TO_STREAM(p, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(p, 0x2006); // HCI_BLE_WRITE_ADV_PARAMS
    UINT8_TO_STREAM(p, 15);
    UINT16_TO_STREAM(p, interval); // Min interval
    UINT16_TO_STREAM(p, interval); // Max interval
    
    UINT8_TO_STREAM(p, 0); // Use ADV_IND (Connectable undirected advertising)
    
    UINT8_TO_STREAM(p, 0); // Own addr type
    UINT8_TO_STREAM(p, 0); // Peer addr type
    UINT8_TO_STREAM(p, 0); UINT8_TO_STREAM(p, 0); UINT8_TO_STREAM(p, 0); UINT8_TO_STREAM(p, 0); UINT8_TO_STREAM(p, 0); UINT8_TO_STREAM(p, 0); 
    UINT8_TO_STREAM(p, 0x07); // Channel Map (All 3 advertising channels)
    UINT8_TO_STREAM(p, 0); // Filter Policy
    
    esp_vhci_host_send_packet(buf, p - buf);
}

// Parameters required for the ACK broadcasting task
typedef struct {
    uint8_t my_id;
    uint8_t cmd_id;
    uint8_t cmd_type;
    uint32_t delay_val;
    uint8_t state;
} ack_task_params_t;

// Background task: Temporarily halts scanning to broadcast an ACK packet, then resumes scanning
static void send_ack_task(void *arg) {
    ack_task_params_t *params = (ack_task_params_t *)arg;
    
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Stop Scanning First to release the RF path for TX.
    uint8_t scan_buf[32];
    make_cmd_ble_set_scan_enable(scan_buf, 0, 0); 
    esp_vhci_host_send_packet(scan_buf, 6);
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_LOGD(TAG, ">>> ACK START: ID=%d, CMD=%d (Scan Stopped)", params->my_id, params->cmd_id);

    // Configure Advertising Parameters
    hci_cmd_send_ble_set_adv_param_ack();
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Build Payload Data Structure
    uint8_t raw_data[31];
    uint8_t idx = 0;
    
    raw_data[idx++] = 2; raw_data[idx++] = 0x01; raw_data[idx++] = 0x06; // Flags
    
    // Service Data Header
    raw_data[idx++] = 14;   // Length of Service Data section
    raw_data[idx++] = 0xFF; // Type: Custom Manufacturer Data
    raw_data[idx++] = 0xFF;
    raw_data[idx++] = 0xFF;

    raw_data[idx++] = UUID1;
    raw_data[idx++] = UUID2;
    raw_data[idx++] = CMD_TYPE_ACK;
    
    // Append Status Info to Payload
    raw_data[idx++] = params->my_id;
    raw_data[idx++] = params->cmd_id;
    raw_data[idx++] = params->cmd_type;
    
    // Convert delay to milliseconds and append as 4 bytes (Big-Endian format here)
    uint32_t delay_ms = params->delay_val / 1000;
    raw_data[idx++] = (delay_ms >> 24) & 0xFF;
    raw_data[idx++] = (delay_ms >> 16) & 0xFF;
    raw_data[idx++] = (delay_ms >> 8) & 0xFF;
    raw_data[idx++] = (delay_ms) & 0xFF;

    raw_data[idx++] = params->state;
    
    // Package and send the raw HCI buffer
    uint8_t hci_buf[128];
    uint16_t pkt_len = make_cmd_ble_set_adv_data(hci_buf, idx, raw_data);
    esp_vhci_host_send_packet(hci_buf, pkt_len);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Enable Advertising for 300ms
    hci_cmd_send_ble_adv_enable(1);
    vTaskDelay(pdMS_TO_TICKS(300)); 
    
    // Stop Advertising
    hci_cmd_send_ble_adv_enable(0);
    ESP_LOGD(TAG, ">>> ACK STOPPED. Resuming Scan.");

    // Resume Scanning Operations
    make_cmd_ble_set_scan_enable(scan_buf, 1, 0);
    esp_vhci_host_send_packet(scan_buf, 6);

    free(params);
    vTaskDelete(NULL);
}

// ==========================================
// Part 2: Fast RX Driver & ISR
// ==========================================

// Runs in Interrupt Context (ISR). Parses raw LE Meta Events (0x3E) directly for lowest possible latency.
// Pushes parsed, valid packets to `s_adv_queue` for the main RTOS task to handle.
static void IRAM_ATTR fast_parse_and_trigger(uint8_t* data, uint16_t len) {
    int64_t now_us = esp_timer_get_time(); // Timestamp of exact arrival
    
    // Validate HCI LE Meta Event header
    if(data[0] != 0x04 || data[1] != 0x3E || data[3] != 0x02) return;

    uint8_t num_reports = data[4];
    uint8_t* payload = &data[5];

    // Iterate through all advertisement reports in this event
    for(int i = 0; i < num_reports; i++) {
        uint8_t* mac = &payload[2];
        uint8_t data_len = payload[8];
        uint8_t* adv_data = &payload[9];
        int8_t rssi = payload[9 + data_len];

        uint8_t offset = 0;
        // Parse individual AD Structures within the payload
        while(offset < data_len) {
            uint8_t ad_len = adv_data[offset++];
            if(ad_len == 0) break;
            uint8_t ad_type = adv_data[offset++];
            
            bool is_valid_format = false;
            // Check for Service Data (0x16) and expected length. 
            // Note: Length 20 is specific to raw ESP32 sender.
            if (ad_type == 0x16 && ad_len == 20) {
                is_valid_format = true;
            }
            
            if(is_valid_format) {
                // Verify UUID prefix matches our target system
                if(adv_data[offset] == UUID1 && adv_data[offset + 1] == UUID2) {
                    
                    // Reconstruct 64-bit target mask from Little-Endian bytes
                    uint64_t rcv_mask = 0;
                    for(int k = 0; k < 8; k++) rcv_mask |= ((uint64_t)adv_data[offset + 3 + k] << (k * 8));

                    // Evaluate if this device is targeted by the bitmask
                    bool is_target = false;
                    if (rcv_mask == 0xFFFFFFFFFFFFFFFFULL) { // Global Broadcast
                        is_target = true; 
                    }
                    else{
                        if ((rcv_mask >> s_config.my_player_id) & 1ULL) { // Specific ID Targeting
                            is_target = true;
                        }
                    }
                    
                    if(is_target) {
                        // Extract Command Meta
                        uint8_t rcv_cmd_id = (adv_data[offset + 2] >> 4) & 0x0F;
                        uint8_t rcv_cmd = adv_data[offset + 2] & 0x0F;
                        
                        // Extract Delay (Big-Endian conversion)
                        uint32_t rcv_delay_ms = (adv_data[offset + 11] << 24) | (adv_data[offset + 12] << 16) | (adv_data[offset + 13] << 8) | (adv_data[offset + 14]);
                        
                        uint32_t rcv_prep_ms = 0;
                        uint8_t rcv_data[3] = {0, 0, 0};
                        
                        // Extract command-specific parameters (e.g., RGB for TEST)
                        int spec_idx = offset + 15;
                        if (rcv_cmd == 0x01) { 
                            rcv_prep_ms = (adv_data[spec_idx] << 24) | (adv_data[spec_idx + 1] << 16) | (adv_data[spec_idx + 2] << 8) | adv_data[spec_idx + 3];
                        } else if (rcv_cmd == 0x05) { 
                            rcv_data[0] = adv_data[spec_idx];
                            rcv_data[1] = adv_data[spec_idx + 1];
                            rcv_data[2] = adv_data[spec_idx + 2];
                        } else if (rcv_cmd == 0x06) { 
                            rcv_data[0] = adv_data[spec_idx];
                        }
                        
                        // Populate structure to pass to RTOS Task
                        ble_rx_packet_t pkt;
                        pkt.cmd_id = rcv_cmd_id;
                        pkt.cmd_type = rcv_cmd;
                        pkt.target_mask = rcv_mask;
                        pkt.delay_val = rcv_delay_ms * 1000ULL; // Convert ms to us
                        pkt.prep_time = rcv_prep_ms * 1000ULL;
                        pkt.data[0] = rcv_data[0];
                        pkt.data[1] = rcv_data[1];
                        pkt.data[2] = rcv_data[2];
                        pkt.rssi = rssi;
                        pkt.rx_time_us = now_us;
                        memcpy(pkt.mac, mac, 6);
                        
                        // Non-blocking send to RTOS queue
                        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                        xQueueSendFromISR(s_adv_queue, &pkt, &xHigherPriorityTaskWoken);
                        if(xHigherPriorityTaskWoken) portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // Force context switch if needed
                        
                        return; // Done processing this valid command
                    }
                }
            }
            offset += (ad_len - 1); // Move to next AD Structure
        }
        payload += (10 + data_len + 1); // Move to next report in event
    }
}

// Wrapper to pass HCI data directly into the custom fast parser
static int host_rcv_pkt(uint8_t* data, uint16_t len) {
    fast_parse_and_trigger(data, len);
    return ESP_OK;
}
static void controller_rcv_pkt_ready(void) {}

// HCI host callback registration struct
static esp_vhci_host_callback_t vhci_host_cb = {controller_rcv_pkt_ready, host_rcv_pkt};

// Spawns the RTOS task that triggers a CHECK/ACK response
static void trigger_ack_task(uint8_t my_id, uint8_t cmd_id, uint8_t cmd_type, uint32_t delay_val, uint8_t state) {
    ack_task_params_t *params = (ack_task_params_t *)malloc(sizeof(ack_task_params_t));
    if (params) {
        params->my_id = my_id;
        params->cmd_id = cmd_id;
        params->cmd_type = cmd_type;
        params->delay_val = delay_val;
        params->state = state;
        xTaskCreate(send_ack_task, "ack_task", 4096, params, 5, NULL);
    }
}

// Callback when the preparation LED timer expires (Turns off the LED)
static void IRAM_ATTR led_timer_cb(void* arg) {
    if (Player::getInstance().getState() == 4) { // State 4 = TEST mode
        Player::getInstance().stop();
    }
}

// Primary execution callback. Fired when a command's delay timer reaches 0.
static void IRAM_ATTR timer_timeout_cb(void* arg) {
    action_slot_t* slot = (action_slot_t*)arg;
    uint8_t cmd = slot->ctx.target_cmd;
    uint8_t test_data[3];
    memcpy(test_data, (const uint8_t*)slot->ctx.data, 3);
    
    int slot_id = slot - s_slots;
    if (slot_id >= 0 && slot_id < MAX_CONCURRENT_ACTIONS) {
        s_visual_ack_done[slot_id] = false; // Reset visual ACK lock
    }
    ESP_LOGD(TAG,">> [ACTION TRIGGERED] CMD: 0x%02X\n", cmd);
    
    // Dispatch execution logic to the Player instance
    switch(cmd) {
        case LPS_CMD_PLAY: 
            Player::getInstance().play();
            break;
        case LPS_CMD_PAUSE: 
            Player::getInstance().pause();
            break;
        case LPS_CMD_STOP: 
            Player::getInstance().stop();
            break;
        case LPS_CMD_RELEASE: 
            Player::getInstance().release();
            break;
        case LPS_CMD_TEST: 
            if (test_data[0] == 0 && test_data[1] == 0 && test_data[2] == 0) {
                Player::getInstance().test();
            } else {
                Player::getInstance().test(test_data[0], test_data[1], test_data[2]);
            }
            break;
        case LPS_CMD_CANCEL: // Aborts a previously scheduled command
            {
                uint8_t target_id = test_data[0];
                if (target_id < MAX_CONCURRENT_ACTIONS) {
                    esp_timer_stop(s_slots[target_id].timer_handle); // Halt the timer
                    s_visual_ack_done[target_id] = false;
                    
                    // Turn off indicator LED only if canceling a PLAY command
                    if (s_slots[target_id].ctx.target_cmd == 0x01) {
                        esp_timer_stop(s_led_timer);
                        Player::getInstance().stop();
                    }
                    ESP_LOGD(TAG, "CMD 0x%02X Canceled! CMD_ID = %d", s_slots[target_id].ctx.target_cmd, target_id);
                }
            }
            break;
        case LPS_CMD_CHECK: // Calculates remaining delay and reports current state back to host
        {
            int8_t state = Player::getInstance().getState();
            int64_t now = esp_timer_get_time();
            int64_t target_time = s_last_locked_cmd.lock_timestamp + s_last_locked_cmd.original_delay;
            int32_t remaining_us = (int32_t)(target_time - now);
            if (remaining_us < 0) remaining_us = 0;
            
            trigger_ack_task(s_config.my_player_id, 
                             s_last_locked_cmd.cmd_id, 
                             s_last_locked_cmd.cmd_type, 
                             (uint32_t)remaining_us,
                             state);
            break;
        }
        case LPS_CMD_UPLOAD: 
        case LPS_CMD_RESET: 
            // Send system-level/control commands to the main application's queue
            if (sys_cmd_queue != NULL) {
                sys_cmd_t msg = (sys_cmd_t)cmd;
                BaseType_t ret = xQueueSend(sys_cmd_queue, &msg, 0);
                if (ret == pdPASS) {
                    ESP_LOGD(TAG, "Sent System CMD (0x%02X) to Task Queue", cmd);
                } else {
                    ESP_LOGE(TAG, "Failed to send CMD to Queue! (Queue Full)");
                }
            }
            break;
        default:
            break;
    }
}

// RTOS Task: De-queues packets from ISR.
// Contains Jitter Compensation Logic: Averages absolute execution times (rx_time_us + delay_val) 
// of duplicate packets received within `sync_window_us` to calculate a highly precise final delay.
static void sync_process_task(void* arg) {
    ble_rx_packet_t pkt;
    uint8_t current_cmd_id = 0;
    uint8_t current_cmd = 0;
    uint64_t current_mask = 0;
    uint8_t current_data[3] = {0, 0, 0};
    uint32_t current_prep_time = 0;
    int64_t sum_rssi = 0;
    int64_t sum_target = 0;
    int count = 0;
    bool collecting = false;
    bool window_expired = false;
    int64_t window_start_time = 0;
    uint8_t current_mac[6] = {0};
    ESP_LOGI(TAG, "Sync Task Running...");

    while(s_is_running) {
        // Block until a packet is parsed by the ISR
        if(xQueueReceive(s_adv_queue, &pkt, pdMS_TO_TICKS(10)) == pdTRUE) {
            int64_t now = esp_timer_get_time();
            if(!collecting) {
                // First packet of a potential burst; initialize the window
                collecting = true;
                current_cmd_id = pkt.cmd_id;
                current_cmd = pkt.cmd_type;
                current_mask = pkt.target_mask;
                current_data[0] = pkt.data[0];
                current_data[1] = pkt.data[1];
                current_data[2] = pkt.data[2];
                current_prep_time = pkt.prep_time;    
                sum_rssi = pkt.rssi;           
                sum_target = (pkt.rx_time_us + pkt.delay_val); // Calculate absolute target timestamp
                count = 1;
                window_start_time = now;
                window_expired = false;
                memcpy(current_mac, pkt.mac, 6);
            }
            else {
                if (pkt.cmd_id == current_cmd_id) {
                    // Accumulate data for averaging if inside the sync window
                    if(now < (window_start_time + s_config.sync_window_us)) {
                        sum_target += (pkt.rx_time_us + pkt.delay_val);
                        sum_rssi += pkt.rssi;
                        count++;
                    }
                } else {
                    // Packet belongs to a different Command ID. Close the current window early.
                    window_expired = true;
                    if(count > 0) {
                        int64_t final_target = sum_target / count; // Jitter-smoothed target timestamp
                        int64_t wait_us = final_target - now;      // Final corrected relative delay
                        int8_t avg_rssi = (int8_t)(sum_rssi / count);
                        
                        if(wait_us > 100000) { // Reject expired/late packets (< 100ms)
                            action_slot_t* target_slot = &s_slots[current_cmd_id];
                            target_slot->ctx.target_cmd = current_cmd;
                            target_slot->ctx.target_mask = current_mask;
                            memcpy((void*)target_slot->ctx.data, current_data, 3);
                            
                            // Load the hardware timer
                            esp_timer_stop(target_slot->timer_handle);
                            esp_timer_start_once(target_slot->timer_handle, wait_us);
                            
                            if (!s_visual_ack_done[current_cmd_id]) {
                                ESP_LOGI(TAG, "LOCKED -> MAC:%02X:%02X:%02X:%02X:%02X:%02X, ID:%d, CMD:0x%02X, AvgRSSI:%d dBm (Cnt:%d), Delay:%lld ms", 
                                         current_mac[5], current_mac[4], current_mac[3], current_mac[2], current_mac[1], current_mac[0],
                                         current_cmd_id, current_cmd, avg_rssi, count, wait_us/1000);
                                
                                // Provide immediate visual feedback (e.g., Red flash for PLAY)
                                if (current_cmd == 0x01 && current_prep_time > 0) {
                                    Player::getInstance().test(255, 0, 0); 
                                    esp_timer_stop(s_led_timer); 
                                    esp_timer_start_once(s_led_timer, current_prep_time);
                                }
                                s_visual_ack_done[current_cmd_id] = true;
                            }
                            
                            // Cache data so the CHECK command knows what to report
                            if (current_cmd != 0x07) {
                                s_last_locked_cmd.cmd_id = current_cmd_id;
                                s_last_locked_cmd.cmd_type = current_cmd;
                                s_last_locked_cmd.original_delay = (uint32_t)wait_us;
                                s_last_locked_cmd.lock_timestamp = esp_timer_get_time();
                            }
                        }
                    }
                    // Re-initialize tracking for the new distinct command we just encountered
                    collecting = true;
                    current_cmd_id = pkt.cmd_id;
                    current_cmd = pkt.cmd_type;
                    current_mask = pkt.target_mask;
                    current_data[0] = pkt.data[0];
                    current_data[1] = pkt.data[1];
                    current_data[2] = pkt.data[2];
                    current_prep_time = pkt.prep_time;
                    sum_rssi = pkt.rssi;
                    window_start_time = now;
                    window_expired = false;
                    memcpy(current_mac, pkt.mac, 6);
                    sum_target = (pkt.rx_time_us + pkt.delay_val);
                    count = 1;
                }
            }
        }
        
        // Timeout check: Process the window naturally if the time has elapsed without interruption
        if(collecting && !window_expired) {
            int64_t now = esp_timer_get_time();
            if(now >= (window_start_time + s_config.sync_window_us)) {
                window_expired = true;
                if(count > 0) {
                     int64_t final_target = sum_target / count;
                     int64_t wait_us = final_target - now;
                     int8_t avg_rssi = (int8_t)(sum_rssi / count);
                     
                     if(wait_us > 100000) { 
                         action_slot_t* target_slot = &s_slots[current_cmd_id];
                         if(target_slot != NULL) {
                             target_slot->ctx.target_cmd = current_cmd;
                             target_slot->ctx.target_mask = current_mask;
                             memcpy((void*)target_slot->ctx.data, current_data, 3);
                             
                             esp_timer_stop(target_slot->timer_handle);
                             esp_timer_start_once(target_slot->timer_handle, wait_us);
                             
                             if (!s_visual_ack_done[current_cmd_id]) {
                                 ESP_LOGI(TAG, "LOCKED -> MAC:%02X:%02X:%02X:%02X:%02X:%02X, ID:%d, CMD:0x%02X, AvgRSSI:%d dBm (Cnt:%d), Delay:%lld ms", 
                                          current_mac[5], current_mac[4], current_mac[3], current_mac[2], current_mac[1], current_mac[0],
                                          current_cmd_id, current_cmd, avg_rssi, count, wait_us/1000);
                                          
                                 if (current_cmd == 0x01 && current_prep_time > 0) {
                                     Player::getInstance().test(255, 0, 0);
                                     esp_timer_stop(s_led_timer);
                                     esp_timer_start_once(s_led_timer, current_prep_time);
                                 }
                                 s_visual_ack_done[current_cmd_id] = true;
                             }
                         }
                     }
                }
                collecting = false; // Reset state machine
            }
        }
    }
    vTaskDelete(NULL);
}

// ==========================================
// Part 3: Public API implementation
// Handles lifecycle management, queue initialization, and HCI controller setup.
// ==========================================

esp_err_t bt_receiver_init(const bt_receiver_config_t* config) {
    if(!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    
    // Create the FreeRTOS Queue linking ISR to Task
    s_adv_queue = xQueueCreate(s_config.queue_size, sizeof(ble_rx_packet_t));
    if(!s_adv_queue) return ESP_ERR_NO_MEM;
    
    // Initialize Hardware Timers for Action Slots
    esp_timer_create_args_t timer_args = {
        .callback = &timer_timeout_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK, // Execute callback in a standard task context
        .name = "bt_slot_tmr",
        .skip_unhandled_events = false
    };
    for(int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        timer_args.arg = (void*)&s_slots[i];
        esp_timer_create(&timer_args, &s_slots[i].timer_handle);
    }
    
    // Initialize LED Preparation Timer
    esp_timer_create_args_t led_timer_args = {
        .callback = &led_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "prep_led_tmr",
        .skip_unhandled_events = false
    };
    esp_timer_create(&led_timer_args, &s_led_timer);

    // Initialize BT Controller at the HCI Layer
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_vhci_host_register_callback(&vhci_host_cb); // Map custom parser to HCI stream
    return ESP_OK;
}

esp_err_t bt_receiver_start(void) {
    if(s_is_running) return ESP_OK;
    
    // HCI Configuration sequence
    uint16_t sz = make_cmd_reset(hci_cmd_buf);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Set event mask to receive LE Meta Events (0x3E)
    uint8_t mask[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
    sz = make_cmd_set_evt_mask(hci_cmd_buf, mask);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Setup high duty cycle scanning for maximum packet capture
    sz = make_cmd_ble_set_scan_params(hci_cmd_buf, 0x00, 0x0F, 0x0F, 0x00, 0x00);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Enable Scanning
    sz = make_cmd_ble_set_scan_enable(hci_cmd_buf, 1, 0);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    
    s_is_running = true;
    xTaskCreatePinnedToCore(sync_process_task, "bt_rx_task", 4096, NULL, 5, &s_task_handle, 1);
    ESP_LOGI(TAG, "Receiver Started");
    return ESP_OK;
}

esp_err_t bt_receiver_stop(void) {
    s_is_running = false;
    uint16_t sz = make_cmd_ble_set_scan_enable(hci_cmd_buf, 0, 0); // Disable Scanning
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    
    // Stop all active action timers
    for(int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if(s_slots[i].timer_handle) esp_timer_stop(s_slots[i].timer_handle);
    }
    if (s_led_timer) esp_timer_stop(s_led_timer);
    return ESP_OK;
}

esp_err_t bt_receiver_deinit(void) {
    // 1. Stop scanning and all timers
    if (s_is_running) {
        bt_receiver_stop();
    }

    // 2. Unregister HCI Callback to stop receiving new packets
    esp_vhci_host_register_callback(NULL);

    // 3. Delete FreeRTOS Task (ensure nobody processes the queue)
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    // 4. Delete FreeRTOS Queue
    if (s_adv_queue) {
        vQueueDelete(s_adv_queue);
        s_adv_queue = NULL;
    }
    
    // 5. Delete ESP Timers completely
    for(int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if(s_slots[i].timer_handle) {
            esp_timer_delete(s_slots[i].timer_handle);
            s_slots[i].timer_handle = NULL;
        }
    }

    // 6. Disable and De-initialize BT Controller
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    
    // 7. (Optional) Release BT memory if needed
    esp_bt_mem_release(ESP_BT_MODE_BTDM);

    ESP_LOGI(TAG, "Receiver De-initialized");
    return ESP_OK;
}