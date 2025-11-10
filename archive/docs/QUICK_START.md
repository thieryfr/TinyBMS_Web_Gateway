# Quick Start: UART-CAN Analysis Reading Guide

## 📌 In 5 Minutes

### What You Need to Know
```
TinyBMS BMS (UART) → 
  UART Decoder → 
    BMS Live Data (500 bytes, 59 registers) →
      CAN Publisher (Synchronous callback) →
        Convert to 8 CAN frames (buffer) →
          TWAI TX (ESP32 hardware) →
            Victron Devices
```

### Critical Issues Found
1. **🔴 Race Condition in CVL State** - Unprotected shared state
2. **🔴 Event Drops on Queue Full** - Non-blocking publish can lose events
3. **🟠 Mutex Timeout Too Short** - 20ms timeout can fail under load
4. **🟠 No UART-CAN Decoupling** - Tight coupling between modules

---

## 🎯 Start Here Based on Your Role

### I'm a Manager/Product Owner
**Time: 10 minutes**
1. Read: SUMMARY_FR.md (sections "Overview" and "Critical Issues")
2. Key Number: 2 CRITICAL issues, 2 HIGH issues
3. Next: Share "Action Items" section with your team lead

### I'm Implementing the Fixes
**Time: 45 minutes**
1. Read: SUMMARY_FR.md "🚨 Problèmes Critiques" (10 min)
2. Read: uart_can_analysis.md Section 10 "Points d'Attention" (20 min)
3. Study: interaction_diagrams.md Diagrammes 4-6 (15 min)
4. Implement: Fix by priority order (see checklist in INDEX_ANALYSIS.md)

### I'm Reviewing Architecture
**Time: 90 minutes**
1. Read: uart_can_analysis.md Sections 1-6 (entire architecture)
2. Study: interaction_diagrams.md all diagrams
3. Review: Section 11 recommendations
4. Document: Your architectural decisions

### I'm Code Reviewing PRs
**Time: 30 minutes**
1. Quick reference: SUMMARY_FR.md synchronization table
2. Check: uart_can_analysis.md Section 6 "Mécanismes de Synchronisation"
3. Validate: Critical sections from Diagramme 8
4. Use: Checklist in INDEX_ANALYSIS.md for verification

---

## 🚨 Critical Issues Summary

### Issue #1: CVL State Race Condition (URGENT)
```
Thread 1: UART callback updates s_cvl_state.charging = 80A
Thread 2: CAN task reads s_cvl_state (reads partial update!)
Result:   Frame with inconsistent values sent to Victron
Risk:     Equipment may malfunction dangerously
```
**Fix:** Add mutex to cvl_controller.c
**Files:** `/main/can_publisher/cvl_controller.c`

### Issue #2: Event Drops (Queue Full)
```
Default queue size: 16 events
Publishers: 10-12 modules
Result: Web server queue fills up, events dropped silently
Risk:   UI and MQTT missing telemetry
```
**Fix:** Increase to 32 or use blocking publish
**Files:** `/main/event_bus/event_bus.c`, `/main/app_main.c`

### Issue #3: Mutex Timeout Too Short
```
Timeout: 20ms
Operation: TWAI TX can take 15-20ms under load
Result: Lock timeout → frame lost
Risk:   CAN bus telemetry gaps
```
**Fix:** Increase to 50ms
**Files:** `/main/can_publisher/can_publisher.c`

### Issue #4: No UART-CAN Decoupling
```
Current: UART → Direct callback → CAN Publisher
Risk:    If CAN Publisher busy, UART callback fails
```
**Fix:** Add intermediate queue
**Files:** Multiple (architecture change)

---

## 📊 Analysis by Numbers

| Metric | Value |
|--------|-------|
| Critical Issues | 2 |
| High Priority Issues | 2 |
| Medium Issues | 2 |
| Total Lines Analyzed | 2000+ |
| Documentation Generated | 15,000+ words |
| Diagrams Created | 8 |
| Mutex Count | 6 (1 missing!) |
| Task Count | 8 |

---

## 🔐 Synchronization Checklist

