# Pattern Table v1.1 Guide 

This document is written for the player team. It explains what the frame system provides, how to use it correctly, and what assumptions the system makes. 

## 1. Finite State Machine

define variable
```
static bool inited; 
static bool running; 
static bool eof_reached;
```

```  
┌──────────┐ 
│ UNINIT   │ ◄────────────────────────────────────────┐ 
│ inited=0 │                                          │
│ running=0│                                          │
│ eof=0    │                                          │
└────┬─────┘                                          │
     │                                                │
     │   frame_system_init()                          │
     │                                                │
     ▼                                                │
┌──────────┐      read_frame()                        │
│ INITED   │ ──────────┐                              │
│ inited=1 │           ▼                              │
│ running=1│      ┌──────────┐                        │ 
│ eof=0    │      │  ACTIVE  │                        │
│ cmd=NONE │      │ inited=1 │                        │ 
└──────────┘      │ running=1│                        │
       ▲          │ eof=0    │                        │ 
       │          └────┬─────┘                        │ 
       │               │                              │ 
       │               │                              │ 
       │     read_frame() (EOF)                       │ 
       │               │                              │ 
       │               ▼                              │
       │            ┌──────────┐                      │
       │            │ EOF      │                      │
       │            │ REACHED  │                      │
       │            │ inited=1 │                      │ 
       │            │ running=1│                      │ 
       │            │ eof=1    │                      │
       │            └────┬─────┘                      │
       │                 │                            │
       │  frame_reset()  │   frame_system_deinit()    │
       └─────────────────┘────────────────────────────│ 
                                                      │
                                                      │
                                                      │
┌──────────┐                                          │
│ STOPPED  │                                          │
│ inited=1 │  frame_system_deinit()                   │
│ running=0│ ─────────────────────────────────────────┘
│ eof=0/1  │
└──────────┘
```

## 2. API

### frame_system_init(const char* control_path, const char* frame_path)

在 UNINIT 狀態下：

成功 → 進入 INITED 狀態

失敗 → 停留在 UNINIT 狀態

- inited = 0 → 1

- running = 0 → 1

- eof = 0 → 0


在 INITED / ACTIVE / EOF_REACHED / STOPPED 狀態下：

拒絕操作，return ESP_ERR_INVALID_STATE

---

### read_frame(table_frame_t* playerbuffer)

在 INITED 狀態下：

成功 → 進入 ACTIVE 狀態

錯誤 → 進入 STOPPED 狀態

在 ACTIVE 狀態下：

情況1：讀取成功

停留在 ACTIVE 狀態

變數無變化

情況2：讀取到 EOF

eof = 0 → 1

情況3：讀取錯誤

進入 STOPPED 狀態

變數：running = 1 → 0

在 INITED / ACTIVE / EOF_REACHED / STOPPED 狀態下：

拒絕操作，return ESP_ERR_INVALID_STATE

---

### frame_reset(void)

在 INITED / ACTIVE / EOF_REACHED 狀態下：

重置到第0幀，SD任務處理後進入 INITED 狀態

eof 1/0 → 0

在 UNINIT / STOPPED 狀態下：

返回 ESP_ERR_INVALID_STATE

---

### frame_system_deinit(void)

在所有狀態下：

直接進入 UNINIT

inited = 1 → 0

---

### get_sd_card_id()

讀取SD卡label回傳ID
有SD卡 → 回傳1~31
無SD卡 → 回傳 0