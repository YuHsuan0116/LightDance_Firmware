# BlueTooth Receiver Component (ESP32)

This is a Bluetooth Low Energy (BLE) receiver component designed for the ESP32. It bypasses standard Bluedroid or NimBLE stacks, communicating directly with the controller via the ESP32's **HCI (Host Controller Interface)**. This achieves ultra-low latency, precise parsing of advertising packets, and includes a **Status Reporting mechanism**.

Its primary function is for multi-device synchronization systems. The receiver scans for specific BLE advertising packets, parses commands, synchronizes triggers using a windowing algorithm, and provides real-time status feedback upon request.

## ✨ Features

* **Low Latency Parsing**: Performs rapid parsing of advertising packets (`fast_parse_and_trigger`) directly within the VHCI callback function (ISR context).
* **Precise Synchronization**: Includes synchronization window logic (`sync_process_task`) to collect multiple advertising packets and calculate the average trigger time, eliminating the variance caused by wireless transmission latency.
* **Status Feedback**: Responds to CHECK commands by reporting the current state and the dynamic remaining time until the next action.
* **Target Filtering**: Supports filtering by `Manufacturer ID` and `Target Mask` (bitmask), allowing commands to be targeted at a single device or a group of devices.

## 🧠 System Internal Workflow

This section explains how the receiver processes a signal from the air to the actual action execution.

### 1. Packet Reception (ISR Context)

* **HCI Callback**: When the Bluetooth Controller receives an advertising packet, it triggers `vhci_host_cb`.
* **Fast Parsing**: The function `fast_parse_and_trigger` filters the packet by Manufacturer ID and Target Mask.
* **Queueing**: If valid, the raw packet data (including the `delay` and `rx_timestamp`) is pushed into a FreeRTOS Queue (`s_adv_queue`).

### 2. Synchronization (Task Context)

The `sync_process_task` continuously reads from the queue:

* **Window Start**: When a new Command ID is seen, a "Sync Window" (e.g., 500ms) starts.
* **Averaging**: The task collects multiple packets for the same Command ID. It calculates the **Absolute Target Time** for each packet: `Target = RX_Timestamp + Delay_Value`.
* **Jitter Reduction**: It averages these Target Times to calculate the final firing timestamp, effectively canceling out transmission jitter.

### 3. Scheduling

* **Timer Set**: Once the window closes (or a sufficient sample count is reached), `esp_timer_start_once` is called with the calculated remaining duration.
* **Locking**: The command is recorded as `s_last_locked_cmd` for status reporting purposes.

### 4. Execution & Feedback

* **Action Trigger**: When the timer expires, `timer_timeout_cb` executes the corresponding Player action (Play, Pause, etc.).
* **Check/ACK Handling**: If the command is `CHECK` (0x07):
1. The system calculates the **Remaining Time** of the *last locked command*.
2. It temporarily **stops scanning**.
3. It broadcasts an **ACK Packet** (Advertising) containing the state and remaining time.
4. It **resumes scanning** to listen for new commands.



## 📂 File Structure

```text
Messenger/
├── CMakeLists.txt          # ESP-IDF component build script
├── include/
│   └── bt_receiver.h       # External API interface and structure definitions
└── src/
    └── bt_receiver.cpp     # Core implementation (HCI commands, ISR parsing, sync logic, ACK task)
```

## 🛠 Dependencies

* **ESP-IDF** (Must include `bt`, `nvs_flash`, `esp_timer`, and other standard components).
* **Player Module** (`player.hpp`): This component relies on an external `Player` singleton class to execute the actual actions (e.g., `play()`, `pause()`).

## 🚀 Usage

### 1. Configuration and Initialization

Include the header file and initialize the component in your `main` code:

```c
#include "bt_receiver.h"

void app_main(void) {
    // Define receiver configuration
    bt_receiver_config_t config = {
        .feedback_gpio_num = -1,     // Debug GPIO (Set to -1 if unused)
        .manufacturer_id = 0xFFFF,   // Must match the Sender's Manufacturer ID
        .my_player_id = 0,           // This device's ID (Used for Target Mask check)
        .sync_window_us = 500000,    // Sync window size (e.g., 500ms)
        .queue_size = 20             // Depth of the command queue
    };

    // Initialize the receiver
    ESP_ERROR_CHECK(bt_receiver_init(&config));

    // Start receiving and scanning
    ESP_ERROR_CHECK(bt_receiver_start());
}
```

### 2. Stop Receiving

If you need to stop scanning and timers:

```c
bt_receiver_stop();
```

### 3. De-initialization (Wi-Fi Coexistence)

To completely release Bluetooth resources (e.g., before connecting to Wi-Fi to save memory or avoid hardware conflicts), use `deinit`. This function disables the controller and frees all allocated tasks and queues.

```c
// 1. Fully stop and release BLE resources
bt_receiver_deinit();

// ... Perform Wi-Fi operations (e.g., OTA update, File Download) ...

// 2. Re-initialize when done with Wi-Fi
bt_receiver_init(&config);
bt_receiver_start();
```

## 📡 Protocol Definition

### 1. Received Packet (From Sender)

The receiver parses the `AD Type = 0xFF` (Manufacturer Specific Data) section within the BLE advertising packet.

| Offset | Length | Description | Notes |
| --- | --- | --- | --- |
| 0 | 3 | **Manufacturer ID** | Little Endian, must match Config |
| 3 | 1 | **CMD Info** | High 4-bit: `CMD_ID` (Identifier), Low 4-bit: `CMD_TYPE` (Action Type) |
| 4 | 8 | **Target Mask** | 64-bit Mask, corresponds to `my_player_id` |
| 12 | 4 | **Delay** | Big Endian, execution delay (us) |
| 16 | 4 | **Prep Time** | Big Endian, preparation time |
| 20 | 3 | **Data** | Extra parameters (e.g., test data) |

### 2. Transmitted ACK Packet (To Sender)

When a `CHECK` command is received, the receiver broadcasts an ACK packet.

| Offset | Length | Description | Notes |
| --- | --- | --- | --- |
| 0 | 2 | **Manufacturer ID** | `0xFFFF` |
| 2 | 1 | **Packet Type** | **`0x07`** (CMD_TYPE_ACK) |
| 3 | 1 | **My ID** | The `my_player_id` of this device |
| 4 | 1 | **CMD ID** | ID of the command being acknowledged |
| 5 | 1 | **CMD Type** | Action type (e.g., PLAY, PAUSE) |
| 6 | 4 | **Delay** | Big Endian, the locked delay value |
| 10 | 1 | **State** | Current Player State (e.g., READY, PLAYING) |

### Supported Command Types (CMD_TYPE)

These values are defined in the `timer_timeout_cb` function within `bt_receiver.cpp`:

| Type Code | Action | Data Payload Usage |
| --- | --- | --- |
| `0x01` | **Play** | None |
| `0x02` | **Pause** | None |
| `0x03` | **Stop** | None |
| `0x04` | **Release** | None |
| `0x05` | **Test** | Uses `Data[0-2]` for test parameters |
| `0x06` | **Cancel** | `Data[0]` contains the target `CMD_ID` to cancel |
| `0x07` | **Check** | Triggers immediate status report (ACK) |