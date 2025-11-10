# Diagrammes Détaillés des Interactions UART-CAN

## Diagramme 1: Pipeline Complet UART → CAN

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         FLUX DE DONNÉES COMPLET                          │
└──────────────────────────────────────────────────────────────────────────┘

1. RÉCEPTION UART
   ┌─────────────────────────┐
   │  TinyBMS Hardware       │
   │  (Batterie BMS)         │
   │  Envoie registres       │
   │  Format propriétaire    │
   └────────────┬────────────┘
                │ Trame UART reçue
                ↓
   ┌─────────────────────────────────────────┐
   │ UART BMS Module                         │
   │ (uart_bms_protocol.c)                   │
   │                                         │
   │ ├─ CRC validation                       │
   │ ├─ Header check                         │
   │ ├─ Extract 59 registers                 │
   │ └─ Apply scaling factors                │
   │                                         │
   │ OUTPUT: uart_bms_live_data_t           │
   │ (500 bytes, 59 registers decoded)      │
   └────────────┬────────────────────────────┘
                │ Callback synchrone
                ↓
   ┌─────────────────────────────────────────┐
   │ CAN Publisher Module                    │
   │ (can_publisher.c)                       │
   │                                         │
   │ ├─ CVL prepare (state machine)          │
   │ ├─ For each CAN channel:                │
   │ │  ├─ Call encoder (fill_fn)            │
   │ │  └─ Store in frame buffer             │
   │ └─ If immediate mode: dispatch now      │
   │                                         │
   │ STORAGE: s_frame_buffer (8 slots)      │
   │ EVENTS: APP_EVENT_ID_CAN_FRAME_READY   │
   └────────────┬────────────────────────────┘
                │ CAN frames prêts
                ├─────────────────────┬──────────────────┬─────────────────┐
                │                     │                  │                 │
                ↓                     ↓                  ↓                 ↓
      ┌─────────────────┐   ┌──────────────────┐  ┌──────────────┐  ┌─────────────────┐
      │ CAN Victron     │   │ Event Bus        │  │ Web Server   │  │ MQTT Gateway    │
      │ Driver          │   │ Subscribers      │  │ Listener     │  │ Listener        │
      │ (TWAI hardware) │   │ (queue 16)       │  │              │  │                 │
      │                 │   │                  │  │ UI           │  │ Cloud/Analytics │
      │ ├─ TX frame     │   │ ├─ Status LED    │  │ Websocket    │  │ MQTT Publish    │
      │ ├─ RX keepalive │   │ ├─ Monitoring    │  │ broadcast    │  │                 │
      │ └─ Bus status   │   │ └─ History log   │  │              │  │                 │
      └────────┬────────┘   └──────────────────┘  └──────────────┘  └─────────────────┘
               │
               ↓
      ┌──────────────────┐
      │ Victron Devices  │
      │ (GX device,      │
      │  Inverters,      │
      │  MPPT,           │
      │  Relay)          │
      └──────────────────┘
```

---

## Diagramme 2: Architecture Synchronisation (Mutexes & Queues)

```
┌──────────────────────────────────────────────────────────────────┐
│                  COUCHES DE SYNCHRONISATION                      │
└──────────────────────────────────────────────────────────────────┘

NIVEAU 1: EVENT BUS (Cœur Asynchrone)
├─ SemaphoreHandle_t s_bus_lock
│  └─ Protège: s_subscribers linked list
│     └─ Timeout: portMAX_DELAY (jamais timeout)
│        └─ Scope: event_bus_publish() lock critical
│
├─ Pour chaque abonné:
│  └─ QueueHandle_t subscriber->queue
│     ├─ Taille: configurable (default 16)
│     └─ Send timeout: 0 (non-blocking)
│        └─ Si queue pleine → EVENT DROP

