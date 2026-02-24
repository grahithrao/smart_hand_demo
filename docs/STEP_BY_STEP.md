# Step-by-Step Build Guide

This guide helps you build the same smart-hand concept from the paper, then improve it with practical features.

## 1. Decide Hardware Mode

Choose one mode first:

1. **Full Mode (recommended):**
   - Board with extra analog input (`A6`) such as Arduino Nano.
   - Uses 5 flex sensors + MPU6050 + Bluetooth.
2. **UNO Compatibility Mode:**
   - Arduino UNO with 5 flex sensors + Bluetooth.
   - IMU is disabled by default due pin overlap on `A4/A5`.

## 2. Assemble Hardware

1. Wire all components using `docs/WIRING.md`.
2. Verify all grounds are common.
3. Before powering, re-check HC-05 RX voltage divider (important).

## 3. Upload Firmware

1. Open Arduino IDE.
2. Open file:
   - `firmware/smart_hand_glove/smart_hand_glove.ino`
3. Select board + port.
4. Install missing libraries if prompted.
5. Upload sketch.

## 4. Verify Serial Output

1. Open Serial Monitor at `115200`.
2. You should see boot messages:
   - `IMU_DEFAULT=ON` or `IMU_DEFAULT=OFF`
   - `CALIBRATION_LOADED` or `DEFAULT_CALIBRATION_ACTIVE`
3. Run command in Serial Monitor:
   - `READINGS`
4. Move fingers and confirm raw values change.

## 5. Calibrate Flex Sensors

For each finger (`THUMB`, `INDEX`, `MIDDLE`, `RING`, `PINKY`):

1. Keep finger straight and note raw value from `READINGS`.
2. Fully bend finger and note raw value.
3. Set calibration:

```
SET_CAL THUMB 550 760
SET_CAL INDEX 540 780
SET_CAL MIDDLE 530 790
SET_CAL RING 530 800
SET_CAL PINKY 520 790
```

4. Save calibration:

```
SAVE_CAL
SHOW_CAL
```

## 6. Install Receiver App

From terminal:

```bash
cd "/Volumes/grahith SSD/smart hand/app"
python3 -m pip install -r requirements.txt
```

## 7. Run Live Gesture Receiver

USB serial example:

```bash
cd "/Volumes/grahith SSD/smart hand/app"
python3 smart_hand_receiver.py --port /dev/tty.usbmodemXXXX --baud 115200 --speak
```

If using Bluetooth serial profile, pass that port instead.

## 8. Validate Paper Gesture Set

Test these gestures and confirm output:

1. `A`
2. `B`
3. `I need food`
4. `Help`

If wrong outputs appear:

1. Recheck calibration values.
2. Tune `BENT_THRESHOLD` and `OPEN_THRESHOLD` in firmware.
3. In Full Mode, verify MPU6050 orientation values with `READINGS`.

## 9. Nice Features Already Included

1. Stability filtering to reduce false positives.
2. Repeat lockout to avoid duplicate speech spam.
3. EEPROM calibration persistence.
4. CSV logging of recognition sessions.
5. Phrase builder in receiver app.

## 10. Recommended Next Improvements

1. Add per-user calibration wizard over Bluetooth.
2. Extend dataset to full alphabet + common phrases.
3. Train a lightweight classifier from logged CSV instead of pure thresholds.
4. Build Android app that directly consumes same JSON packets from HC-05.
