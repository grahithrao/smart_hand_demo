# Smart Hand Gesture to Speech

This project implements a smart glove inspired by the paper:
`Sign Language to Speech: the Role of Conversion Gloves in Enhancing Communication` (ICSSES 2025).

It follows the same core architecture:

- Flex sensors on fingers
- IMU (MPU6050) for hand orientation
- Arduino UNO for processing
- Bluetooth output (HC-05) for wireless communication
- Text + speech output in a companion app

## What Is Included

- `firmware/smart_hand_glove/smart_hand_glove.ino`
  - Reads 5 flex sensors + MPU6050
  - Recognizes gestures with threshold logic
  - Sends recognized output over Serial + Bluetooth
  - Optional LCD support
  - EEPROM-backed calibration
- `app/smart_hand_receiver.py`
  - Receives Arduino/Bluetooth data
  - Displays live gesture stream
  - Speaks recognized text (TTS)
  - Logs sessions to CSV
- `app/mock_sender.py`
  - Simulates firmware output for testing without hardware
- `docs/WIRING.md`
  - Exact wiring map
- `docs/STEP_BY_STEP.md`
  - Full beginner-friendly build instructions

## Paper-Aligned Gesture Set

Default gestures included:

- `A`
- `B`
- `I_NEED_FOOD`
- `HELP`

Extra letter hooks (`C`, `D`, `E`, `G`) are provided in code comments so you can expand.

## Quick Start

1. Build wiring from `docs/WIRING.md`.
2. Upload firmware from `firmware/smart_hand_glove/smart_hand_glove.ino`.
3. Install Python requirements:

```bash
cd "/Volumes/grahith SSD/smart hand/app"
python3 -m pip install -r requirements.txt
```

4. Run receiver app:

```bash
python3 smart_hand_receiver.py --port /dev/tty.usbmodemXXXX --baud 115200 --speak
```

5. Follow calibration steps in `docs/STEP_BY_STEP.md`.

## Notes

- Full mode is recommended on boards with `A6` (for 5 flex + I2C IMU).
- UNO compatibility mode works, but IMU is disabled by default due pin overlap.
- If your Arduino IDE does not have `LiquidCrystal_I2C`, firmware still works without LCD.
- Bluetooth module HC-05 default baud is usually `9600`.
- Gesture thresholds must be tuned per glove and sensor placement.

## Docs

- Build steps: `docs/STEP_BY_STEP.md`
- Wiring map: `docs/WIRING.md`
- Paper mapping: `docs/PAPER_ALIGNMENT.md`
# smart_hand_demo
