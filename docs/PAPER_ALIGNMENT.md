# Paper Alignment Notes

Reference used:
`Sign Language to Speech: the Role of Conversion Gloves in Enhancing Communication` (ICSSES 2025).

## Elements Matched Directly

- Flex-sensor-based glove input
- IMU usage for orientation cues (pitch/roll/yaw)
- Arduino-based embedded processing
- Bluetooth data transfer path
- Conversion to text and speech output
- Gesture set focus:
  - `A`
  - `B`
  - `I Need Food`
  - `Help`

## Values Reflected in Current Build

Target accuracy placeholders in app config (`app/gestures_config.json`) mirror paper-reported values:

- `A`: 95.2%
- `B`: 92.5%
- `I_NEED_FOOD`: 88.7%
- `HELP`: 90.3%

## Practical Engineering Additions

- EEPROM-backed calibration persistence
- Stable-frame filtering and duplicate lockout
- JSON packet format for easier app integration
- CSV logging for model improvement and benchmarking
- UNO compatibility fallback (IMU off by default when analog pins are limited)

## Why Calibration Is Critical

Flex sensors vary significantly by:

- sensor vendor
- glove fabric tension
- mounting angle
- finger length and hand posture

So this implementation uses paper-aligned logic with user-specific threshold calibration to achieve repeatable real-world behavior.