NIVEAU 2: CAN PUBLISHER (Synchronisation Dual-Mutex)
├─ SemaphoreHandle_t s_buffer_mutex
│  ├─ Protège: can_publisher_buffer_t s_frame_buffer
│  │  └─ 8 slots circulaires
│  │     └─ slot_valid[] flags
│  │        └─ deadlines[] tracking
│  │
│  └─ Accès dans:
│     ├─ can_publisher_store_frame() - write
│     │  └─ Timeout: 20ms → FAIL si occupé
│     │
│     └─ can_publisher_publish_buffer() - read
│        └─ Timeout: 20ms → SKIP frame si timeout
│
├─ SemaphoreHandle_t s_event_mutex
│  ├─ Protège: can_publisher_frame_t s_event_frames[8]
│  │  └─ Circular index s_event_frame_index
│  │
│  └─ Accès: can_publisher_publish_event()
│     ├─ Store frame in next slot
│     ├─ Release mutex
│     └─ Publish to event bus (separate call)
│
└─ Relationship:
   ├─ Independent mutexes (pas de nesting)
   ├─ Same timeout (20ms)
   └─ Different protected resources

NIVEAU 3: CAN VICTRON (Driver TWAI)
├─ SemaphoreHandle_t s_twai_mutex
│  ├─ Protège: TWAI hardware registers
│  │  └─ ESP32 CAN controller
│  │
│  └─ Accès: can_victron_publish_frame()
│     └─ Timeout: 20ms
│
├─ SemaphoreHandle_t s_driver_state_mutex
│  ├─ Protège: bool s_driver_started
│  │
│  └─ Accès: can_victron_is_driver_started()
│     └─ Timeout: 20ms
│
├─ FreeRTOS Queues (TWAI):
│  ├─ RX Queue (16 messages)
│  │  └─ Receive timeout: 10ms per frame
│  │
│  └─ TX Queue (16 messages)
│     └─ Managed by TWAI driver
│
└─ Task: can_victron_task
   └─ Priority: tskIDLE_PRIORITY + 6 (HIGH)
      └─ Cycle: 50ms (vTaskDelay)
```

---

## Diagramme 3: Sequence Diagram - UART Event → CAN Frame

```
Time →

