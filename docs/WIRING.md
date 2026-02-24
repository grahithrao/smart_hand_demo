# Wiring Guide (Arduino UNO)

## Components

- Arduino UNO
- 5x flex sensors (thumb/index/middle/ring/pinky)
- 5x fixed resistors (typically 10k) for voltage divider
- MPU6050 IMU module
- HC-05 Bluetooth module
- Optional I2C LCD (16x2, address `0x27`)
- Breadboard + jumper wires

## Flex Sensor Wiring

Each flex sensor must be used in a voltage divider.

For each finger:

1. One end of flex sensor -> `5V`
2. Other end of flex sensor -> analog pin (`A0..A4`) and to one end of 10k resistor
3. Other end of resistor -> `GND`

Pin assignment in firmware:

### Full mode (board with `A6`, IMU enabled by default)

- `A0`: Thumb
- `A1`: Index
- `A2`: Middle
- `A3`: Ring
- `A6`: Pinky

### UNO compatibility mode (no `A6`, IMU disabled by default)

- `A0`: Thumb
- `A1`: Index
- `A2`: Middle
- `A3`: Ring
- `A4`: Pinky

## MPU6050 Wiring

- `VCC` -> `5V` (or `3.3V` depending on module)
- `GND` -> `GND`
- `SCL` -> `A5` (UNO SCL)
- `SDA` -> `A4` (UNO SDA)

Notes:

- In Full mode, `A4/A5` stay free for I2C (MPU6050/LCD).
- In UNO compatibility mode, IMU is off by default due this analog pin overlap.

## HC-05 Wiring

- `HC-05 TXD` -> Arduino `D10` (software serial RX)
- `HC-05 RXD` -> Arduino `D11` (software serial TX) through voltage divider (5V -> 3.3V safe)
- `VCC` -> `5V`
- `GND` -> `GND`

## Optional I2C LCD (16x2)

- `VCC` -> `5V`
- `GND` -> `GND`
- `SDA` -> `A4`
- `SCL` -> `A5`

## Important Conflict Note

On UNO, I2C uses `A4/A5`, so 5 flex inputs + MPU6050 is pin-limited without extra hardware.  
For full paper-like setup, use:

1. Arduino Nano (or board with `A6/A7`), or
2. External ADC/multiplexer for additional analog channels.
