# Power PINOUT Diagram Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update the editable Plant_Net PINOUT SVG so the real `+3.3 V`, `+12 V` and Micro-USB power paths are unambiguous.

**Architecture:** Keep `plant-net-pinout.svg` as one self-contained editable diagram. Use the established color language: red for positive supply, black for GND, orange for STM32 TX and blue for module TX to STM32 RX. Add labels rather than creating unsupported electrical links between the independent ESP32-CAM and extension board.

**Tech Stack:** SVG 1.1, project Markdown documentation.

---

### Task 1: Update power and reserve-interface labels

**Files:**
- Modify: `docs/assets/plant-net-pinout.svg`
- Test: visual opening in an SVG-capable viewer

- [x] **Step 1: Add the ESP-01S enable power relationship**

Add an `EN / CH_PD` line to the ESP-01S block and draw a red connection to the existing `+3.3V` network. Add the literal label `EN / CH_PD → +3.3V` beside it so its startup requirement cannot be missed.

- [x] **Step 2: Add the 12 V sensor power relationship**

Expand the tower-sensor block with `V+ 12V` and `GND` terminals. Draw the red `+12V` path independently from the `+3.3V` path and draw the black shared-ground path. Preserve A/B in purple/pink.

- [x] **Step 3: Mark ESP32-CAM as USB-only at the expansion board boundary**

Replace the generic independent-node label with the literal text `Micro-USB 5V 供电 / 烧录` and `不接扩展板`. Retain its dashed Wi-Fi/TCP indication to the server-side network only.

- [x] **Step 4: Mark all intentionally unconnected hardware interfaces**

Add a compact `预留 / NC` legend listing PC13, PD0/PD1, PB6/PB7, PA13/PA14, NRST and BOOT0. Do not draw signal traces for them.

- [x] **Step 5: Perform visual inspection**

Open `docs/assets/plant-net-pinout.svg` in an SVG-capable viewer. Confirm that each of these text strings is visible and not overlapped: `EN / CH_PD`, `+12V`, `Micro-USB 5V 供电 / 烧录`, `不接扩展板`, and `预留 / NC`.

Expected: the graphic has no crossing red `+3.3V`/`+12V` paths that could imply they are electrically common.
