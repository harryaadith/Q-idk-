# Q-idk-

## Multi Agent Smart Bricks -- Prototype

Two `bricks` each with its own collection of sensors running fully independently without a central controller. Each brick reads its local RFID card, track presence (Along with a timeout check, so removal is detected) and brodcasts its state to another brick via ESP-NOW Protocol.<br>

## Color legend
### Red - No other bricks detected
### Blue - Other brick detected
### Green - RFID tag of other brick detected

## Connections (Identical to all bricks)

| Component pin        | ESP32 GPIO |
|-----------------------|------------|
| RC522 – SCK           | GPIO18     |
| RC522 – MISO          | GPIO19     |
| RC522 – MOSI          | GPIO23     |
| RC522 – SDA           | GPIO5      |
| RC522 – RST           | GPIO4      |
| RC522 – 3.3V          | 3V3        |
| RC522 – GND           | GND        |
| RGB LED – Red (220Ω)  | GPIO16     |
| RGB LED – Green (220Ω)| GPIO17     |
| RGB LED – Blue (220Ω) | GPIO25     |
| RGB LED – common      |    GND     |

NOTE - Within the sketch of each brick, change `BRICK_ID`