UART ISR       UART BMS Module    CAN Publisher    Event Bus    CAN Victron
  │                │                  │               │            │
  │ Receive Data   │                  │               │            │
  ├─────────────→  │                  │               │            │
  │                │                  │               │            │
  │            Decode Frame            │               │            │
  │            CRC Check       ✓       │               │            │
  │            Parse Regs      ✓       │               │            │
  │                │                  │               │            │
  │         Notify Listeners           │               │            │
  │         (Synchrone)        ────────────→           │            │
  │                │                  │               │            │
  │                │            Can Publisher On BMS Update         │
  │                │              ├─ cvl_prepare()   │            │
  │                │              ├─ For each chan   │            │
  │                │              │  ├─ Encoder      │            │
  │                │              │  └─ store_frame  │            │
  │                │              │                  │            │
  │                │         xSemaphoreTake(buffer_mutex, 20ms)   │
  │                │              │ ✓ Acquired      │            │
  │                │         Store in s_frame_buffer │            │
  │                │         xSemaphoreGive(mutex)   │            │
  │                │              │ ✓ Released      │            │
  │                │              │                  │            │
  │                │         [If immediate mode]     │            │
  │                │         dispatch_frame()        │            │
  │                │         (s_frame_publisher)     │            │
  │                │              │                  │            │
  │                │              │──────────────────│            │
  │                │         publish_event()  ✓      │            │
  │                │              │                  │            │
  │                │         [Store in s_event_frames│            │
  │                │              │                  │            │
  │                │         [Publish CAN_FRAME_READY to bus]    │
  │                │              │                  │            │
  │                │              │            [Queued to subs]   │
  │                │              │            [Web, MQTT, etc]   │
  │                │              │                  │            │
  │                │         [If periodic mode]      │            │
  │                │         can_publisher_task      │            │
  │                │              │                  │            │
  │                │         [Wait until deadline]   │            │
  │                │         [Read s_frame_buffer]   │            │
  │                │              │                  │            │
  │                │              │    ────────────────────────→ │
  │                │              │    Publish via TWAI TX       │
  │                │              │                  │     Sent to│
  │                │              │                  │     Bus    │
  │                │              │                  │            │
  
Latency Analysis:
- UART→Decode: 1-2ms
- Decode→Callback: 0.5-1ms
- Callback→Store: 20ms (mutex timeout) + <5ms (store operation)
- Store→Dispatch: <5ms
- Dispatch→CAN bus: ~1ms (TWAI queue)
- Total: ~28-35ms (immediate mode)
      or ~80-100ms (periodic mode, 50ms task delay + processing)
```

---

## Diagramme 4: Modèle de Contentious Locks & Timeouts

```
┌────────────────────────────────────────────────────────────────┐
│              SCENARIO: CAN PUBLISHER LOCK CONTENTION           │
└────────────────────────────────────────────────────────────────┘

Timeline (ms):
0       10       20       30       40       50

THREAD 1 (CAN Publisher callback from UART)
│
├─ t=0: Try acquire s_buffer_mutex
├─       ✓ Lock acquired
├─ t=5: Storing frame...
├─ t=7: Store complete
├─ t=7: Release mutex
└─       ✓ Success

THREAD 2 (CAN Publisher task)
│
├─ t=0:  Try acquire s_buffer_mutex
├─       ⏳ WAIT (Thread 1 holds lock)
├─ t=7:  ✓ Lock acquired (Thread 1 released)
├─ t=17: Read frame from buffer
├─ t=17: Release mutex
└─       ✓ Success


┌────────────────────────────────────────────────────────────────┐
│      PROBLEMATIC SCENARIO: TIMEOUT (SLOW TWAI)                 │
└────────────────────────────────────────────────────────────────┘

Timeline (ms):
0       10       20       30       40       50

TWAI Driver (holding implicit lock via hardware access)
│
├─ t=0:  TWAI TX busy (congestion)
├─ t=15: Still transmitting
└─ t=30: Finally done


CAN Publisher (s_buffer_mutex timeout = 20ms)
│
├─ t=0:  Try acquire s_buffer_mutex
├─       ✓ Lock acquired
├─ t=5:  Call can_victron_publish_frame()
├─       │
├─       └─→ s_twai_mutex acquired
├─       │
├─       └─→ TWAI TX slow (15ms)
├─ t=20: Timeout expired!
├─       ❌ Release with incomplete operation
└─       ❌ Frame may be corrupted


Alternative Thread (UART callback)
│
├─ t=10: Try acquire s_buffer_mutex
├─       ⏳ WAIT (CAN Publisher holds lock)
├─ t=20: Still waiting (Timeout - FAILED)
├─       ❌ Frame dropped
├─       ❌ Log: "Timed out acquiring CAN publisher buffer"
└─       ❌ No retry mechanism


┌────────────────────────────────────────────────────────────────┐
│           FIX: INCREASE TIMEOUT (Recommended)                  │
└────────────────────────────────────────────────────────────────┘

Before:
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS 20U

After:
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS 50U  // More forgiving

Timeline:
0       10       20       30       40       50

UART Callback:
│
├─ t=10: Try acquire s_buffer_mutex
├─       ⏳ WAIT (CAN Publisher holds lock)
├─ t=25: Still waiting (CAN Publisher done by now)
├─ t=30: ✓ Lock acquired
├─ t=35: Store complete
└─       ✓ Success (didn't timeout)
```

---

## Diagramme 5: CVL State Machine Race Condition

```
┌────────────────────────────────────────────────────────────────┐
│           RACE CONDITION: CVL STATE (CURRENT BUG)              │
└────────────────────────────────────────────────────────────────┘

Shared State:
  static cvl_state_t s_cvl_state = {
    charging_current_a,
    discharge_current_a,
    aux_port_relay,
    dcl_limit,
    ...
  };

Timeline:

UART BMS Callback Thread
│
├─ t=0:  can_publisher_cvl_prepare(bms_data1)
├─       │
├─       ├─ Read current SOC/SOH
├─       ├─ Compute new CVL limits
├─       └─ Write to s_cvl_state (UNPROTECTED)
├─           ├─ s_cvl_state.charging_current = 80A
├─           ├─ s_cvl_state.dcl_limit = 100A
├─           └─ s_cvl_state.aux_relay = ON
│
│
CAN Publisher Task Thread (runs concurrently)
│
├─ t=5:  can_publisher_publish_buffer()
├─       │
├─       └─ For each channel, read s_cvl_state
│           ├─ Read charging_current (PARTIALLY WRITTEN)
│           ├─ Read dcl_limit (INCONSISTENT)
│           └─ Encode frame with STALE/MIXED data
│
│           Frame sent to Victron with WRONG values:
│           ├─ Charge limit = 80A (just written)
│           ├─ Discharge limit = 120A (old value!)
│           └─ Relay = ON but cutoff = OFF (inconsistent)
│
│           Victron devices receive contradictory commands
│           Inverter behavior undefined (can be dangerous)


RACE CONDITION WINDOW:
  ┌──────────────────────────────────────┐
  │ UART updates s_cvl_state             │
  │ ├─ write field 1 ✓                   │
  │ ├─ write field 2 (CAN task reads!) ❌ │
  │ └─ write field 3 ✓                   │
  │                                      │
  │ CAN task reads mixture of old/new    │
  └──────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│              MITIGATION: ADD MUTEX PROTECTION                  │
└────────────────────────────────────────────────────────────────┘

static SemaphoreHandle_t s_cvl_mutex = NULL;

void can_publisher_cvl_prepare(const uart_bms_live_data_t *data) {
    xSemaphoreTake(s_cvl_mutex, pdMS_TO_TICKS(10));  // Lock
    
    // Critical section
    s_cvl_state.charging_current = ...;
    s_cvl_state.dcl_limit = ...;
    s_cvl_state.aux_relay = ...;
    // All writes are atomic from CAN task perspective
    
    xSemaphoreGive(s_cvl_mutex);  // Unlock
}

static void fill_cvl_frame(can_publisher_frame_t *frame) {
    xSemaphoreTake(s_cvl_mutex, pdMS_TO_TICKS(10));  // Lock
    
    // Critical section
    memcpy(&frame->data, &s_cvl_state, sizeof(cvl_state_t));
    // Reads consistent snapshot
    
    xSemaphoreGive(s_cvl_mutex);  // Unlock
}
```

---

## Diagramme 6: Event Drop Mechanism

```
┌────────────────────────────────────────────────────────────────┐
│        EVENT DROP: QUEUE FULL SCENARIO                         │
└────────────────────────────────────────────────────────────────┘

Default Queue Size per Subscriber: 16

Scenario: Web Server is SLOW (processing every 500ms)

Timeline:

t=0ms:  CAN Publisher publish CAN_FRAME_READY
        │
        ├─ Event Bus acquire s_bus_lock
        ├─ Iterate subscribers:
        │  ├─ [Web Server subscriber]
        │  │   └─ xQueueSend(queue, event, 0)  ← Non-blocking!
        │  │       ├─ Queue size: 1/16 ✓
        │  │       └─ Enqueued successfully
        │  │
        │  └─ [Monitoring subscriber]
        │      └─ xQueueSend(queue, event, 0)
        │          ├─ Queue size: 1/16 ✓
        │          └─ Enqueued successfully
        │
        └─ Release s_bus_lock

t=100ms: Another CAN_FRAME_READY published
        └─ Web Server still busy (hasn't processed first one yet)
           ├─ Queue size: 2/16 ✓
           └─ Enqueued

t=200ms: CAN_FRAME_READY published
        └─ Queue size: 3/16 ✓

...

t=800ms: CAN_FRAME_READY published
        └─ Queue size: 8/16 ✓

t=900ms: CAN_FRAME_READY published
        └─ Queue size: 9/16 ✓
           Web Server finally processing (slow task scheduling)

t=1000ms: CAN_FRAME_READY published
        ├─ Queue size: 10/16 ✓
        └─ Can still fit 6 more

t=1500ms: 16 consecutive events published
        ├─ Queue size goes 10 → 16 ✓
        └─ QUEUE FULL

t=1550ms: Another CAN_FRAME_READY published
        ├─ Web Server subscriber queue FULL (16/16)
        ├─ xQueueSend() returns FALSE
        ├─ Event dropped
        ├─ Counter: subscriber->dropped_events = 1
        ├─ If (dropped_events & (dropped_events-1)) == 0 → log warning
        │  └─ Log appears at counts: 1, 2, 4, 8, 16, 32... (power of 2)
        ├─
        └─ Web Browser NEVER sees this frame
           └─ Monitoring incomplete
           └─ Potential data loss

┌────────────────────────────────────────────────────────────────┐
│             VISIBLE INDICATORS IN LOGS                         │
└────────────────────────────────────────────────────────────────┘

W (12345) event_bus: Dropped event 0x00001202 for subscriber 0x3ffc2345 (1 total)
W (12567) event_bus: Dropped event 0x00001202 for subscriber 0x3ffc2345 (2 total)
W (13456) event_bus: Dropped event 0x00001202 for subscriber 0x3ffc2345 (4 total)
W (15432) event_bus: Dropped event 0x00001202 for subscriber 0x3ffc2345 (8 total)
...

┌────────────────────────────────────────────────────────────────┐
│              MITIGATION OPTIONS                                │
└────────────────────────────────────────────────────────────────┘

Option 1: Increase Queue Size
event_bus_subscribe(32, callback, NULL);  ← 16 to 32

Option 2: Blocking Publish for Critical Events
event_bus_publish(&event, pdMS_TO_TICKS(10));  ← Wait up to 10ms

Option 3: Subscribe Only to Needed Events
if (event->id == APP_EVENT_ID_CAN_FRAME_READY) {
    // Process only critical events
}

Option 4: Improve Web Server Task Scheduling
xTaskSetPriority(web_task, tskIDLE_PRIORITY + 3);  ← Higher priority
```

---

## Diagramme 7: Module Dependencies & Data Flow

```
┌────────────────────────────────────────────────────────────────┐
│               DEPENDENCY GRAPH                                 │
└────────────────────────────────────────────────────────────────┘

app_main.c (Entry Point)
│
├── event_bus ← Core dependency
│   ├── event_bus.h (API)
│   └── event_bus.c (Impl)
│
├── uart_bms
│   ├── uart_bms.h (API)
│   │   └── depends on: event_bus.h
│   │
│   └── uart_bms_protocol.c
│       ├── uart_bms_protocol.h
│       └── 59 register definitions
│
├── can_publisher
│   ├── can_publisher.h (API)
│   │   └── depends on: event_bus.h, uart_bms.h
│   │
│   ├── can_publisher.c (Main logic)
│   │   └── uses: uart_bms_live_data_t as input
│   │
│   ├── conversion_table.h/c
│   │   └── Encoder functions (BMS → CAN frames)
│   │
│   └── cvl_controller.c/cvl_logic.c
│       └── State machine (CVL logic) ← NEEDS MUTEX
│
├── can_victron
│   ├── can_victron.h (API)
│   │   └── depends on: event_bus.h
│   │
│   └── can_victron.c (TWAI driver)
│       └── Hardware UART/TX/RX via ESP32 TWAI
│
├── config_manager
│   ├── Configuration management
│   └── CAN/UART settings persistence
│
├── web_server
│   └── HTTP/WebSocket listener (async)
│
├── mqtt_gateway/mqtt_client
│   └── MQTT listener (async)
│
├── monitoring
│   └── Telemetry collector (async)
│
└── Others (wifi, status_led, history_logger)
    └── Event bus subscribers


DATA FLOW PATHS:
════════════════════════════════════════════════════════════════

Path 1: UART → BMS Live Data → CAN Frame → CAN Bus
  uart_bms_protocol.c → uart_bms.h (notify_listeners)
                     ↓
           can_publisher_on_bms_update()
                     ↓
            can_publisher_store_frame()
                     ↓
          can_publisher_dispatch_frame()
                     ↓
           can_victron_publish_frame()
                     ↓
              TWAI TX to CAN bus

Path 2: Event Bus Pub/Sub (Async)
  [Producer] publish_event()
                     ↓
          event_bus_publish()
                     ↓
         [All subscribers get notification]
                     ↓
  [Web Server] ← [Monitoring] ← [MQTT] ← [Status LED]

Path 3: CAN RX Reception
  TWAI RX ISR
                     ↓
       can_victron_handle_rx_message()
                     ↓
        Publish APP_EVENT_ID_CAN_FRAME_RAW
                     ↓
     [Event bus broadcasts to listeners]
```

---

## Diagramme 8: Critical Sections & Exclusion

```
┌────────────────────────────────────────────────────────────────┐
│          MUTEX LOCKING CRITICAL SECTIONS                       │
└────────────────────────────────────────────────────────────────┘

Mutex: s_bus_lock (Event Bus)
├─ Duration: O(N subscribers) where N ≈ 10
├─ Typical: 1-5ms
├─ Max: ~10ms (worst case: all subscribers slow)
├─ Protected: s_subscribers linked list
└─ Function: event_bus_publish()

Mutex: s_buffer_mutex (CAN Publisher frame storage)
├─ Duration: < 1ms (just copy frame)
├─ Timeout: 20ms
├─ Protected: s_frame_buffer[8]
├─ Protected: s_channel_deadlines[8]
└─ Functions: 
   ├─ can_publisher_store_frame() - write
   └─ can_publisher_publish_buffer() - read

Mutex: s_event_mutex (CAN Publisher events)
├─ Duration: < 1ms (just copy frame)
├─ Timeout: 20ms
├─ Protected: s_event_frames[8]
├─ Protected: s_event_frame_index
└─ Function: can_publisher_publish_event()

Mutex: s_twai_mutex (TWAI/CAN Driver)
├─ Duration: 5-20ms (hardware operation)
├─ Timeout: 20ms
├─ Protected: TWAI hardware registers
└─ Function: can_victron_publish_frame()

Mutex: s_driver_state_mutex (CAN Victron state)
├─ Duration: < 1ms (just flag read/write)
├─ Timeout: 20ms
├─ Protected: s_driver_started boolean
└─ Function: can_victron_is_driver_started()

NO MUTEX: s_cvl_state (CVL Controller) ❌ BUG!
├─ Duration: Should be < 1ms
├─ Current: UNPROTECTED
├─ Protected: cvl_state_t s_cvl_state
└─ Functions: 
   ├─ can_publisher_cvl_prepare() - write
   └─ fill_cvl_frame() - read
   
   RACE CONDITION POSSIBLE!


Priority Inversion Risk:
═══════════════════════════════════════════════════════════════

High Priority Task (CAN Victron, priority +6)
  ├─ Needs: s_buffer_mutex (held by low-priority task)
  ├─ Blocks: Waiting for mutex
  └─ Priority inversion!

Medium Priority Task (CAN Publisher task, priority +2)
  ├─ Holds: s_buffer_mutex
  ├─ Gets preempted: By higher priority task
  └─ Can't release: Because higher priority task acquired first

Low Priority Task (Event Bus publish)
  ├─ Holds: s_bus_lock
  ├─ Gets preempted: By higher priority task needing same lock
  └─ Starvation risk

Mitigation: FreeRTOS priority inheritance (if enabled)
  └─ Semaphore automatically boosts holder priority
```