```
✅ event_bus.c:s_bus_lock
   └─ Protects: subscribers list
   └─ Status: OK, portMAX_DELAY timeout

✅ can_publisher.c:s_buffer_mutex
   └─ Protects: frame buffer (8 slots)
   └─ Status: ⚠️ Tight, 20ms timeout (see Issue #3)

✅ can_publisher.c:s_event_mutex
   └─ Protects: event frames
   └─ Status: OK

✅ can_victron.c:s_twai_mutex
   └─ Protects: TWAI hardware
   └─ Status: ⚠️ Tight, 20ms timeout

✅ can_victron.c:s_driver_state_mutex
   └─ Protects: driver started flag
   └─ Status: OK

❌ cvl_controller.c:s_cvl_state
   └─ Protects: NOTHING - UNPROTECTED!
   └─ Status: 🔴 BUG! (see Issue #1)
```

---

## 📁 File Location Quick Reference

| Module | File | Status | Note |
|--------|------|--------|------|
| Event Bus | `/main/event_bus/event_bus.c` | ✅ OK | Core infrastructure |
| UART BMS | `/main/uart_bms/uart_bms_protocol.c` | ✅ OK | Decoding works |
| CAN Publisher | `/main/can_publisher/can_publisher.c` | ⚠️ Issues | Timeout, decoupling |
| CVL Control | `/main/can_publisher/cvl_controller.c` | 🔴 BUG | Race condition! |
| CAN Driver | `/main/can_victron/can_victron.c` | ✅ OK | Tight timeout |
| App Init | `/main/app_main.c` | ✅ OK | Orchestration |

---

## 🎯 One-Page Action Plan

### Week 1 (CRITICAL)
- [ ] Add mutex to cvl_controller.c for s_cvl_state
- [ ] Increase CAN_PUBLISHER_LOCK_TIMEOUT_MS from 20 to 50

### Week 2-3 (HIGH)
- [ ] Increase Event Bus queue from 16 to 32
- [ ] Design UART→CAN decoupling queue

### Week 4+ (MEDIUM)
- [ ] Reduce keepalive task delay or make event-driven
- [ ] Add event bus statistics/monitoring

---

## 💡 Key Insights

1. **Architecture is Generally Good**
   - Modular design
   - Appropriate event patterns
   - Low latency critical path

2. **Synchronization is Almost Correct**
   - 5 out of 6 mutex areas protected
   - CVL state is the critical missing piece

3. **Performance is Acceptable**
   - 28-35ms latency (immediate mode) is fine
   - 50ms task cycle for keepalive is tight but OK

4. **Data Loss Risk is Real**
   - Event drops can happen with full queue
   - Frame loss under TWAI congestion
   - Race condition can corrupt CVL commands

5. **Easy Fixes Available**
   - Most issues have 1-3 hour fixes
   - No architectural redesign needed
   - Just need better synchronization

---

## 📞 Document Navigation

| If You Want... | Read... | Time |
|---|---|---|
| Executive summary | SUMMARY_FR.md | 5-10 min |
| Complete analysis | uart_can_analysis.md | 45-60 min |
| Visual diagrams | interaction_diagrams.md | 20-30 min |
| Detailed index | INDEX_ANALYSIS.md | 10-15 min |
| Quick start | This file | 5 min |

---

## ✅ Checklist Before You Start Coding

- [ ] I've read SUMMARY_FR.md
- [ ] I understand the 2 CRITICAL issues
- [ ] I know which files to modify
- [ ] I have a estimate of effort needed
- [ ] I have approval from code reviewer
- [ ] I have branch plan
- [ ] I have test plan
- [ ] I can explain the issue in 1 sentence

---

## 🚀 Go Implement!

**Step 1:** Pick Issue #1 (CVL Race Condition)  
**Step 2:** Read Section 10.1 in uart_can_analysis.md  
**Step 3:** Study Diagramme 5 in interaction_diagrams.md  
**Step 4:** Write code + tests  
**Step 5:** Code review + merge  
**Step 6:** Repeat for Issue #2, #3, #4...

---

**Last Updated:** 7 November 2025  
**Status:** Complete Analysis  
**Next:** Implementation Phase
