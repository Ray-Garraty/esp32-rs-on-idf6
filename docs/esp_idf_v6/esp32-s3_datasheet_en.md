---
type: ESP32 Reference
title: ESP32-S3 Series Datasheet v2.2 + Migration Plan
description: Official Espressif ESP32-S3 datasheet with appended migration plan for porting from classic ESP32
tags: [esp32-s3, hardware, datasheet, reference, migration]
timestamp: 2026-07-06
---

# ESP32-S3 Series Datasheet v2.2

ESP32-S3 Series
DatasheetVersion 2.2
## Xtensa
## ®
32-bit LX7 dual-core microprocessor
2.4 GHz Wi-Fi (IEEE 802.11b/g/n) and Bluetooth
## ®
## 5 (LE)
Optional 1.8 V or 3.3 V flash and PSRAM in the chip’s package
45 GPIOs
QFN56 (7×7 mm) Package
## Including:
## ESP32-S3
## ESP32-S3FN8
## ESP32-S3RH2
## ESP32-S3R8
## ESP32-S3R16V
## ESP32-S3FH4R2
## ESP32-S3R8V –
Endoflife(EOL)
ESP32-S3R2 –Endoflife(EOL), upgraded to ESP32-S3RH2
www.espressif.com

## Product Overview
ESP32-S3 is a low-power MCU-based system on a chip (SoC) with integrated 2.4 GHz Wi-Fi and Bluetooth
## ®
Low Energy (Bluetooth LE). It consists of high-performance dual-core microprocessor (Xtensa
## ®
32-bit LX7), a
ULP coprocessor, a Wi-Fi baseband, a Bluetooth LE baseband, RFmodule, and numerousperipherals.
The functional block diagram of the SoC is shown below.
Espressif ESP32-S3 Wi-Fi + Bluetooth
## ®
Low Energy SoC
Power consumption
## Normal
Low power consumption components capable of working in Deep-sleep mode
## Wireless Digital Circuits
Wi-Fi MAC
Wi-Fi
## Baseband
Bluetooth LE Link Controller
Bluetooth LE Baseband
## Security
## Flash
## Encryption
## RSARNG
## RSA_DS
## SHAAES
## HMAC
## Secure Boot
## RTC
## RTC
## Memory
## PMU
ULP Coprocessor
## Peripherals
USB Serial/
## JTAG
## GPIO
## UART
## TWAI
## ®
## General-
purpose
## Timers
## I2S
## I2C
## Pulse
## Counter
## LED PWM
## Camera
## Interface
## SPI0/1
## RMT
## SPI2/3
## DIG ADC
## System
## Timer
## RTC GPIO
## Temperature
## Sensor
## RTC
## Watchdog
## Timer
## GDMA
## LCD
## Interface
## RTC ADC
## SD/MMC
## Host
## MCPWM
## USB OTG
eFuse
## Controller
## Touch
## Sensor
## RTC I2C
## RF
2.4 GHz Balun +
## Switch
2.4 GHz
## Receiver
2.4 GHz
## Transmitter
## RF
## Synthesizer
Fast RC
## Oscillator
## External
## Main Clock
## Phase Lock
## Loop
## Super
## Watchdog
CPU and Memory
## Xtensa
## ®
Dual-core 32-bit LX7
## Microprocessor
## JTAG
## Cache
## ROM
## SRAM
## Interrupt
## Matrix
## Permission
## Control
## World
## Controller
## Main System
## Watchdog
## Timers
ESP32-S3 Functional Block Diagram
For more information on power consumption, see Section4.1.3.5Power Management Unit (PMU).
## Espressif Systems2
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Features
Wi-Fi
•Complies with IEEE 802.11b/g/n
•Supports 20 MHz and 40 MHz bandwidth in 2.4 GHz band
•1T1R mode with data rate up to 150 Mbps
•Wi-Fi Multimedia (WMM)
## •TX/RX A-MPDU, TX/RX A-MSDU
•Immediate Block ACK
•Fragmentation and defragmentation
•Automatic Beacon monitoring (hardware TSF)
•Four virtual Wi-Fi interfaces
•Simultaneous support for Infrastructure BSS in Station, SoftAP, or Station + SoftAP modes
Note that when ESP32-S3 scans in Station mode, the SoftAP channel will change along with the Station
channel
•Antenna diversity
•802.11mc FTM
## Bluetooth
## ®
•Bluetooth LE: Bluetooth 5, Bluetooth Mesh
•High-power mode with up to 20 dBm transmission power
•Speed: 125 Kbps, 500 Kbps, 1 Mbps, 2 Mbps
•LE Advertising Extensions
•Multiple Advertising Sets
•LE Channel Selection Algorithm #2
•Internal co-existence mechanism between Wi-Fi and Bluetooth to share the same antenna
CPU and Memory
•Xtensa
## ®
dual-core 32-bit LX7 microprocessor
•Clock speed: up to 240 MHz
•CoreMark
## ®
score:
–Two cores at 240 MHz: 1329.92 CoreMark; 5.54 CoreMark/MHz
•Five-stage pipeline
•128-bit data bus and dedicated SIMD instructions
•Single precision floating point unit (FPU)
## Espressif Systems3
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

•Ultra-Low-Power (ULP) coprocessors:
–ULP-RISC-V coprocessor
–ULP-FSM coprocessor
•General DMA controller, with 5 transmit channels and 5 receive channels
•L1 cache
## •ROM: 384 KB
## •SRAM: 512 KB
•SRAM in RTC: 16 KB
•4096-bit eFuse memory, up to 1792 bits for users
•Supported SPI protocols: SPI, Dual SPI, Quad SPI, Octal SPI, QPI and OPI interfaces that allow
connection to flash, external RAM, and other SPI devices
•Flash controller with cache is supported
•Flash in-Circuit Programming (ICP) is supported
## Peripherals
•45 programmable GPIOs
–4 strapping GPIOs
–GPIOs allocated for in-package memory:
*6 GPIOs for eitherin-package flashor PSRAM
*7 GPIOs when bothin-package flashand PSRAM are integrated
•Connectivity interfaces:
–Three UART interfaces
–Two I2C interfaces
–Two I2S interfaces
–LCD interface
–8-bit~16-bit DVP camera interface
–Two SPI ports for communication with flash and RAM
–Two general-purpose SPI ports
## –TWAI
## ®
controller, compatible with ISO 11898-1 (CAN Specification 2.0)
–Full-speed USB OTG
–USB Serial/JTAG controller
–SD/MMC host controller with 2 slots
–LED PWM controller, up to 8 channels
–Two Motor Control PWM (MCPWM)
## Espressif Systems4
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## –RMT (TX/RX)
–Pulse count controller
•Analog signal processing:
–Two 12-bit SAR ADCs, up to 20 channels
–Temperature sensor
–14 capacitive touch sensing IOs
•Timers:
–Four 54-bit general-purpose timers
–52-bit system timer
–Three watchdog timers
## Power Management
•Fine-resolution power control, including clock frequency, duty cycle, Wi-Fi operating modes, and
individual internal component control
•Four power modes designed for typical scenarios: Active, Modem-sleep, Light-sleep, Deep-sleep
•Power consumption in Deep-sleep mode is 7μA
•RTC memory remains powered on in Deep-sleep mode
## Security
•Secure boot - permission control on accessing internal and external memory
•Flash encryption - memory encryption and decryption
•Cryptographic hardware acceleration:
–SHA Accelerator (FIPS PUB 180-4)
–AES Accelerator (FIPS PUB 197)
–RSA Accelerator
–HMAC Accelerator
–RSA Digital Signature Peripheral (RSA_DS)
–Random Number Generator (RNG)
RF Module
•Antenna switches, RF balun, power amplifier, low-noise receive amplifier
•Up to +21 dBm of power for an 802.11b transmission
•Up to +19.5 dBm of power for an 802.11n transmission
•Up to -104.5 dBm of sensitivity for Bluetooth LE receiver (125 Kbps)
## Espressif Systems5
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Applications
With low power consumption, ESP32-S3 is an ideal choice for IoT devices in the following areas:
•Smart Home
•Industrial Automation
•Health Care
•Consumer Electronics
•Smart Agriculture
•POS Machines
•Service Robot
•Audio Devices
•Generic Low-power IoT Sensor Hubs
•Generic Low-power IoT Data Loggers
•Cameras for Video Streaming
•USB Devices
•Speech Recognition
•Image Recognition
•Wi-Fi + Bluetooth Networking Card
•Touch and Proximity Sensing
## Espressif Systems6
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Contents
## Note:
Check the link or the QR code to make sure that you use the latest version of this document:
https://www.espressif.com/documentation/esp32-s3_datasheet_en.pdf
## Contents
## Product Overview2
## Features3
## Applications6
1  ESP32-S3 Series Comparison13
## 1.1    Nomenclature13
## 1.2   Comparison13
## 1.3   Chip Revision14
## 2  Pins15
## 2.1   Pin Layout15
## 2.2   Pin Overview16
2.3   IO Pins20
2.3.1   IO MUX Functions20
2.3.2  RTC Functions23
## 2.3.3  Analog Functions24
2.3.4  Restrictions for GPIOs and RTC_GPIOs25
## 2.3.5  Peripheral Pin Assignment26
## 2.4   Analog Pins28
## 2.5   Power Supply29
## 2.5.1   Power Pins29
## 2.5.2  Power Scheme29
2.5.3  Chip Power-up and Reset30
2.6   Pin Mapping Between Chip and Flash/PSRAM31
## 3  Boot Configurations32
## 3.1   Chip Boot Mode Control33
3.2   VDD_SPI Voltage Control34
3.3   ROM Messages Printing Control34
3.4   JTAG Signal Source Control34
## 4  Functional Description36
## 4.1   System36
4.1.1   Microprocessor and Master36
## 4.1.1.1CPU36
4.1.1.2    Processor Instruction Extensions (PIE)36
## Espressif Systems7
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Contents
4.1.1.3    Ultra-Low-Power Coprocessor (ULP)37
4.1.1.4    GDMA Controller (GDMA)37
## 4.1.2   Memory Organization38
## 4.1.2.1    Internal Memory38
4.1.2.2   External Flash and RAM39
## 4.1.2.3   Cache39
4.1.2.4   eFuse Controller40
## 4.1.3   System Components40
4.1.3.1    IO MUX and GPIO Matrix40
## 4.1.3.2   Reset41
## 4.1.3.3   Clock41
## 4.1.3.4   Interrupt Matrix42
4.1.3.5   Power Management Unit (PMU)42
## 4.1.3.6   System Timer44
## 4.1.3.7   General Purpose Timers44
## 4.1.3.8   Watchdog Timers45
4.1.3.9   XTAL32K Watchdog Timers45
## 4.1.3.10   Permission Control45
## 4.1.3.11   World Controller46
## 4.1.3.12   System Registers47
4.1.4   Cryptography and Security Component47
4.1.4.1    SHA Accelerator47
4.1.4.2   AES Accelerator48
4.1.4.3   RSA Accelerator48
## 4.1.4.4   Secure Boot48
4.1.4.5   HMAC Accelerator49
4.1.4.6   RSA Digital Signature Peripheral (RSA_DS)49
4.1.4.7   External Memory Encryption and Decryption49
## 4.1.4.8   Clock Glitch Detection50
## 4.1.4.9   Random Number Generator50
## 4.2   Peripherals51
## 4.2.1   Connectivity Interface51
4.2.1.1    UART Controller51
4.2.1.2   I2C Interface51
4.2.1.3   I2S Interface52
4.2.1.4   LCD and Camera Controller52
4.2.1.5   Serial Peripheral Interface (SPI)53
4.2.1.6   Two-Wire Automotive Interface (TWAI
## ®
## )54
4.2.1.7   USB 2.0 OTG Full-Speed Interface55
4.2.1.8   USB Serial/JTAG Controller56
4.2.1.9   SD/MMC Host Controller56
4.2.1.10   Motor Control PWM (MCPWM)57
4.2.1.11   Remote Control Peripheral (RMT)58
4.2.1.12   Pulse Count Controller (PCNT)58
## 4.2.2  Analog Signal Processing59
## 4.2.2.1   SAR ADC59
## Espressif Systems8
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Contents
## 4.2.2.2  Temperature Sensor59
## 4.2.2.3  Touch Sensor59
## 4.3   Wireless Communication61
## 4.3.1   Radio61
4.3.1.1    2.4 GHz Receiver61
4.3.1.2   2.4 GHz Transmitter61
## 4.3.1.3   Clock Generator61
4.3.2  Wi-Fi61
4.3.2.1   Wi-Fi Radio and Baseband62
4.3.2.2  Wi-Fi MAC62
## 4.3.2.3   Networking Features62
4.3.3  Bluetooth LE62
4.3.3.1   Bluetooth LE PHY63
4.3.3.2   Bluetooth LE Link Controller63
## 5  Electrical Characteristics64
## 5.1   Absolute Maximum Ratings64
## 5.2   Recommended Operating Conditions64
5.3   VDD_SPI Output Characteristics65
5.4   DC Characteristics (3.3 V, 25 °C)65
5.5   ADC Characteristics66
## 5.6   Current Consumption66
5.6.1   Current Consumption in Active Mode66
5.6.2  Current Consumption in Other Modes67
## 5.7   Memory Specifications68
## 5.8   Reliability69
6  RF Characteristics70
6.1   Wi-Fi Radio70
6.1.1   Wi-Fi RF Transmitter (TX) Characteristics70
6.1.2   Wi-Fi RF Receiver (RX) Characteristics71
6.2   Bluetooth LE Radio72
6.2.1   Bluetooth LE RF Transmitter (TX) Characteristics73
6.2.2  Bluetooth LE RF Receiver (RX) Characteristics74
## 7  Packaging77
ESP32-S3 Consolidated Pin Overview79
## Datasheet Versioning80
## Glossary81
Related Documentation and Resources82
## Revision History83
## Espressif Systems9
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

List of Tables
List of Tables
1-1  ESP32-S3 Series Comparison13
## 2-1  Pin Overview16
2-2 Power-Up Glitches on Pins18
2-3 Peripheral Signals Routed via IO MUX20
2-4 IO MUX Functions21
2-5 RTC Peripheral Signals Routed via RTC IO MUX23
2-6 RTC Functions23
2-7  Analog Signals Routed to Analog Functions24
## 2-8 Analog Functions24
## 2-9 Peripheral Pin Assignment27
## 2-10 Analog Pins28
## 2-11 Power Pins29
## 2-12 Voltage Regulators29
2-13 Description of Timing Parameters for Power-up and Reset30
2-14 Pin Mapping Between Chip and Flash or PSRAM31
3-1  Default Configuration of Strapping Pins32
3-2 Description of Timing Parameters for the Strapping Pins33
## 3-3 Chip Boot Mode Control33
3-4 VDD_SPI Voltage Control34
3-5 JTAG Signal Source Control35
4-1  Components and Power Domains44
## 5-1  Absolute Maximum Ratings64
## 5-2 Recommended Operating Conditions64
5-3 VDD_SPI Internal and Output Characteristics65
5-4 DC Characteristics (3.3 V, 25 °C)65
5-5 ADC Characteristics66
5-6 ADC Calibration Results66
5-7  Current Consumption for Wi-Fi (2.4 GHz) in Active Mode66
5-8 Current Consumption for Bluetooth LE in Active Mode67
5-9 Current Consumption in Modem-sleep Mode67
5-10 Current Consumption in Low-Power Modes68
## 5-11 Flash Specifications68
5-12 PSRAM Specifications69
## 5-13 Reliability Qualifications69
6-1  Wi-Fi RF Characteristics70
6-2 TX Power with Spectral Mask and EVM Meeting 802.11 Standards70
6-3 TX EVM Test
## 1
## 70
6-4 RX Sensitivity71
6-5 Maximum RX Level72
6-6 RX Adjacent Channel Rejection72
6-7  Bluetooth LE Frequency72
6-8 Transmitter Characteristics - Bluetooth LE 1 Mbps73
6-9 Transmitter Characteristics - Bluetooth LE 2 Mbps73
## Espressif Systems10
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

List of Tables
6-10 Transmitter Characteristics - Bluetooth LE 125 Kbps73
6-11 Transmitter Characteristics - Bluetooth LE 500 Kbps74
6-12 Receiver Characteristics - Bluetooth LE 1 Mbps74
6-13 Receiver Characteristics - Bluetooth LE 2 Mbps75
6-14 Receiver Characteristics - Bluetooth LE 125 Kbps75
6-15 Receiver Characteristics - Bluetooth LE 500 Kbps76
## 7-1  Consolidated Pin Overview79
## Espressif Systems11
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

List of Figures
List of Figures
1-1  ESP32-S3 Series Nomenclature13
2-1  ESP32-S3 Pin Layout (Top View)15
2-2 ESP32-S3 Power Scheme30
2-3 Visualization of Timing Parameters for Power-up and Reset30
3-1  Visualization of Timing Parameters for the Strapping Pins33
## 4-1  Address Mapping Structure38
4-2 Components and Power Domains43
7-1  QFN56 (7×7 mm) Package77
7-2  QFN56 (7×7 mm) Package (Only for ESP32-S3FH4R2)78
## Espressif Systems12
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

1  ESP32-S3 Series Comparison
1ESP32-S3 Series Comparison
1.1Nomenclature
## ESP32-S3
## ESP32-S3
## F
## F
## H/N
## H/N
x
x
Flash size (MB)
Flash temperature
H: High temperature
N: Normal temperature
## Flash
Chip series
## R
## R
x
x
## V
## V
1.8 V external SPI flash only
PSRAM size (MB)
## PSRAM
## H
## H
PSRAM temperature
H: High temperature
Figure 1-1. ESP32-S3 Series Nomenclature
1.2Comparison
Table 1-1. ESP32-S3 Series Comparison
## Part Number
## 1
In-Package Flash
## 2
In-Package PSRAMAmbient Temp.
## 3
VDD_SPI Voltage
## 4
## Chip Revision
ESP32-S3——40∼105 °C3.3 V/1.8 Vv0.1/v0.2
ESP32-S3FN88 MB (Quad SPI)
## 5
—40∼85 °C3.3 Vv0.1/v0.2
ESP32-S3RH2—2 MB (Quad SPI)40∼105 °C3.3 Vv0.2
ESP32-S3R8—8 MB (Octal SPI)40∼65 °C3.3 Vv0.1/v0.2
ESP32-S3R16V—16 MB (Octal SPI)40∼65 °C1.8 Vv0.2
ESP32-S3FH4R24 MB (Quad SPI)2 MB (Quad SPI)40∼85 °C3.3 Vv0.1/v0.2
ESP32-S3R8V (EOL)—8 MB (Octal SPI)40∼65 °C1.8 Vv0.1/v0.2
## ESP32-S3R2 (EOL)
## 6
—2 MB (Quad SPI)40∼85 °C3.3 Vv0.1/v0.2
## 1
For details on chip marking and packing, see Section7Packaging.
## 2
For information aboutin-package flash, see also Section4.1.2.1Internal Memory. By default, the SPI flash on the
chip operates at a maximum clock frequency of 80 MHz and does not support the auto suspend feature. If you have
a requirement for a higher flash clock frequency of 120 MHz or if you need the flash auto suspend feature, please
contactus.
## 3
Ambient temperature specifies the recommended temperature range of the environment immediately outside an
Espressif chip. For chips with Octal SPI PSRAM (ESP32-S3R8, ESP32-S3R8V, and ESP32-S3R16V), if the PSRAM ECC
function is enabled, the maximum ambient temperature can be improved to 85 °C, while the usable size of PSRAM will
be reduced by 1/16.
## 4
For more information on VDD_SPI, see Section2.5Power Supply.
## 5
For details about SPI modes, see Section2.6Pin Mapping Between Chip and Flash/PSRAM.
## 6
ESP32-S3R2 has been upgraded to ESP32-S3RH2. For more information, seePCN.
## Espressif Systems13
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

1  ESP32-S3 Series Comparison
1.3Chip Revision
As shown in Table1-1ESP32-S3 Series Comparison, ESP32-S3 now has multiple chip revisions available on
the market using the same part number.
For chip revision identification, ESP-IDF release that supports a specific chip revision, and errors fixed in each
chip revision, please refer toESP32-S3 Series SoC Errata.
## Espressif Systems14
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2Pins
2.1Pin Layout
## 1
## 2
## 3
## 4
## 5
## 6
## 7
## 8
## 9
## 29
## 30
## 31
## 32
## 33
## 34
## 35
## 36
## 37
## 38
## 39
## 40
## 41
## 42
## 15161718192021222324252628
## 4546474849505152535455564443
## ESP32-S3
## 13
## 14
## 10
## 11
## 12
## GPIO20
## 27
## GPIO21GPIO19GPIO18GPIO17
## XTAL_32K_N
## XTAL_32K_P
## VDD3P3_RTC
## GPIO14GPIO13GPIO12GPIO11GPIO10
## GPIO9
## GPIO8
## GPIO7
## GPIO6
## GPIO5
## GPIO4
## GPIO3
## GPIO2
## GPIO1
## GPIO0
## CHIP_PU
## VDD3P3
## VDD3P3
## LNA_IN
## VDDAXTAL_PXTAL_NGPIO46GPIO45U0RXDU0TXDMTMSMTDIVDD3P3_CPUMTDOMTCKGPIO38VDDA
## GPIO37
## GPIO36
## GPIO35
## GPIO34
## GPIO33
## SPICLK_P
## SPID
## SPIQ
## SPICLK
## SPICS0
## SPIWP
## SPIHD
## VDD_SPI
## 57 GND
## SPICS1
## SPICLK_N
Figure 2-1. ESP32-S3 Pin Layout (Top View)
## Espressif Systems15
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.2Pin Overview
The ESP32-S3 chip integrates multiple peripherals that require communication with the outside world. To keep
the chip package size reasonably small, the number of available pins has to be limited. So the only way to
route all the incoming and outgoing signals is through pin multiplexing. Pin muxing is controlled via software
programmable registers (seeESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO
## Matrix).
All in all, the ESP32-S3 chip has the following types of pins:
•IO pinswith the following predefined sets of functions to choose from:
–EachIO pin has predefinedIO MUX functions– see Table2-4IO MUX Functions
–SomeIO pins have predefinedRTC functions– see Table2-6RTC Functions
–SomeIO pins have predefinedanalog functions– see Table2-8Analog Functions
Predefined functionsmeans that each IO pin has a set of direct connections to certain on-chip
peripherals. During run-time, the user can configure which peripheral from a predefined set to connect
to a certain pin at a certain time via memory mapped registers (see
ESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO pins).
•Analog pinsthat have exclusively-dedicatedanalog functions– see Table2-10Analog Pins
•Power pinsthat supply power to the chip components and non-power pins – see Table2-11Power Pins
Table2-1Pin Overviewgives an overview of all the pins. For more information, see the respective sections for
each pin type below, orESP32-S3 Consolidated Pin Overview.
## Table 2-1. Pin Overview
## Pin Settings
## 6
## Pin Function Sets
## 1
Pin No.Pin NamePin TypePin Providing Power
## 2-5
At ResetAfter ResetIO MUXRTC IO MUXAnalog
1LNA_INAnalog
2VDD3P3Power
3VDD3P3Power
4CHIP_PUAnalogVDD3P3_RTC
## 5GPIO0IOVDD3P3_RTCWPU, IEWPU, IEIO MUXRTC IO MUX
6GPIO1IOVDD3P3_RTCIEIEIO MUXRTC IO MUXAnalog
7GPIO2IOVDD3P3_RTCIEIEIO MUXRTC IO MUXAnalog
8GPIO3IOVDD3P3_RTCIEIEIO MUXRTC IO MUXAnalog
9GPIO4IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
10GPIO5IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
11GPIO6IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
12GPIO7IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
13GPIO8IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
14GPIO9IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
15GPIO10IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
16GPIO11IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
17GPIO12IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
18GPIO13IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
Cont’d on next page
## Espressif Systems16
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
Cont’d from previous page
## Pin Settings
## 6
## Pin Function Sets
## 1
Pin No.Pin NamePin TypePin Providing Power
## 2-5
At ResetAfter ResetIO MUXRTC IO MUXAnalog
19GPIO14IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
20VDD3P3_RTCPower
21XTAL_32K_PIOVDD3P3_RTCIO MUXRTC IO MUXAnalog
22XTAL_32K_NIOVDD3P3_RTCIO MUXRTC IO MUXAnalog
23GPIO17IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
24GPIO18IOVDD3P3_RTCIEIO MUXRTC IO MUXAnalog
25GPIO19IOVDD3P3_RTCIO MUXRTC IO MUXAnalog
26GPIO20IOVDD3P3_RTCUSB_PUUSB_PUIO MUXRTC IO MUXAnalog
## 27GPIO21IOVDD3P3_RTCIO MUXRTC IO MUX
## 28SPICS1IOVDD_SPIWPU, IEWPU, IEIO MUX
29VDD_SPIPower
## 30SPIHDIOVDD_SPIWPU, IEWPU, IEIO MUX
## 31SPIWPIOVDD_SPIWPU, IEWPU, IEIO MUX
## 32SPICS0IOVDD_SPIWPU, IEWPU, IEIO MUX
## 33SPICLKIOVDD_SPIWPU, IEWPU, IEIO MUX
## 34SPIQIOVDD_SPIWPU, IEWPU, IEIO MUX
## 35SPIDIOVDD_SPIWPU, IEWPU, IEIO MUX
## 36SPICLK_NIOVDD_SPI/VDD3P3_CPUIEIEIO MUX
## 37SPICLK_PIOVDD_SPI/VDD3P3_CPUIEIEIO MUX
## 38GPIO33IOVDD_SPI/VDD3P3_CPUIEIO MUX
## 39GPIO34IOVDD_SPI/VDD3P3_CPUIEIO MUX
## 40GPIO35IOVDD_SPI/VDD3P3_CPUIEIO MUX
## 41GPIO36IOVDD_SPI/VDD3P3_CPUIEIO MUX
## 42GPIO37IOVDD_SPI/VDD3P3_CPUIEIO MUX
## 43GPIO38IOVDD3P3_CPUIEIO MUX
## 44MTCKIOVDD3P3_CPUIE
## 7
## IO MUX
## 45MTDOIOVDD3P3_CPUIEIO MUX
46VDD3P3_CPUPower
## 47MTDIIOVDD3P3_CPUIEIO MUX
## 48MTMSIOVDD3P3_CPUIEIO MUX
## 49U0TXDIOVDD3P3_CPUWPU, IEWPU, IEIO MUX
## 50U0RXDIOVDD3P3_CPUWPU, IEWPU, IEIO MUX
## 51GPIO45IOVDD3P3_CPUWPD, IEWPD, IEIO MUX
## 52GPIO46IOVDD3P3_CPUWPD, IEWPD, IEIO MUX
53XTAL_NAnalog
54XTAL_PAnalog
55VDDAPower
56VDDAPower
57GNDPower
1.Boldmarks the pin function set in which a pin has its default function in the default boot mode. For more information about the
boot modesee Section3.1Chip Boot Mode Control.
2.In columnPin Providing Power, regarding pins powered by VDD_SPI:
•Power actually comes from the internal power rail supplying power to VDD_SPI. For details, see Section2.5.2Power
## Scheme.
3.In columnPin Providing Power, regarding pins powered by VDD3P3_CPU / VDD_SPI:
## Espressif Systems17
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
•Pin Providing Power (either VDD3P3_CPU or VDD_SPI) is decided by eFuse bit EFUSE_PIN_POWER_SELECTION (see
ESP32-S3 Technical Reference Manual> ChaptereFuse Controller) and can be configured via the
IO_MUX_PAD_POWER_CTRL bit (seeESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO pins).
4.For ESP32-S3R8V and ESP32-S3R16V chip, as the VDD_SPI voltage has been set to 1.8 V, the working voltage for pins SPICLK_N
and SPICLK_P (GPIO47 and GPIO48) would also be 1.8 V, which is different from other GPIOs.
5.The default drive strengths for each pin are as follows:
•GPIO17 and GPIO18: 10 mA
•GPIO19 and GPIO20: 40 mA
•All other pins: 20 mA
6.ColumnPin Settingsshows predefined settings at reset and after reset with the following abbreviations:
•IE – input enabled
•WPU – internal weak pull-up resistor enabled
•WPD – internal weak pull-down resistor enabled
•USB_PU – USB pull-up resistor enabled
–By default, the USB function is enabled for USB pins (i.e., GPIO19 and GPIO20), and the pin pull-up is decided by the
USB pull-up. The USB pull-up is controlled by USB_SERIAL_JTAG_DP/DM_PULLUP and the pull-up resistor value is
controlled by USB_SERIAL_JTAG_PULLUP_VALUE. For details, seeESP32-S3 Technical Reference Manual> Chapter
USB Serial/JTAG Controller).
–When the USB function is disabled, USB pins are used as regular GPIOs and the pin’s internal weak pull-up and
pull-down resistors are disabled by default (configurable by IO_MUX_FUN_
WPU/WPD). For details, seeESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO Matrix.
7.Depends on the value of EFUSE_DIS_PAD_JTAG
•0- WPU is enabled
•1- pin floating
Some pins have glitches during power-up. See details in Table
## 2-2.
Table 2-2. Power-Up Glitches on Pins
PinGlitch
## 1
## Typical Time Period (μs)
GPIO1Low-level glitch60
GPIO2Low-level glitch60
GPIO3Low-level glitch60
GPIO4Low-level glitch60
GPIO5Low-level glitch60
GPIO6Low-level glitch60
GPIO7Low-level glitch60
GPIO8Low-level glitch60
GPIO9Low-level glitch60
GPIO10Low-level glitch60
GPIO11Low-level glitch60
GPIO12Low-level glitch60
GPIO13Low-level glitch60
GPIO14Low-level glitch60
XTAL_32K_PLow-level glitch60
XTAL_32K_NLow-level glitch60
GPIO17Low-level glitch60
Cont’d on next page
## Espressif Systems18
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
Table 2-2 – cont’d from previous page
PinGlitch
## 1
## Typical Time Period (μs)
## GPIO18
Low-level glitch60
High-level glitch60
## GPIO19
Low-level glitch60
High-level glitch
## 2
## 60
## GPIO20
Pull-down glitch60
High-level glitch
## 2
## 60
## 1
Low-level glitch: the pin is at a low level output status during the time period;
High-level glitch: the pin is at a high level output status during the time period;
Pull-down glitch: the pin is at an internal weak pulled-down status during the time period;
Pull-up glitch: the pin is at an internal weak pulled-up status during the time period.
Please refer to Table5-4DC Characteristics (3.3 V, 25 °C)for detailed parameters about
low/high-level and pull-down/up.
## 2
GPIO19 and GPIO20 pins both have two high-level glitches during chip power-up, each
lasting for about 60μs. The total duration for the glitches and the delay are 3.2 ms and
2 ms respectively for GPIO19 and GPIO20.
## Espressif Systems19
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.3IO Pins
2.3.1IO MUX Functions
The IO MUX allows multiple input/output signals to be connected to a single input/output pin. Each IO pin of
ESP32-S3 can be connected to one of the five signals (IO MUX functions, i.e., F0-F4), as listed in Table2-4IO
MUX Functions.
Among the five sets of signals:
•Some are routed via the GPIO Matrix (GPIO0, GPIO1, etc.), which incorporates internal signal routing
circuitry for mapping signals programmatically. It gives the pin access to almost any peripheral signals.
However, the flexibility of programmatic mapping comes at a cost as it might affect the latency of routed
signals. For details about connecting to peripheral signals via GPIO Matrix, see
ESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO Matrix.
•Some are directly routed from certain peripherals (U0TXD, MTCK, etc.), including UART0/1, JTAG,
SPI0/1, and SPI2 - see Table2-3Peripheral Signals Routed via IO MUX.
Table 2-3. Peripheral Signals Routed via IO MUX
Pin FunctionSignalDescription
U...TXDTransmit data
UART0/1 interface
U...RXDReceive data
U...RTSRequest to send
U...CTSClear to send
MTCKTest clock
JTAG interface for debugging
MTDOTest Data Out
MTDITest Data In
MTMSTest Mode Select
SPIQMaster in, slave out
SPI0/1 interface (powered by VDD_SPI) for connection to in-package or
off-package flash/PSRAM via the SPI bus. It supports 1-, 2-, 4-line SPI
modes. See also Section2.6Pin Mapping Between Chip and
Flash/PSRAM
SPIDMaster out, slave in
SPIHDHold
SPIWPWrite protect
SPICLKClock
SPICS...Chip select
SPIIO...DataSPI0/1 interface (powered by VDD_SPI or VDD3P3_CPU) for the higher
4 bits data line interface and DQS interface in 8-line SPI modeSPIDQSData strobe/data mask
SPICLK_N_DIFFNegative clock signalDifferential clock negative/positive for the SPI bus
SPICLK_P_DIFFPositive clock signal
SUBSPIQMaster in, slave out
SPI0/1 interface (powered by VDD3P3_RTC or VDD3V3_CPU) for
connection to in-package oroff-package flash/PSRAM via the SUBSPI
bus. It supports 1-, 2-, 4-line SPI modes
SUBSPIDMaster out, slave in
SUBSPIHDHold
SUBSPIWPWrite protect
SUBSPICLKClock
SUBSPICS...Chip select
SUBSPICLK_N_DIFFNegative clock signalDifferential clock negative/positive for the SUBSPI bus
SUBSPICLK_P_DIFFPositive clock signal
Cont’d on next page
## Espressif Systems20
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
Table 2-3 – cont’d from previous page
Pin FunctionSignalDescription
FSPIQMaster in, slave out
SPI2 interface for fast SPI connection. It supports 1-, 2-, 4-line SPI
modes
FSPIDMaster out, slave in
FSPIHDHold
FSPIWPWrite protect
FSPICLKClock
FSPICS0Chip select
FSPIIO...DataThe higher 4 bits data line interface and DQS interface for SPI2 interface
in 8-line SPI modeFSPIDQSData strobe/data mask
CLK_OUT...Clock outputOutput clock signals generated by the chip’s internal components
Table2-4IO MUX Functionsshows the IO MUX functions of IO pins.
Table 2-4. IO MUX Functions
IO MUX Function
## 1, 2, 3
Pin No.GPIO
## 2
F0Type
## 3
F1TypeF2TypeF3TypeF4Type
## 5GPIO0GPIO0I/O/TGPIO0I/O/T
## 6GPIO1GPIO1I/O/TGPIO1I/O/T
## 7GPIO2GPIO2I/O/TGPIO2I/O/T
## 8GPIO3GPIO3I/O/TGPIO3I/O/T
## 9GPIO4GPIO4I/O/TGPIO4I/O/T
## 10GPIO5GPIO5I/O/TGPIO5I/O/T
## 11GPIO6GPIO6I/O/TGPIO6I/O/T
## 12GPIO7GPIO7I/O/TGPIO7I/O/T
## 13GPIO8GPIO8I/O/TGPIO8I/O/TSUBSPICS1O/T
## 14GPIO9GPIO9I/O/TGPIO9I/O/TSUBSPIHDI1/O/TFSPIHDI1/O/T
## 15GPIO10GPIO10I/O/TGPIO10I/O/TFSPIIO4I1/O/TSUBSPICS0O/TFSPICS0I1/O/T
## 16GPIO11GPIO11I/O/TGPIO11I/O/TFSPIIO5I1/O/TSUBSPIDI1/O/TFSPIDI1/O/T
## 17GPIO12GPIO12I/O/TGPIO12I/O/TFSPIIO6I1/O/TSUBSPICLKO/TFSPICLKI1/O/T
## 18GPIO13GPIO13I/O/TGPIO13I/O/TFSPIIO7I1/O/TSUBSPIQI1/O/TFSPIQI1/O/T
## 19GPIO14GPIO14I/O/TGPIO14I/O/TFSPIDQSO/TSUBSPIWPI1/O/TFSPIWPI1/O/T
## 21GPIO15GPIO15I/O/TGPIO15I/O/TU0RTSO
## 22GPIO16GPIO16I/O/TGPIO16I/O/TU0CTSI1
## 23GPIO17GPIO17I/O/TGPIO17I/O/TU1TXDO
## 24GPIO18GPIO18I/O/TGPIO18I/O/TU1RXDI1CLK_OUT3O
## 25GPIO19GPIO19I/O/TGPIO19I/O/TU1RTSOCLK_OUT2O
## 26GPIO20GPIO20I/O/TGPIO20I/O/TU1CTSI1CLK_OUT1O
## 27GPIO21GPIO21I/O/TGPIO21I/O/T
## 28GPIO26SPICS1O/TGPIO26I/O/T
## 30GPIO27SPIHDI1/O/TGPIO27I/O/T
## 31GPIO28SPIWPI1/O/TGPIO28I/O/T
## 32GPIO29SPICS0O/TGPIO29I/O/T
## 33GPIO30SPICLKO/TGPIO30I/O/T
## 34GPIO31SPIQI1/O/TGPIO31I/O/T
Cont’d on next page
## Espressif Systems21
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
Cont’d from previous page
IO MUX Function
## 1, 2, 3
Pin No.GPIO
## 2
F0Type
## 3
F1TypeF2TypeF3TypeF4Type
## 35GPIO32SPIDI1/O/TGPIO32I/O/T
## 36GPIO48SPICLK_N_DIFFO/TGPIO48I/O/TSUBSPICLK_N_DIFFO/T
## 37GPIO47SPICLK_P_DIFFO/TGPIO47I/O/TSUBSPICLK_P_DIFFO/T
## 38GPIO33GPIO33I/O/TGPIO33I/O/TFSPIHDI1/O/TSUBSPIHDI1/O/TSPIIO4I1/O/T
## 39GPIO34GPIO34I/O/TGPIO34I/O/TFSPICS0I1/O/TSUBSPICS0O/TSPIIO5I1/O/T
## 40GPIO35GPIO35I/O/TGPIO35I/O/TFSPIDI1/O/TSUBSPIDI1/O/TSPIIO6I1/O/T
## 41GPIO36GPIO36I/O/TGPIO36I/O/TFSPICLKI1/O/TSUBSPICLKO/TSPIIO7I1/O/T
## 42GPIO37GPIO37I/O/TGPIO37I/O/TFSPIQI1/O/TSUBSPIQI1/O/TSPIDQSI0/O/T
## 43GPIO38GPIO38I/O/TGPIO38I/O/TFSPIWPI1/O/TSUBSPIWPI1/O/T
## 44GPIO39MTCKI1GPIO39I/O/TCLK_OUT3OSUBSPICS1O/T
## 45GPIO40MTDOO/TGPIO40I/O/TCLK_OUT2O
## 47GPIO41MTDII1GPIO41I/O/TCLK_OUT1O
## 48GPIO42MTMSI1GPIO42I/O/T
## 49GPIO43U0TXDOGPIO43I/O/TCLK_OUT1O
## 50GPIO44U0RXDI1GPIO44I/O/TCLK_OUT2O
## 51GPIO45GPIO45I/O/TGPIO45I/O/T
## 52GPIO46GPIO46I/O/TGPIO46I/O/T
## 1
Boldmarks the default pin functions in the default boot mode. For more information about the boot modesee Section3.1Chip
## Boot Mode Control.
## 2
Regardinghighlightedcells, see Section2.3.4Restrictions for GPIOs and RTC_GPIOs.
## 3
Each IO MUX function (Fn,n= 0 ~ 4) is associated with atype. The description oftypeis as follows:
•I – input. O – output. T – high impedance.
•I1 – input; if the pin is assigned a function other than Fn, the input signal of Fnis always1.
•I0 – input; if the pin is assigned a function other than Fn, the input signal of Fnis always0.
## Espressif Systems22
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.3.2RTC Functions
When the chip is in Deep-sleep mode, the IO MUX described in Section2.3.1IO MUX Functionswill not work.
That is where the RTC IO MUX comes in. It allows multiple input/output signals to be a single input/output pin
in Deep-sleep mode, as the pin is connected to the RTC system and powered by VDD3P3_RTC.
RTC IO pins can be assigned toRTC functions. They can
•Either work as RTC GPIOs (RTC_GPIO0, RTC_GPIO1, etc.), connected to the ULP coprocessor
•Or connect to RTC peripheral signals (sar_i2c_scl_0, sar_i2c_sda_0, etc.) - see Table2-5RTC
Peripheral Signals Routed via RTC IO MUX
Table 2-5. RTC Peripheral Signals Routed via RTC IO MUX
Pin FunctionSignalDescription
sar_i2c_scl...Serial clock
RTC I2C0/1 interface
sar_i2c_sda...Serial data
Table2-6RTC Functionsshows the RTC functions of RTC IO pins.
Table 2-6. RTC Functions
PinRTCRTC Function
## 2
No.IO Name
## 1
## F0F1F2F3
5RTC_GPIO0RTC_GPIO0sar_i2c_scl_0
6RTC_GPIO1RTC_GPIO1sar_i2c_sda_0
7RTC_GPIO2RTC_GPIO2sar_i2c_scl_1
8RTC_GPIO3RTC_GPIO3sar_i2c_sda_1
## 9RTC_GPIO4RTC_GPIO4
## 10RTC_GPIO5RTC_GPIO5
## 11RTC_GPIO6RTC_GPIO6
## 12RTC_GPIO7RTC_GPIO7
## 13RTC_GPIO8RTC_GPIO8
## 14RTC_GPIO9RTC_GPIO9
## 15RTC_GPIO10RTC_GPIO10
## 16RTC_GPIO11RTC_GPIO11
## 17RTC_GPIO12RTC_GPIO12
## 18RTC_GPIO13RTC_GPIO13
## 19RTC_GPIO14RTC_GPIO14
## 21RTC_GPIO15RTC_GPIO15
## 22RTC_GPIO16RTC_GPIO16
## 23RTC_GPIO17RTC_GPIO17
## 24RTC_GPIO18RTC_GPIO18
## 25RTC_GPIO19RTC_GPIO19
## 26RTC_GPIO20RTC_GPIO20
## 27RTC_GPIO21RTC_GPIO21
## 1
This column lists the RTC GPIO names, since RTC functions are con-
figured with RTC GPIO registers that use RTC GPIO numbering.
## 2
Regardinghighlightedcells, see Section2.3.4RestrictionsforGPIOs
and RTC_GPIOs
## .
## Espressif Systems23
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.3.3Analog Functions
Some IO pins also haveanalog functions, for analog peripherals (such as ADC) in any power mode. Internal
analog signals are routed to these analog functions, see Table2-7Analog Signals Routed to Analog
## Functions.
Table 2-7. Analog Signals Routed to Analog Functions
Pin FunctionSignalDescription
TOUCH...Touch sensor channel ... signalTouch sensor interface
ADC..._CH...ADC1/2 channel ... signalADC1/2 interface
XTAL_32K_NNegative clock signal32 kHz external clock input/output
connected to ESP32-S3’s oscillatorXTAL_32K_PPositive clock signal
USB_D-Data -
USB OTG and USB Serial/JTAG function
USB_D+Data +
Table2-8Analog Functionsshows the analog functions of IO pins.
## Table 2-8. Analog Functions
## Analog Function
## 1, 2
Pin No.GPIO
## 3
## F0F1
## 6RTC_GPIO1TOUCH1ADC1_CH0
## 7RTC_GPIO2TOUCH2ADC1_CH1
## 8RTC_GPIO3TOUCH3ADC1_CH2
## 9RTC_GPIO4TOUCH4ADC1_CH3
## 10RTC_GPIO5TOUCH5ADC1_CH4
## 11RTC_GPIO6TOUCH6ADC1_CH5
## 12RTC_GPIO7TOUCH7ADC1_CH6
## 13RTC_GPIO8TOUCH8ADC1_CH7
## 14RTC_GPIO9TOUCH9ADC1_CH8
## 15RTC_GPIO10TOUCH10ADC1_CH9
## 16RTC_GPIO11TOUCH11ADC2_CH0
## 17RTC_GPIO12TOUCH12ADC2_CH1
## 18RTC_GPIO13TOUCH13ADC2_CH2
## 19RTC_GPIO14TOUCH14ADC2_CH3
## 21RTC_GPIO15XTAL_32K_PADC2_CH4
## 22RTC_GPIO16XTAL_32K_NADC2_CH5
## 23RTC_GPIO17ADC2_CH6
## 24RTC_GPIO18ADC2_CH7
## 25RTC_GPIO19USB_D-ADC2_CH8
## 26RTC_GPIO20USB_D+ADC2_CH9
## 1
Boldmarks the default pin functions in the default boot
mode. For more information about the boot modesee
## Section
3.1Chip Boot Mode Control.
## 2
This column lists the RTC GPIO names, since analog
functions are configured with RTC GPIO registers that
use RTC GPIO numbering.
## 3
Regardinghighlightedcells, see Section2.3.4Re-
strictions for GPIOs and RTC_GPIOs.
## Espressif Systems24
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.3.4Restrictions for GPIOs and RTC_GPIOs
All IO pins of ESP32-S3 have GPIO and some have RTC_GPIO pin functions. However, the IO pins are
multiplexed and can be configured for different purposes based on the requirements. Some IOs have
restrictions for usage. It is essential to consider the multiplexed nature and the limitations when using these IO
pins.
In tables of this chapter, some pin functions are inredoryellow. These functions indicate pins that require
extra caution when used asGPIO/GPIO:
•IO Pins– allocated for communication with in-package flash/PSRAM and NOT recommended for other
uses. For details, see Section2.6Pin Mapping Between Chip and Flash/PSRAM.
•IO Pins– have one of the following important functions:
–Strapping pins– need to be at certain logic levels at startup. See Section3Boot Configurations.
## Note:
Strapping pins are highlighted byPin Nameor configurationsAt Reset, instead of the pin functions.
–USB_D+/-– by default, connected to the USB Serial/JTAG Controller. To function as GPIOs, these
pins need to be reconfigured via the IO_MUX_MCU_SEL bit (see
ESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO Matrixfor details).
–JTAG interface– often used for debugging. See Table2-4IO MUX Functions. To free these pins
up, the pin functions USB_D+/- of the USB Serial/JTAG Controller can be used instead. See also
## Section
3.4JTAG Signal Source Control.
–UART0 interface– often used for debugging. See Table2-4IO MUX Functions.
–8-line SPI interface– no restrictions, unless the chip is connected to flash/PSRAM using 8-line SPI
mode.
For more information about assigning pins, please see Section2.3.5Peripheral Pin AssignmentandESP32-S3
## Consolidated Pin Overview
## .
## Espressif Systems25
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.3.5Peripheral Pin Assignment
Table2-9Peripheral Pin Assignmenthighlights which pins can be assigned to each peripheral interface
according to the following priorities:
•Priority 1 (P1): Fixed pins connected directly to peripheral signals via IO MUX or RTC IO MUX.
If a peripheral interface does not have priority 1 pins, such as UART2, it can be assigned to any GPIO pins
from priority 2 to priority 4.
•Any GPIO pins mapping to peripheral signals via GPIO Matrix, can be priority 2, 3, or 4.
–Priority 2 (P2): GPIO pins can be freely used without restrictions.
–Priority 3 (P3): GPIO pins should be used with caution, as they may conflict with the following
important functions described in Section2.3.4Restrictions for GPIOs and RTC_GPIOs:
*GPIO0, GPIO3, GPIO45, GPIO46: Strapping pins.
*GPIO19, GPIO20: USB Serial/JTAG interface.
*GPIO39, GPIO40, GPIO41, GPIO42: JTAG interface.
*GPIO43, GPIO44: UART0 interface.
*GPIO33, GPIO34, GPIO35, GPIO36, GPIO37: The higher 4 bits data line interface and DQS
interface for the SPI0/1 interface in 8-line SPI mode, and can be GPIO pins if the chip is not
connected to flash or PSRAM in 8-line SPI mode.
–Priority 4 (P4): GPIO pins already allocated or not recommended for use, as described in Section
2.3.4Restrictions for GPIOs and RTC_GPIOs:
*GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, GPIO31, GPIO32: SPI0/1 interface connected to
the in-package flash and PSRAM, or recommended for the off-package flash and PSRAM.
If a peripheral interface does not have priority 2 to 4 pins, such as USB Serial/JTAG, it means it can be
assigned only to priority 1 pins.
## Note:
•For details about which peripheral signals are connected to IO MUX or RTC IO MUX pins, please refer to Section
2.3.1IO MUX Functionsor Section2.3.2RTC Functions.
•For details about which peripheral signals can be assigned to GPIO pins, please refer to
ESP32-S3 Technical Reference Manual> Chapter IO MUX and GPIO Matrix > Section Peripheral Signal List.
## Espressif Systems26
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
## Table 2-9. Peripheral Pin Assignment
## Pin No.
## Pin Name
USB Serial/JTAG
Full-speed USB OTG
## JTAG
## ADC1
## ADC2
## Touch Sensor
## UART0
## UART1
SPI0/1 (recommended)
SPI0/1 (alternative)
SPI2 (recommended)
SPI2 (alternative)
## UART2
## I2C
## TWAI
## LED PWM
## I2S
LCD and Camera
## SPI3
## SD/MMC
## MCPWM
## RMT
## PCNT
## 1
## LNA_IN
## 2
## VDD3P3
## 3
## VDD3P3
## 4
## CHIP_PU
## 5
## GPIO0
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## GPIO0 (P3)
## 6
## GPIO1
## ADC1_CH0 (P1)
## TOUCH1 (P1)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## GPIO1 (P2)
## 7
## GPIO2
## ADC1_CH1 (P1)
## TOUCH2 (P1)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## GPIO2 (P2)
## 8
## GPIO3
## ADC1_CH2 (P1)
## TOUCH3 (P1)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## GPIO3 (P3)
## 9
## GPIO4
## ADC1_CH3 (P1)
## TOUCH4 (P1)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## GPIO4 (P2)
## 10
## GPIO5
## ADC1_CH4 (P1)
## TOUCH5 (P1)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## GPIO5 (P2)
## 11
## GPIO6
## ADC1_CH5 (P1)
## TOUCH6 (P1)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## GPIO6 (P2)
## 12
## GPIO7
## ADC1_CH6 (P1)
## TOUCH7 (P1)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## GPIO7 (P2)
## 13
## GPIO8
## ADC1_CH7 (P1)
## TOUCH8 (P1)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## SUBSPICS1 (P1)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## GPIO8 (P2)
## 14
## GPIO9
## ADC1_CH8 (P1)
## TOUCH9 (P1)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## SUBSPIHD (P1)
## FSPIHD (P1)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## GPIO9 (P2)
## 15
## GPIO10
## ADC1_CH9 (P1)
## TOUCH10 (P1)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## SUBSPICS0 (P1)
## FSPICS0 (P1)
## FSPIIO4 (P1)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## GPIO10 (P2)
## 16
## GPIO11
## ADC2_CH0 (P1)
## TOUCH11 (P1)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## SUBSPID (P1)
## FSPID (P1)
## FSPIIO5 (P1)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## GPIO11 (P2)
## 17
## GPIO12
## ADC2_CH1 (P1)
## TOUCH12 (P1)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## SUBSPICLK (P1)
## FSPICLK (P1)
## FSPIIO6 (P1)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## GPIO12 (P2)
## 18
## GPIO13
## ADC2_CH2 (P1)
## TOUCH13 (P1)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## SUBSPIQ (P1)
## FSPIQ (P1)
## FSPIIO7 (P1)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## GPIO13 (P2)
## 19
## GPIO14
## ADC2_CH3 (P1)
## TOUCH14 (P1)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## SUBSPIWP (P1)
## FSPIWP (P1)
## FSPIDQS (P1)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## GPIO14 (P2)
## 20
## VDD3P3_RTC
## 21
## XTAL_32K_P
## ADC2_CH4 (P1)
## U0RTS (P1)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## GPIO15 (P2)
## 22
## XTAL_32K_N
## ADC2_CH5 (P1)
## U0CTS (P1)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## GPIO16 (P2)
## 23
## GPIO17
## ADC2_CH6 (P1)
## GPIO17 (P2)
## U1TXD (P1)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## GPIO17 (P2)
## 24
## GPIO18
## ADC2_CH7 (P1)
## GPIO18 (P2)
## U1RXD (P1)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## GPIO18 (P2)
## 25
## GPIO19
## USB_D- (P1)
## USB_D- (P1)
## ADC2_CH8 (P1)
## GPIO19 (P3)
## U1RTS (P1)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## GPIO19 (P3)
## 26
## GPIO20
## USB_D+ (P1)
## USB_D+ (P1)
## ADC2_CH9 (P1)
## GPIO20 (P3)
## U1CTS (P1)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## GPIO20 (P3)
## 27
## GPIO21
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## GPIO21 (P2)
## 28
## SPICS1
## GPIO26 (P4)
## GPIO26 (P4)
## SPICS1 (P1)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## GPIO26 (P4)
## 29
## VDD_SPI
## 30
## SPIHD
## GPIO27 (P4)
## GPIO27 (P4)
## SPIHD (P1)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## GPIO27 (P4)
## 31
## SPIWP
## GPIO28 (P4)
## GPIO28 (P4)
## SPIWP (P1)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## GPIO28 (P4)
## 32
## SPICS0
## GPIO29 (P4)
## GPIO29 (P4)
## SPICS0 (P1)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## GPIO29 (P4)
## 33
## SPICLK
## GPIO30 (P4)
## GPIO30 (P4)
## SPICLK (P1)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## GPIO30 (P4)
## 34
## SPIQ
## GPIO31 (P4)
## GPIO31 (P4)
## SPIQ (P1)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## GPIO31 (P4)
## 35
## SPID
## GPIO32 (P4)
## GPIO32 (P4)
## SPID (P1)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## GPIO32 (P4)
## 36
## SPICLK_N
## GPIO48 (P2)
## GPIO48 (P2)
## SPICLK_N_DIFF (P1)
## SUBSPICLK_N_DIFF (P1)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## GPIO48 (P2)
## 37
## SPICLK_P
## GPIO47 (P2)
## GPIO47 (P2)
## SPICLK_P_DIFF (P1)
## SUBSPICLK_P_DIFF (P1)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## GPIO47 (P2)
## 38
## GPIO33
## GPIO33 (P3)
## GPIO33 (P3)
## SPIIO4 (P1)
## SUBSPIHD (P1)
## GPIO33 (P3)
## FSPIHD (P1)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## GPIO33 (P3)
## 39
## GPIO34
## GPIO34 (P3)
## GPIO34 (P3)
## SPIIO5 (P1)
## SUBSPICS0 (P1)
## GPIO34 (P3)
## FSPICS0 (P1)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## GPIO34 (P3)
## 40
## GPIO35
## GPIO35 (P3)
## GPIO35 (P3)
## SPIIO6 (P1)
## SUBSPID (P1)
## GPIO35 (P3)
## FSPID (P1)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## GPIO35 (P3)
## 41
## GPIO36
## GPIO36 (P3)
## GPIO36 (P3)
## SPIIO7 (P1)
## SUBSPICLK (P1)
## GPIO36 (P3)
## FSPICLK (P1)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## GPIO36 (P3)
## 42
## GPIO37
## GPIO37 (P3)
## GPIO37 (P3)
## SPIDQS (P1)
## SUBSPIQ (P1)
## GPIO37 (P3)
## FSPIQ (P1)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## GPIO37 (P3)
## 43
## GPIO38
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## SUBSPIWP (P1)
## GPIO38 (P2)
## FSPIWP (P1)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## GPIO38 (P2)
## 44
## MTCK
## MTCK (P1)
## MTCK (P1)
## MTCK (P1)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## SUBSPICS1 (P1)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## GPIO39 (P3)
## 45
## MTDO
## MTDO (P1)
## MTDO (P1)
## MTDO (P1)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## GPIO40 (P3)
## 46
## VDD3P3_CPU
## 47
## MTDI
## MTDI (P1)
## MTDI (P1)
## MTDI (P1)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## GPIO41 (P3)
## 48
## MTMS
## MTMS (P1)
## MTMS (P1)
## MTMS (P1)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## GPIO42 (P3)
## 49
## U0TXD
## U0TXD (P1)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## GPIO43 (P3)
## 50
## U0RXD
## U0RXD (P1)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## GPIO44 (P3)
## 51
## GPIO45
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## GPIO45 (P3)
## 52
## GPIO46
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## GPIO46 (P3)
## 53
## XTAL_N
## 54
## XTAL_P
## 55
## VDDA
## 56
## VDDA
## 57
## GND
## 1
For USB Serial/JTAG and USB OTG, use USB_D- and USB_D+ when on internal PHY, and the USB_D- and USB_D+ can be swapped by configuring the USB_SERIAL_JTAG_EXCHG_PINS bit according to
ESP32-S3 Technical Reference Manual
; use
other fixed pins
when on external PHY. For how to select PHY, see
ESP32-S3 Technical Reference Manual
> USB Serial/JTAG Controller > Internal/External
PHY Selection.
## 2
Signals of UART0, UART1, SPI0/1, and SPI2 interfaces can be mapped to any GPIO pins through the GPIO Matrix, regardless of whether they are directly routed to
fixed pins
via IO MUX.
## Espressif Systems27
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.4Analog Pins
## Table 2-10. Analog Pins
PinPinPinPin
No.NameTypeFunction
1LNA_INI/OLow Noise Amplifier (RF LNA) input/output signals
## 4CHIP_PUI
High: on, enables the chip (powered up).
Low: off, disables the chip (powered down).
Note: Do not leave the CHIP_PU pin floating.
53XTAL_N—External clock input/output connected to chip’s crystal or oscillator.
P/N means differential clock positive/negative.54XTAL_P—
## Espressif Systems28
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.5Power Supply
2.5.1Power Pins
The chip is powered via the power pins described in Table2-11Power Pins.
## Table 2-11. Power Pins
## Power Supply
## 1, 2
Pin No.Pin NameDirectionPower Domain/OtherIO Pins
## 5
2VDD3P3InputAnalog power domain
3VDD3P3InputAnalog power domain
20VDD3P3_RTCInputRTC and part of Digital power domainsRTC IO
## 29VDD_SPI
## 3,4
InputIn-package memory (backup power line)
OutputIn-package and off-package flash/PSRAMSPI IO
46VDD3P3_CPUInputDigital power domainDigital IO
55VDDAInputAnalog power domain
56VDDAInputAnalog power domain
57GND–External ground connection
## 1
See in conjunction with Section2.5.2Power Scheme.
## 2
For recommended and maximum voltage and current, see Section5.1Absolute Maximum
Ratingsand Section5.2Recommended Operating Conditions.
## 3
To configure VDD_SPI as input or output, seeESP32-S3 Technical Reference Manual> Chap-
terLow-power Management.
## 4
To configure output voltage, see Section3.2VDD_SPI Voltage Controland Section5.3
VDD_SPI Output Characteristics.
## 5
RTC IO pins are those powered by VDD3P3_RTC and so on, as shown in Figure2-2ESP32-S3
## Power Scheme
. See also Table2-1Pin Overview> ColumnPin Providing Power.
2.5.2Power Scheme
The power scheme is shown in Figure2-2ESP32-S3 Power Scheme.
The components on the chip are powered via voltage regulators.
## Table 2-12. Voltage Regulators
Voltage RegulatorOutputPower Supply
Digital1.1 VDigital power domain
Low-power1.1 VRTC power domain
## Flash1.8 V
Can be configured to power
in-package flash/PSRAM or
off-package memory
## Espressif Systems29
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
Figure 2-2. ESP32-S3 Power Scheme
2.5.3Chip Power-up and Reset
Once the power is supplied to the chip, its power rails need a short time to stabilize. After that, CHIP_PU – the
pin used for power-up and reset – is pulled high to activate the chip. For information on CHIP_PU as well as
power-up and reset timing, see Figure
## 2-3and Table2-13.
## V
IL_nRST
t
## STBL
t
## RST
## 2.8 V
## VDDA,
## VDD3P3,
## VDD3P3_RTC,
## VDD3P3_CPU
## CHIP_PU
Figure 2-3. Visualization of Timing Parameters for Power-up and Reset
Table 2-13. Description of Timing Parameters for Power-up and Reset
ParameterDescriptionMin (μs)
t
## ST BL
Time  reserved  for  the  power  rails  of  VDDA,  VDD3P3,
VDD3P3_RTC, and VDD3P3_CPU to stabilize before the CHIP_PU
pin is pulled high to activate the chip
## 50
t
## RST
Time reserved for CHIP_PU to stay below V
IL_nRST
to reset the
chip (see Table
## 5-4)
## 50
## Espressif Systems30
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 2  Pins
2.6Pin Mapping Between Chip and Flash/PSRAM
Table2-14lists the pin mapping between the chip and flash/PSRAM for all SPI modes.
For chip variants with in-package flash/PSRAM (see Table1-1ESP32-S3 Series Comparison), the pins allocated
for communication with in-package flash/PSRAM can be identified depending on the SPI mode used.
For off-package flash/PSRAM, these are the recommended pin mappings.
For more information on SPI controllers, see also Section4.2.1.5Serial Peripheral Interface (SPI).
Notice:Do not use the pins connected to in-package flash/PSRAM for any other purposes.
Table 2-14. Pin Mapping Between Chip and Flash or PSRAM
Single SPIDual SPIQuad SPI/QPIOctal SPI/OPI
Pin No.Pin NameFlashPSRAMFlashPSRAMFlashPSRAMFlashPSRAM
## 28SPICS1
## 2
## CE#CE#CE#CE#
## 30SPIHDHOLD#SIO3HOLD#SIO3HOLD#SIO3DQ3DQ3
## 31SPIWPWP#SIO2WP#SIO2WP#SIO2DQ2DQ2
## 32SPICS0
## 1
## CS#CS#CS#CS#
## 33SPICLKCLKCLKCLKCLKCLKCLKCLKCLK
## 34SPIQDOSO/SIO1DOSO/SIO1DOSO/SIO1DQ1DQ1
## 35SPIDDISI/SIO0DISI/SIO0DISI/SIO0DQ0DQ0
## 38GPIO33DQ4DQ4
## 39GPIO34DQ5DQ5
## 40GPIO35DQ6DQ6
## 41GPIO36DQ7DQ7
## 42GPIO37DQS/DMDQS/DM
## 1
CS0 is for in-package flash
## 2
CS1 is for in-package PSRAM
## Espressif Systems31
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 3  Boot Configurations
3Boot Configurations
The chip allows for configuring the following boot parameters throughstrapping pinsandeFuse parametersat
power-up or a hardware reset, without microcontroller interaction.
•Chip boot mode
–Strapping pin: GPIO0 and GPIO46
•VDD_SPI voltage
–Strapping pin: GPIO45
–eFuse parameter: EFUSE_VDD_SPI_FORCE and EFUSE_VDD_SPI_TIEH
•ROM message printing
–Strapping pin: GPIO46
–eFuse parameter: EFUSE_UART_PRINT_CONTROL and
## EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT
•JTAG signal source
–Strapping pin: GPIO3
–eFuse parameter: EFUSE_DIS_PAD_JTAG, EFUSE_DIS_USB_JTAG, and EFUSE_STRAP_JTAG_SEL
The default values of all the above eFuse parameters are 0, which means that they are not burnt. Given that
eFuse is one-time programmable, once programmed to 1, it can never be reverted to 0. For how to program
eFuse parameters, please refer to
ESP32-S3 Technical Reference Manual> ChaptereFuse Controller.
The default values of the strapping pins, namely the logic levels, are determined by pins’ internal weak
pull-up/pull-down resistors at reset if the pins are not connected to any circuit, or connected to an external
high-impedance circuit.
Table 3-1. Default Configuration of Strapping Pins
Strapping PinDefault ConfigurationBit Value
GPIO0Weak pull-up1
GPIO3Floating–
GPIO45Weak pull-down0
GPIO46Weak pull-down0
To change the bit values, the strapping pins should be connected to external pull-down/pull-up resistances. If
the ESP32-S3 is used as a device by a host MCU, the strapping pin voltage levels can also be controlled by
the host MCU.
All strapping pins have latches. At Chip Reset, the latches sample the bit values of their respective strapping
pins and store them until the chip is powered down or shut down. The states of latches cannot be changed in
any other way. It makes the strapping pin values available during the entire chip operation, and the pins are
freed up to be used as regular IO pins after reset. For details on Chip Reset, see
ESP32-S3 Technical Reference Manual> ChapterReset and Clock.
## Espressif Systems32
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 3  Boot Configurations
The timing of signals connected to the strapping pins should adhere to thesetup timeandhold time
specifications in Table3-2and Figure3-1.
Table 3-2. Description of Timing Parameters for the Strapping Pins
ParameterDescriptionMin (ms)
t
## SU
Setup timeis the time reserved for the power rails to stabilize be-
fore the CHIP_PU pin is pulled high to activate the chip.
## 0
t
## H
Hold timeis the time reserved for the chip to read the strapping
pin values after CHIP_PU is already high and before these pins
start operating as regular IO pins.
## 3
Strapping pin
## V
IH_nRST
## V
## IH
t
## SU
t
## H
## CHIP_PU
Figure 3-1. Visualization of Timing Parameters for the Strapping Pins
3.1Chip Boot Mode Control
GPIO0 and GPIO46 control the boot mode after the reset is released. See Table3-3Chip Boot Mode
## Control.
## Table 3-3. Chip Boot Mode Control
Boot ModeGPIO0GPIO46
SPI boot mode1Any value
Joint download boot mode
## 2
## 00
## 1
Boldmarks the default value and configuration.
## 2
Joint Download Boot mode supports the following
download methods:
•USB Download Boot:
–USB-Serial-JTAG Download Boot
–USB-OTG Download Boot
•UART Download Boot
In addition to SPI Boot and Joint Download Boot modes, ESP32-S3 also supports SPI Download Boot mode.
## Espressif Systems33
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 3  Boot Configurations
For details, please seeESP32-S3 Technical Reference Manual> ChapterChip Boot Control.
3.2VDD_SPI Voltage Control
The required VDD_SPI voltage for the chips of the ESP32-S3 Series can be found in Table1-1ESP32-S3 Series
## Comparison.
The VDD_SPI voltage can be:
•(Default) 3.3 V supplied by VDD3P3_RTC via R
## SP I
•1.8V supplied by the Flash Voltage Regulator
The voltage is determined by EFUSE_VDD_SPI_FORCE, GPIO45, and EFUSE_VDD_SPI_TIEH.
Table 3-4. VDD_SPI Voltage Control
VDD_SPI power source
## 2
VoltageEFUSE_VDD_SPI_FORCEGPIO45EFUSE_VDD_SPI_TIEH
VDD3P3_RTC via R
## SP I
## 3.3 V
00Ignored
1Ignored1
## Flash Voltage Regulator1.8 V
01Ignored
1Ignored0
## 1
Boldmarks the default value and configuration.
## 2
See Section2.5.2Power Scheme.
3.3ROM Messages Printing Control
During the boot process, the messages by the ROM code can be printed to:
•(Default) UART0 and USB Serial/JTAG controller
•USB Serial/JTAG controller
## •UART0
The ROM messages printing to UART or USB Serial/JTAG controller can be respectively disabled by configuring
registers and eFuse. For detailed information, please refer toESP32-S3 Technical Reference Manual>
ChapterChip Boot Control.
3.4JTAG Signal Source Control
The strapping pin GPIO3 can be used to control the source of JTAG signals during the early boot process. This
pin does not have any internal pull resistors and the strapping value must be controlled by the external circuit
that cannot be in a high impedance state.
## As Table
3-5JTAG Signal Source Controlshows, GPIO3 is used in combination with EFUSE_DIS_PAD_JTAG,
EFUSE_DIS_USB_JTAG, and EFUSE_STRAP_JTAG_SEL.
## Espressif Systems34
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 3  Boot Configurations
Table 3-5. JTAG Signal Source Control
JTAG Signal SourceEFUSE_DIS_PAD_JTAGEFUSE_DIS_USB_JTAGEFUSE_STRAP_JTAG_SELGPIO3
USB Serieal/JTAG Controller
000Ignored
## 0011
10IgnoredIgnored
JTAG pins
## 2
## 0010
01IgnoredIgnored
JTAG is disabled11IgnoredIgnored
## 1
Boldmarks the default value and configuration.
## 2
JTAG pins refer to MTDI, MTCK, MTMS, and MTDO.
## Espressif Systems35
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4Functional Description
4.1System
This section describes the core of the chip’s operation, covering its microprocessor, memory organization,
system components, and security features.
4.1.1Microprocessor and Master
This subsection describes the core processing units within the chip and their capabilities.
## 4.1.1.1CPU
ESP32-S3 has a low-power Xtensa
## ®
dual-core 32-bit LX7 microprocessor.
## Feature List
•Five-stage pipeline that supports the clock frequency of up to 240 MHz
•16-bit/24-bit instruction set providing high code density
•32-bit customized instruction set and 128-bit data bus that provide high computing performance
•Support for single-precision floating-point unit (FPU)
•32-bit multiplier and 32-bit divider
•Unbuffered GPIO instructions
•32 interrupts at six levels
•Windowed ABI with 64 physical general registers
•Trace function with TRAX compressor, up to 16 KB trace memory
•JTAG for debugging
For information about the Xtensa
## ®
Instruction Set Architecture, please refer to
## Xtensa
## ®
Instruction Set Architecture (ISA) Summary.
4.1.1.2Processor Instruction Extensions (PIE)
ESP32-S3 contains a series of new extended instruction set in order to improve the operation efficiency of
specific AI and DSP (Digital Signal Processing) algorithms.
## Feature List
•128-bit new general-purpose registers
•128-bit vector operations, e.g., complex multiplication, addition, subtraction, multiplication, shifting,
comparison, etc
•Data handling instructions and load/store operation instructions combined
•Non-aligned 128-bit vector data
## Espressif Systems36
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Saturation operation
For details, seeESP32-S3 Technical Reference Manual> ChapterProcessor Instruction Extensions.
4.1.1.3Ultra-Low-Power Coprocessor (ULP)
The ULP coprocessor is designed as a simplified, low-power replacement of CPU in sleep modes. It can be
also used to supplement the functions of the CPU in normal working mode. The ULP coprocessor and RTC
memory remain powered up during the Deep-sleep mode. Hence, the developer can store a program for the
ULP coprocessor in the RTC slow memory to access RTC GPIO, RTC peripheral devices, RTC timers and
internal sensors in Deep-sleep mode.
ESP32-S3 has two ULP coprocessors, one based on RISC-V instruction set architecture (ULP-RISC-V) and the
other on finite state machine (ULP-FSM). The clock of the coprocessors is the internal fast RC oscillator.
## Feature List
## •ULP-RISC-V:
–Support forRV32IMCinstruction set
–Thirty-two 32-bit general-purpose registers
–32-bit multiplier and divider
–Support for interrupts
–Booted by the CPU, its dedicated timer, or RTC GPIO
## •ULP-FSM:
–Support for common instructions including arithmetic, jump, and program control instructions
–Support for on-board sensor measurement instructions
–Booted by the CPU, its dedicated timer, or RTC GPIO
## Note:
Note that these two coprocessors cannot work simultaneously.
For details, seeESP32-S3 Technical Reference Manual> ChapterULP Coprocessor.
4.1.1.4GDMA Controller (GDMA)
ESP32-S3 has a general-purpose DMA controller (GDMA) with five independent channels for transmitting and
another five independent channels for receiving. These ten channels are shared by peripherals that have DMA
feature, and support dynamic priority.
The GDMA controller controls data transfer using linked lists. It allows peripheral-to-memory and
memory-to-memory data transfer at a high speed. All channels can access internal and external RAM.
The ten peripherals on ESP32-S3 with DMA feature are SPI2, SPI3, UHCI0, I2S0, I2S1, LCD/CAM, AES, SHA,
ADC, and RMT.
For details, see
ESP32-S3 Technical Reference Manual> ChapterGDMA Controller.
## Espressif Systems37
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.1.2Memory Organization
This subsection describes the memory arrangement to explain how data is stored, accessed, and managed
for efficient operation.
Figure4-1illustrates the address mapping structure of ESP32-S3.
## CPU
## 0x0000_0000
0x3BFF_FFFF
0x3C00_0000
0x3DFF_FFFF
0x3E00_0000
0x3FC8_7FFF
0x3FC8_8000
0x3FCF_FFFF
0x3FD0_0000
0x3FEF_FFFF
0x3FF0_0000
0x3FF1_FFFF
0x3FF2_0000
0x3FFF_FFFF
## 0x4000_0000
0x4005_FFFF
## 0x4006_0000
0x4036_FFFF
## 0x4037_0000
0x403D_FFFF
0x403E_0000
0x41FF_FFFF
## 0x4200_0000
0x43FF_FFFF
## 0x4400_0000
0x4FFF_FFFF
## 0x5000_0000
0x5000_1FFF
## 0x5000_2000
0x5FFF_FFFF
## 0x6000_0000
0x600D_0FFF
0x600F_E000
0x600F_FFFF
0x600D_1000
0x600F_DFFF
Not available for use
Available for use
## Cache
MMUExternal Memory
## SRAMROM
## GDMA
## RTC
## Fast Memory
## RTC
## Slow Memory
## 0x6010_0000
0xFFFF_FFFF
## Reserved
## 32 MB
External memory
## Reserved
## 480 KB
Internal memory
## Reserved
## 128 KB
Internal memory
## Reserved
## 384 KB
Internal memory
## Reserved
## 448 KB
Internal memory
## Reserved
## 32 MB
External memory
## Reserved
## 8 KB
Internal memory
## Reserved
## 836 KB
## Peripherals
## 8 KB
Internal memory
## Reserved
## Reserved
Data bus
Data bus
Data bus
Instruction bus
Instruction bus
Instruction bus
Data/Instruction bus
Data/Instruction bus
## ★
★Accessible by ULP co-processor
RTC Peripherals
## Other Peripherals
## ★
## Figure 4-1. Address Mapping Structure
4.1.2.1Internal Memory
The internal memory of ESP32-S3 refers to the memory integrated on the chip die or in the chip package,
including ROM, SRAM, eFuse, and flash.
## Feature List
•384 KB ROM: for booting and core functions
•512 KB on-chip SRAM: for data and instructions, running at a configurable frequency of up to 240 MHz
•RTC FAST memory: 8 KB SRAM that supports read/write/instruction fetch by the main CPU (LX7
dual-core processor). It can retain data in Deep-sleep mode
## Espressif Systems38
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•RTC SLOW Memory: 8 KB SRAM that supports read/write/instruction fetch by the main CPU (LX7
dual-core processor) or coprocessors. It can retain data in Deep-sleep mode
•4096-biteFusememory: 1792 bits are available for users, such as encryption key and device ID. See
also Section4.1.2.4eFuse Controller
•In-package flash and PSRAM:
–See flash and PSRAM size in Chapter1ESP32-S3 Series Comparison
–For specifications, refer to Section5.7Memory Specifications.
For details, seeESP32-S3 Technical Reference Manual> ChapterSystem and Memory.
4.1.2.2External Flash and RAM
ESP32-S3 supports SPI, Dual SPI, Quad SPI, Octal SPI, QPI, and OPI interfaces that allow connection to
multiple external flash and RAM.
The external flash and RAM can be mapped into the CPU instruction memory space and read-only data
memory space. The external RAM can also be mapped into the CPU data memory space. ESP32-S3 supports
up to 1 GB of external flash and RAM, and hardware encryption/decryption based on XTS-AES to protect users’
programs and data in flash and external RAM.
Through high-speed caches, ESP32-S3 can support at a time up to:
•External flash or RAM mapped into 32 MB instruction space as individual blocks of 64 KB
•External RAM mapped into 32 MB data space as individual blocks of 64 KB. 8-bit, 16-bit, 32-bit, and
128-bit reads and writes are supported. External flash can also be mapped into 32 MB data space as
individual blocks of 64 KB, but only supporting 8-bit, 16-bit, 32-bit and 128-bit reads.
## Note:
After ESP32-S3 is initialized, firmware can customize the mapping of external RAM or flash into the CPU address space.
For details, seeESP32-S3 Technical Reference Manual> ChapterSystem and Memory.
4.1.2.3Cache
ESP32-S3 has an instruction cache and a data cache shared by the two CPU cores. Each cache can be
partitioned into multiple banks.
## Feature List
•Instruction cache: 16 KB (one bank) or 32 KB (two banks)
Data cache: 32 KB (one bank) or 64 KB (two banks)
•Instruction cache: four-way or eight-way set associative
Data cache: four-way set associative
•Block size of 16 bytes or 32 bytes for both instruction cache and data cache
•Pre-load function
•Lock function
## Espressif Systems39
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Critical word first and early restart
For details, seeESP32-S3 Technical Reference Manual> ChapterSystem and Memory.
4.1.2.4eFuse Controller
ESP32-S3 contains a 4-Kbit eFuse to store parameters, which are burned and read by an eFuse
controller.
## Feature List
•4 Kbits in total, with 1792 bits reserved for users, e.g., encryption key and device ID
•One-time programmable storage
•Configurable write protection
•Configurable read protection
•Various hardware encoding schemes to protect against data corruption
For details, seeESP32-S3 Technical Reference Manual> ChaptereFuse Controller.
4.1.3System Components
This subsection describes the essential components that contribute to the overall functionality and control of
the system.
4.1.3.1IO MUX and GPIO Matrix
The IO MUX and GPIO Matrix in the ESP32-S3 chip provide flexible routing of peripheral input and output
signals to the GPIO pins. These peripherals enhance the functionality and performance of the chip by allowing
the configuration of I/O, support for multiplexing, and signal synchronization for peripheral inputs.
## Feature List
•GPIO Matrix:
–A full-switching matrix between the peripheral input/output signals and the GPIO pins
–175 digital peripheral input signals can be sourced from the input of any GPIO pins
–The output of any GPIO pins can be from any of the 184 digital peripheral output signals
–Supports signal synchronization for peripheral inputs based on APB clock bus
–Provides input signal filter
–Supports sigma delta modulated output
–Supports GPIO simple input and output
## •IO MUX:
–Provides one configuration register IO_MUX_GPIOn_REG for each GPIO pin. The pin can be
configured to
*perform GPIO function routed by GPIO matrix
## Espressif Systems40
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
*or perform direct connection bypassing GPIO matrix
–Supports some high-speed digital signals (SPI, JTAG, UART) bypassing GPIO matrix for better
high-frequency digital performance (IO MUX is used to connect these pins directly to peripherals)
## •RTC IO MUX:
–Controls low power feature of 22 RTC GPIO pins
–Controls analog functions of 22 RTC GPIO pins
–Redirects 22 RTC input/output signals to RTC system
For details, seeESP32-S3 Technical Reference Manual> ChapterIO MUX and GPIO Matrix.
4.1.3.2Reset
ESP32-S3 provides four reset levels, namely CPU Reset, Core Reset, System Reset, and Chip Reset.
## Feature List
•Support four reset levels:
–CPU Reset: only resets CPUxcore. CPUxcan be CPU0 or CPU1 here. Once such reset is released,
programs will be executed from CPUxreset vector. Each CPU core has its own reset logic. If CPU
Reset is from CPU0, the
sensitiveregisterswill be reset, too.
–Core Reset: resets the whole digital system except RTC, including CPU0, CPU1, peripherals, Wi-Fi,
## Bluetooth
## ®
LE (BLE), and digital GPIOs.
–System Reset: resets the whole digital system, including RTC.
–Chip Reset: resets the whole chip.
•Support software reset and hardware reset:
–Software reset is triggered by CPUxconfiguring its corresponding registers. Refer to
ESP32-S3 Technical Reference Manual> ChapterLow-power Managementfor more details.
–Hardware reset is directly triggered by the circuit.
For details, seeESP32-S3 Technical Reference Manual> ChapterReset and Clock.
4.1.3.3Clock
CPU Clock
The CPU clock has three possible sources:
•External main crystal clock
•Internal fast RC oscillator (typically about 17.5 MHz, adjustable)
•PLL clock
The application can select the clock source from the three clocks above. The selected clock source drives
the CPU clock directly, or after division, depending on the application. Once the CPU is reset, the default
clock source would be the external main crystal clock divided by 2.
## Espressif Systems41
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
## Note:
ESP32-S3 is unable to operate without an external main crystal clock.
RTC Clock
The RTC slow clock is used for RTC counter, RTC watchdog and low-power controller. It has three possible
sources:
•External low-speed (32 kHz) crystal clock
•Internal slow RC oscillator (typically about 136 kHz, adjustable)
•Internal fast RC oscillator divided clock (derived from the internal fast RC oscillator divided by 256)
The RTC fast clock is used for RTC peripherals and sensor controllers. It has two possible sources:
•External main crystal clock divided by 2
•Internal fast RC oscillator (typically about 17.5 MHz, adjustable)
For details, seeESP32-S3 Technical Reference Manual> ChapterReset and Clock.
4.1.3.4Interrupt Matrix
The interrupt matrix embedded in ESP32-S3 independently allocates peripheral interrupt sources to the two
CPUs’ peripheral interrupts, to timely inform CPU0 or CPU1 to process the interrupts once the interrupt signals
are generated.
## Feature List
•99 peripheral interrupt sources as input
•Generate 26 peripheral interrupts to CPU0 and 26 peripheral interrupts to CPU1 as output.
Note that the remaining six CPU0 interrupts and six CPU1 interrupts are internal interrupts.
•Disable CPU non-maskable interrupt (NMI) sources
•Query current interrupt status of peripheral interrupt sources
For details, seeESP32-S3 Technical Reference Manual> ChapterInterrupt Matrix.
4.1.3.5Power Management Unit (PMU)
ESP32-S3 has an advanced Power Management Unit (PMU). It can be flexibly configured to power up
different power domains of the chip to achieve the best balance between chip performance, power
consumption, and wakeup latency.
The integrated Ultra-Low-Power (ULP) coprocessors allow ESP32-S3 to operate in Deep-sleep mode with
most of the power domains turned off, thus achieving extremely low-power consumption.
Configuring the PMU is a complex procedure. To simplify power management for typical scenarios, there are
the followingpredefined power modesthat power up different combinations of power domains:
•Active mode– The CPU, RF circuits, and all peripherals are on. The chip can process data, receive,
transmit, and listen.
## Espressif Systems42
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Modem-sleep mode– The CPU is on, but the clock frequency can be reduced. The wireless
connections can be configured to remain active as RF circuits are periodically switched on when
required.
•Light-sleep mode– The CPU stops running, and can be optionally powered on. The RTC peripherals, as
well as the ULP coprocessor can be woken up periodically by the timer. The chip can be woken up via
all wake up mechanisms: MAC, RTC timer, or external interrupts. Wireless connections can remain active.
Some groups of digital peripherals can be optionally powered off.
•Deep-sleep mode– Only RTC is powered on. Wireless connection data is stored in RTC memory.
For power consumption in different power modes, see Section5.6Current Consumption.
Figure4-2Components and Power Domainsand the following Table4-1show the distribution of chip
components betweenpower domainsandpower subdomains.
## Wireless Digital Circuits
Wi-Fi MAC
Wi-Fi
## Baseband
Bluetooth LE Link
## Controller
Bluetooth LE
## Baseband
## Digital Power Domain
Espressif’s ESP32-S3 Wi-Fi + Bluetooth
## ®
Low Energy SoC
## ROMSRAM
2.4 GHz Balun
## + Switch
2.4 GHz
## Receiver
2.4 GHz
## Transmitter
## RF
## Synthesizer
RF Circuits
## Phase Lock
## Loop
## PLL
## XTAL_CLK
## External Main
## Clock
## RC_FAST_CLK
Fast RC
## Oscillator
## Analog Power Domain
## Flash
## Encryption
## RNG
USB Serial/
## JTAG
## GPIO
## UART
## TWAI
## ®
## General-
purpose
## Timers
## I2S
## I2C
## Pulse
## Counter
## LED PWM
## Camera
## Interface
## SPI0/1
## RMT
## DIG ADC
## System
## Timer
## LCD
## Interface
## Main System
## Watchdog
## Timers
## MCPWM
RTC Memory
## RTC
## Watchdog
## Timer
## PMU
RTC Power Domain
## RTC GPIO
## Temperature
## Sensor
## Touch
## Sensor
## ULP
## Coprocessor
## RTC ADC
Optional RTC Peripherals
## RTC I2C
eFuse
## Controller
Power distribution
Power domain
Power subdomain
## Super
## Watchdog
## CPU
## Xtensa
## ®
## Dual-
core 32-bit LX7
## Microprocessor
## JTAG
## Cache
## Interrupt
## Matrix
## World
## Controller
## Optional Digital Peripherals
## RSARSA_DSSHA
## AES
## HMAC
Secure BootSPI2/3GDMA
## SD/MMC
## Host
## USB OTG
Figure 4-2. Components and Power Domains
## Espressif Systems43
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
Table 4-1. Components and Power Domains
RTCDigitalAnalog
## Power
## Mode
## Power
## Domain
## Optional
## RTC
## Periph
## CPU
## Optional
## Digital
## Periph
## Wireless
## Digital
## Circuits
## RC_
## FAST_
## CLK
## XTAL_
## CLK
## PLL
## RF
## Circuits
ActiveONONONONONONONONONONON
Modem-sleepONONONONONON
## 1
## ONONONONOFF
## 2
Light-sleepONONONOFF
## 1
## ON
## 1
## OFF
## 1
## ONOFFOFFOFFOFF
## 2
Deep-sleepONON
## 1
## OFFOFFOFFOFFONOFFOFFOFFOFF
## 1
Configurable. SeeESP32-S3 Technical Reference Manual> ChapterLow-power Managementfor more details.
## 2
If Wireless Digital Circuits are on, RF circuits are periodically switched on when required by internal operation to keep
active wireless connections running.
For details, seeESP32-S3 Technical Reference Manual> ChapterLow Power Management.
4.1.3.6System Timer
ESP32-S3 integrates a 52-bit system timer, which has two 52-bit counters and three comparators.
## Feature List
•Counters with a clock frequency of 16 MHz
•Three types of independent interrupts generated according to alarm value
•Two alarm modes: target mode and period mode
•52-bit target alarm value and 26-bit periodic alarm value
•Read sleep time from RTC timer when the chip is awaken from Deep-sleep or Light-sleep mode
•Counters can be stalled if the CPU is stalled or in OCD mode
For details, seeESP32-S3 Technical Reference Manual> ChapterSystem Timer.
4.1.3.7General Purpose Timers
ESP32-S3 is embedded with four 54-bit general-purpose timers, which are based on 16-bit prescalers and
54-bit auto-reload-capable up/down-timers.
## Feature List
•16-bit clock prescaler, from 2 to 65536
•54-bit time-base counter programmable to be incrementing or decrementing
•Able to read real-time value of the time-base counter
•Halting and resuming the time-base counter
•Programmable alarm generation
•Timer value reload (Auto-reload at alarm or software-controlled instant reload)
## Espressif Systems44
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Level interrupt generation
For details, seeESP32-S3 Technical Reference Manual> ChapterTimer Group.
4.1.3.8Watchdog Timers
ESP32-S3 contains three watchdog timers: one in each of the two timer groups (called Main System
Watchdog Timers, or MWDT) and one in the RTC Module (called the RTC Watchdog Timer, or RWDT).
During the flash boot process, RWDT and the first MWDT are enabled automatically in order to detect and
recover from booting errors.
## Feature List
•Four stages:
–Each with a programmable timeout value
–Each stage can be configured, enabled and disabled separately
•Upon expiry of each stage:
–Interrupt, CPU reset, or core reset occurs for MWDT
–Interrupt, CPU reset, core reset, or system reset occurs for RWDT
•32-bit expiry counter
•Write protection, to prevent RWDT and MWDT configuration from being altered inadvertently
•Flash boot protection: If the boot process from an SPI flash does not complete within a predetermined
period of time, the watchdog will reboot the entire main system
For details, seeESP32-S3 Technical Reference Manual> ChapterWatchdog Timers.
4.1.3.9XTAL32K Watchdog Timers
Interrupt and Wake-Up
When the XTAL32K watchdog timer detects the oscillation failure of XTAL32K_CLK, an oscillation failure
interrupt RTC_XTAL32K_DEAD_INT (for interrupt description, please refer to
ESP32-S3 Technical Reference Manual> ChapterLow-power Management) is generated. At this point, the
CPU will be woken up if in Light-sleep mode or Deep-sleep mode.
## BACKUP32K_CLK
Once the XTAL32K watchdog timer detects the oscillation failure of XTAL32K_CLK, it replaces XTAL32K_CLK
with BACKUP32K_CLK (with a frequency of 32 kHz or so) derived from RTC_CLK as RTC’s SLOW_CLK, so as to
ensure proper functioning of the system.
For details, see
ESP32-S3 Technical Reference Manual> ChapterXTAL32K Watchdog Timers.
4.1.3.10Permission Control
In ESP32-S3, the Permission Control module is used to control access to the slaves (including internal
memory, peripherals, external flash, and RAM). The host can access its slave only if it has the right permission.
In this way, data and instructions are protected from illegitimate read or write.
## Espressif Systems45
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
The ESP32-S3 CPU can run in both Secure World and Non-secure World where independent permission
controls are adopted. The Permission Control module is able to identify which World the host is running and
then proceed with its normal operations.
## Feature List
•Manage access to internal memory by:
## –CPU
–CPU trace module
## –GDMA
•Manage access to external flash and RAM by:
## –MMU
## –SPI1
## –GDMA
–CPU through Cache
•Manage access to peripherals, supporting
–independent permission control for each peripheral
–monitoring non-aligned access
–access control for customized address range
•Integrate permission lock register
–All permission registers can be locked with the permission lock register. Once locked, the
permission register and the lock register cannot be modified, unless the CPU is reset.
•Integrate permission monitor interrupt
–In case of illegitimate access, the permission monitor interrupt will be triggered and the CPU will be
informed to handle the interrupt.
For details, see
ESP32-S3 Technical Reference Manual> ChapterPermission Control.
4.1.3.11World Controller
ESP32-S3 can divide the hardware and software resources into a Secure World and a Non-Secure World to
prevent sabotage or access to device information. Switching between the two worlds is performed by the
## World Controller.
## Feature List
•Control of the CPU switching between secure and non-secure worlds
•Control of 15 DMA peripherals switching between secure and non-secure worlds
•Record of CPU’s world switching logs
•Shielding of the CPU’s NMI interrupt
## Espressif Systems46
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
For details, seeESP32-S3 Technical Reference Manual> ChapterWorld Controller.
4.1.3.12System Registers
ESP32-S3 system registers can be used to control the following peripheral blocks and core modules:
•System and memory
•Clock
•Software Interrupt
•Low-power management
•Peripheral clock gating and reset
•CPU Control
For details, seeESP32-S3 Technical Reference Manual> ChapterSystem Registers.
4.1.4Cryptography and Security Component
This subsection describes the security features incorporated into the chip, which safeguard data and
operations.
4.1.4.1SHA Accelerator
ESP32-S3 integrates an SHA accelerator, which is a hardware device that speeds up SHA algorithm
significantly.
## Feature List
•All the hash algorithms introduced inFIPSPUB180-4Spec.
## –SHA-1
## –SHA-224
## –SHA-256
## –SHA-384
## –SHA-512
## –SHA-512/224
## –SHA-512/256
–SHA-512/t
•Two working modes
–Typical SHA
## –DMA-SHA
•interleaved function when working in Typical SHA working mode
•Interrupt function when working in DMA-SHA working mode
For details, seeESP32-S3 Technical Reference Manual> ChapterSHA Accelerator.
## Espressif Systems47
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.1.4.2AES Accelerator
ESP32-S3 integrates an Advanced Encryption Standard (AES) Accelerator, which is a hardware device that
speeds up AES algorithm significantly.
## Feature List
•Typical AES working mode
–AES-128/AES-256 encryption and decryption
•DMA-AES working mode
–AES-128/AES-256 encryption and decryption
–Block cipher mode
*ECB (Electronic Codebook)
*CBC (Cipher Block Chaining)
*OFB (Output Feedback)
*CTR (Counter)
*CFB8 (8-bit Cipher Feedback)
*CFB128 (128-bit Cipher Feedback)
–Interrupt on completion of computation
For details, see
ESP32-S3 Technical Reference Manual> ChapterAES Accelerator.
4.1.4.3RSA Accelerator
The RSA Accelerator provides hardware support for high precision computation used in various RSA
asymmetric cipher algorithms.
## Feature List
•Large-number modular exponentiation with two optional acceleration options
•Large-number modular multiplication, up to 4096 bits
•Large-number multiplication, with operands up to 2048 bits
•Operands of different lengths
•Interrupt on completion of computation
For details, seeESP32-S3 Technical Reference Manual> ChapterRSA Accelerator.
4.1.4.4Secure Boot
Secure Boot feature uses a hardware root of trust to ensure only signed firmware (with RSA-PSS signature) can
be booted.
## Espressif Systems48
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.1.4.5HMAC Accelerator
The Hash-based Message Authentication Code (HMAC) module computes Message Authentication Codes
(MACs) using Hash algorithm and keys as described in RFC 2104.
## Feature List
•Standard HMAC-SHA-256 algorithm
•Hash result only accessible by configurable hardware peripheral (in downstream mode)
•Compatible to challenge-response authentication algorithm
•Generates required keys for the RSA Digital Signature Peripheral (RSA_DS) (in downstream mode)
•Re-enables soft-disabled JTAG (in downstream mode)
For details, seeESP32-S3 Technical Reference Manual> ChapterHMAC Accelerator.
4.1.4.6RSA Digital Signature Peripheral (RSA_DS)
An RSA Digital Signature Peripheral (RSA_DS) is used to verify the authenticity and integrity of a message
using a cryptographic algorithm.
## Feature List
•RSA_DS with key length up to 4096 bits
•Encrypted private key data, only decryptable by RSA_DS
•SHA-256 digest to protect private key data against tampering by an attacker
For details, seeESP32-S3 Technical Reference Manual> ChapterRSA Digital Signature Peripheral (RSA_DS).
4.1.4.7External Memory Encryption and Decryption
ESP32-S3 integrates an External Memory Encryption and Decryption module that complies with the XTS-AES
standard.
## Feature List
•General XTS-AES algorithm, compliant with IEEE Std 1619-2007
•Software-based manual encryption
•High-speed auto encryption, without software’s participation
•High-speed auto decryption, without software’s participation
•Encryption and decryption functions jointly determined by registers configuration, eFuse parameters,
and boot mode
For details, seeESP32-S3 Technical Reference Manual> ChapterExternal Memory Encryption and
## Decryption.
## Espressif Systems49
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.1.4.8Clock Glitch Detection
The Clock Glitch Detection module on ESP32-S3 monitors input clock signals from XTAL_CLK. If it detects a
glitch with a width shorter than 3 ns, input clock signals from XTAL_CLK are blocked.
For details, seeESP32-S3 Technical Reference Manual> ChapterClock Glitch Detection.
4.1.4.9Random Number Generator
The random number generator (RNG) in ESP32-S3 generates true random numbers, which means random
number generated from a physical process, rather than by means of an algorithm. No number generated
within the specified range is more or less likely to appear than any other number.
For details, seeESP32-S3 Technical Reference Manual> ChapterRandom Number Generator.
## Espressif Systems50
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.2Peripherals
This section describes the chip’s peripheral capabilities, covering connectivity interfaces and on-chip sensors
that extend its functionality.
4.2.1Connectivity Interface
This subsection describes the connectivity interfaces on the chip that enable communication and interaction
with external devices and networks.
4.2.1.1UART Controller
ESP32-S3 has three UART (Universal Asynchronous Receiver Transmitter) controllers, i.e., UART0, UART1, and
UART2, which support IrDA and asynchronous communication (RS232 and RS485) at a speed of up to 5
## Mbps.
## Feature List
•Three clock sources that can be divided
•Programmable baud rate
•1024 x 8-bit RAM shared by TX FIFOs and RX FIFOs of the three UART controllers
•Full-duplex asynchronous communication
•Automatic baud rate detection of input signals
•Data bits ranging from 5 to 8
•Stop bits of 1, 1.5, 2, or 3 bits
•Parity bit
•Special character AT_CMD detection
•RS485 protocol
•IrDA protocol
•High-speed data communication using GDMA
•UART as wake-up source
•Software and hardware flow control
For details, seeESP32-S3 Technical Reference Manual> ChapterUART Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.2I2C Interface
ESP32-S3 has two I2C bus interfaces which are used for I2C master mode or slave mode, depending on the
user’s configuration.
## Espressif Systems51
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
## Feature List
•Standard mode (100 kbit/s)
•Fast mode (400 kbit/s)
•Up to 800 kbit/s (constrained by SCL and SDA pull-up strength)
•7-bit and 10-bit addressing mode
•Double addressing mode (slave addressing and slave register addressing)
The hardware provides a command abstraction layer to simplify the usage of the I2C peripheral.
For details, seeESP32-S3 Technical Reference Manual> ChapterI2C Controller.
## Pin Assignment
For details, see Section
2.3.5Peripheral Pin Assignment.
4.2.1.3I2S Interface
ESP32-S3 includes two standard I2S interfaces. They can operate in master mode or slave mode, in
full-duplex mode or half-duplex communication mode, and can be configured to operate with an 8-bit, 16-bit,
24-bit, or 32-bit resolution as an input or output channel. BCK clock frequency, from 10 kHz up to 40 MHz, is
supported.
The I2S interface has a dedicated DMA controller. It supports TDM PCM, TDM MSB alignment, TDM LSB
alignment, TDM Phillips, and PDM interface.
For details, see
ESP32-S3 Technical Reference Manual> ChapterI2S Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.4LCD and Camera Controller
The LCD and Camera controller of ESP32-S3 consists of a LCD module and a camera module.
The LCD module is designed to send parallel video data signals, and its bus supports 8-bit~16-bit parallel
RGB, I8080, and MOTO6800 interfaces. These interfaces operate at 40 MHz or lower, and support conversion
among RGB565, YUV422, YUV420, and YUV411.
The camera module is designed to receive parallel video data signals, and its bus supports an 8-bit~16-bit
DVP image sensor, with clock frequency of up to 40 MHz. The camera interface supports conversion among
RGB565, YUV422, YUV420, and YUV411.
For details, see
ESP32-S3 Technical Reference Manual> ChapterLCD and Camera Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
## Espressif Systems52
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.2.1.5Serial Peripheral Interface (SPI)
ESP32-S3 has the following SPI interfaces:
•SPI0used by ESP32-S3’s GDMA controller and cache to access in-package or off-package flash/PSRAM
•SPI1used by the CPU to access in-package or off-package flash/PSRAM
•SPI2is a general purpose SPI controller with access to a DMA channel allocated by the GDMA controller
•SPI3is a general purpose SPI controller with access to a DMA channel allocated by the GDMA controller
## Feature List
•SPI0 and SPI1:
–Supports Single SPI, Dual SPI, Quad SPI, Octal SPI, QPI, and OPI modes
–8-line SPI mode supports single data rate (SDR) and double data rate (DDR)
–Configurable clock frequency with a maximum of 120 MHz for 8-line SPI SDR/DDR modes
–Data transmission is in bytes
## •SPI2:
–Supports operation as a master or slave
–Connects to a DMA channel allocated by the GDMA controller
–Supports Single SPI, Dual SPI, Quad SPI, Octal SPI, QPI, and OPI modes
–Configurable clock polarity (CPOL) and phase (CPHA)
–Configurable clock frequency
–Data transmission is in bytes
–Configurable read and write data bit order: most-significant bit (MSB) first, or least-significant bit
(LSB) first
–As a master
*Supports 2-line full-duplex communication with clock frequency up to 80 MHz
*Full-duplex 8-line SPI mode supports single data rate (SDR) only
*Supports 1-, 2-, 4-, 8-line half-duplex communication with clock frequency up to 80 MHz
*Half-duplex 8-line SPI mode supports both single data rate (up to 80 MHz) and double data rate
(up to 40 MHz)
*Provides six SPI_CS pins for connection with six independent SPI slaves
*Configurable CS setup time and hold time
–As a slave
*Supports 2-line full-duplex communication with clock frequency up to 60 MHz
*Supports 1-, 2-, 4-line half-duplex communication with clock frequency up to 60 MHz
*Full-duplex and half-duplex 8-line SPI mode supports single data rate (SDR) only
## Espressif Systems53
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
## •SPI3:
–Supports operation as a master or slave
–Connects to a DMA channel allocated by the GDMA controller
–Supports Single SPI, Dual SPI, Quad SPI, and QPI modes
–Configurable clock polarity (CPOL) and phase (CPHA)
–Configurable clock frequency
–Data transmission is in bytes
–Configurable read and write data bit order: most-significant bit (MSB) first, or least-significant bit
(LSB) first
–As a master
*Supports 2-line full-duplex communication with clock frequency up to 80 MHz
*Supports 1-, 2-, 4-line half-duplex communication with clock frequency up to 80 MHz
*Provides three SPI_CS pins for connection with three independent SPI slaves
*Configurable CS setup time and hold time
–As a slave
*Supports 2-line full-duplex communication with clock frequency up to 60 MHz
*Supports 1-, 2-, 4-line half-duplex communication with clock frequency up to 60 MHz
For details, seeESP32-S3 Technical Reference Manual> ChapterSPI Controller.
## Pin Assignment
For details, see Section
2.3.5Peripheral Pin Assignment.
4.2.1.6Two-Wire Automotive Interface (TWAI
## ®
## )
The Two-Wire Automotive Interface (TWAI
## ®
) is a multi-master, multi-cast communication protocol with error
detection and signaling as well as inbuilt message priorities and arbitration.
## Feature List
•Compatible with ISO 11898-1 protocol (CAN Specification 2.0)
•Standard frame format (11-bit ID) and extended frame format (29-bit ID)
•Bit rates from 1 Kbit/s to 1 Mbit/s
•Multiple modes of operation:
–Normal
–Listen Only
–Self-Test (no acknowledgment required)
•64-byte receive FIFO
## Espressif Systems54
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Acceptance filter (single and dual filter modes)
•Error detection and handling:
–Error counters
–Configurable error interrupt threshold
–Error code capture
–Arbitration lost capture
For details, seeESP32-S3 Technical Reference Manual> ChapterTwo-wire Automotive Interface.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.7USB 2.0 OTG Full-Speed Interface
ESP32-S3 features a full-speed USB OTG interface along with an integrated transceiver. The USB OTG
interface complies with the USB 2.0 specification.
## General Features
•FS and LS data rates
•HNP and SRP as A-device or B-device
•Dynamic FIFO (DFIFO) sizing
•Multiple modes of memory access
–Scatter/Gather DMA mode
–Buffer DMA mode
–Slave mode
•Can choose integrated transceiver or external transceiver
•Utilizing integrated transceiver with USB Serial/JTAG by time-division multiplexing when only integrated
transceiver is used
•Support USB OTG using one of the transceivers while USB Serial/JTAG using the other one when both
integrated transceiver or external transceiver are used
## Device Mode Features
•Endpoint number 0 always present (bi-directional, consisting of EP0 IN and EP0 OUT)
•Six additional endpoints (endpoint numbers 1 to 6), configurable as IN or OUT
•Maximum of five IN endpoints concurrently active at any time (including EP0 IN)
•All OUT endpoints share a single RX FIFO
•Each IN endpoint has a dedicated TX FIFO
## Espressif Systems55
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
## Host Mode Features
•Eight channels (pipes)
–A control pipe consists of two channels (IN and OUT), as IN and OUT transactions must be handled
separately. Only Control transfer type is supported.
–Each of the other seven channels is dynamically configurable to be IN or OUT, and supports Bulk,
Isochronous, and Interrupt transfer types.
•All channels share an RX FIFO, non-periodic TX FIFO, and periodic TX FIFO. The size of each FIFO is
configurable.
For details, seeESP32-S3 Technical Reference Manual> ChapterUSB On-The-Go.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.8USB Serial/JTAG Controller
ESP32-S3 integrates a USB Serial/JTAG controller.
## Feature List
•USB Full-speed device.
•Can be configured to either use internal USB PHY of ESP32-S3 or external PHY via GPIO matrix.
•Fixed function device, hardwired for CDC-ACM (Communication Device Class - Abstract Control Model)
and JTAG adapter functionality.
•Two OUT Endpoints, three IN Endpoints in addition to Control Endpoint 0; Up to 64-byte data payload
size.
•Internal PHY, so no or very few external components needed to connect to a host computer.
•CDC-ACM adherent serial port emulation is plug-and-play on most modern OSes.
•JTAG interface allows fast communication with CPU debug core using a compact representation of JTAG
instructions.
•CDC-ACM supports host controllable chip reset and entry into download mode.
For details, seeESP32-S3 Technical Reference Manual> ChapterUSB Serial/JTAG Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.9SD/MMC Host Controller
ESP32-S3 has an SD/MMC Host controller.
## Espressif Systems56
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
## Feature List
•Secure Digital (SD) memory version 3.0 and version 3.01
•Secure Digital I/O (SDIO) version 3.0
•Consumer Electronics Advanced Transport Architecture (CE-ATA) version 1.1
•Multimedia Cards (MMC version 4.41, eMMC version 4.5 and version 4.51)
•Up to 80 MHz clock output
•Three data bus modes:
## –1-bit
–4-bit (supports two SD/SDIO/MMC 4.41 cards, and one SD card operating at 1.8 V in 4-bit mode)
## –8-bit
## Note:
When working at 80 MHz, the clock phase adjustment is limited and only phase 0° and 180° are supported. The PCB
layout should be optimized accordingly to ensure timing closure.
For details, seeESP32-S3 Technical Reference Manual> ChapterSD/MMC Host Controller.
## Pin Assignment
For details, see Section
2.3.5Peripheral Pin Assignment.
## Feature List
•Can generate a digital waveform with configurable periods and duty cycle. The duty cycle resolution can
be up to 14 bits within a 1 ms period
•Multiple clock sources, including APB clock and external main crystal clock
•Can operate when the CPU is in Light-sleep mode
•Gradual increase or decrease of duty cycle, useful for the LED RGB color-fading generator
For details, see
ESP32-S3 Technical Reference Manual> ChapterLED PWM Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.10Motor Control PWM (MCPWM)
ESP32-S3 integrates two MCPWMs that can be used to drive digital motors and smart light. Each MCPWM
peripheral has one clock divider (prescaler), three PWM timers, three PWM operators, and a capture module.
PWM timers are used for generating timing references. The PWM operators generate desired waveform based
on the timing references. Any PWM operator can be configured to use the timing references of any PWM
timers. Different PWM operators can use the same PWM timer’s timing references to produce related PWM
## Espressif Systems57
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
signals. PWM operators can also use different PWM timers’ values to produce the PWM signals that work
alone. Different PWM timers can also be synchronized together.
For details, seeESP32-S3 Technical Reference Manual> ChapterMotor Control PWM.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.11Remote Control Peripheral (RMT)
The Remote Control Peripheral (RMT) is designed to send and receive infrared remote control signals.
## Feature List
•Four TX channels
•Four RX channels
•Support multiple channels (programmable) transmitting data simultaneously
•Eight channels share a 384 x 32-bit RAM
•Support modulation on TX pulses
•Support filtering and demodulation on RX pulses
•Wrap TX mode
•Wrap RX mode
•Continuous TX mode
•DMA access for TX mode on channel 3
•DMA access for RX mode on channel 7
For details, seeESP32-S3 Technical Reference Manual> ChapterRemote Control Peripheral.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.1.12Pulse Count Controller (PCNT)
The pulse count controller (PCNT) captures pulse and counts pulse edges through multiple modes.
## Feature List
•Four independent pulse counters (units) that count from 1 to 65535
•Each unit consists of two independent channels sharing one pulse counter
•All channels have input pulse signals (e.g. sig_ch0_un) with their corresponding control signals (e.g.
ctrl_ch0_u
n)
•Independently filter glitches of input pulse signals (sig_ch0_unand sig_ch1_un) and control signals
(ctrl_ch0_unand ctrl_ch1_un) on each unit
## Espressif Systems58
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
•Each channel has the following parameters:
1.Selection between counting on positive or negative edges of the input pulse signal
2.Configuration to Increment, Decrement, or Disable counter mode for control signal’s high and low
states
For details, seeESP32-S3 Technical Reference Manual> ChapterPulse Count Controller.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.2Analog Signal Processing
This subsection describes components on the chip that sense and process real-world data.
## 4.2.2.1SAR ADC
ESP32-S3 integrates two 12-bit SAR ADCs and supports measurements on 20 channels (analog-enabled pins).
For power-saving purpose, the ULP coprocessors in ESP32-S3 can also be used to measure voltage in sleep
modes. By using threshold settings or other methods, we can awaken the CPU from sleep modes.
## Note:
Please note that the ADC2_CH... analog functions (see Table2-8Analog Functions) cannot be used with Wi-Fi simul-
taneously.
For details, seeESP32-S3 Technical Reference Manual> ChapterOn-Chip Sensors and Analog Signal
## Processing
## .
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
4.2.2.2Temperature Sensor
The temperature sensor generates a voltage that varies with temperature. The voltage is internally converted
via an ADC into a digital value.
The temperature sensor has a range of40 °C to 125 °C. It is designed primarily to sense the temperature
changes inside the chip. The temperature value depends on factors such as microcontroller clock frequency
or I/O load. Generally, the chip’s internal temperature is higher than the ambient temperature.
For details, seeESP32-S3 Technical Reference Manual> ChapterOn-Chip Sensors and Analog Signal
## Processing.
4.2.2.3Touch Sensor
ESP32-S3 has 14 capacitive-sensing GPIOs, which detect variations induced by touching or approaching the
GPIOs with a finger or other objects. The low-noise nature of the design and the high sensitivity of the circuit
allow relatively small pads to be used. Arrays of pads can also be used, so that a larger area or more points
## Espressif Systems59
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
can be detected. The touch sensing performance can be further enhanced by the waterproof design and
digital filtering feature.
## Note:
ESP32-S3 touch sensor has not passed the Conducted Susceptibility (CS) test for now, and thus has limited application
scenarios.
For details, seeESP32-S3 Technical Reference Manual> ChapterOn-Chip Sensors and Analog Signal
## Processing.
## Pin Assignment
For details, see Section2.3.5Peripheral Pin Assignment.
## Espressif Systems60
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.3Wireless Communication
This section describes the chip’s wireless communication capabilities, spanning radio technology, Wi-Fi,
Bluetooth, and 802.15.4.
4.3.1Radio
This subsection describes the fundamental radio technology embedded in the chip that facilitates wireless
communication and data exchange.
4.3.1.12.4 GHz Receiver
The 2.4 GHz receiver demodulates the 2.4 GHz RF signal to quadrature baseband signals and converts them
to the digital domain with two high-resolution, high-speed ADCs. To adapt to varying signal channel
conditions, ESP32-S3 integrates RF filters, Automatic Gain Control (AGC), DC offset cancelation circuits, and
baseband filters.
4.3.1.22.4 GHz Transmitter
The 2.4 GHz transmitter modulates the quadrature baseband signals to the 2.4 GHz RF signal, and drives the
antenna with a high-powered CMOS power amplifier. The use of digital calibration further improves the linearity
of the power amplifier.
To compensate for receiver imperfections, additional calibration methods are built into the chip,
including:
•Carrier leakage compensation
•I/Q amplitude/phase matching
•Baseband nonlinearities suppression
•RF nonlinearities suppression
•Antenna matching
These built-in calibration routines reduce the cost and time to the market for your product, and eliminate the
need for specialized testing equipment.
4.3.1.3Clock Generator
The clock generator produces quadrature clock signals of 2.4 GHz for both the receiver and the transmitter. All
components of the clock generator are integrated into the chip, including inductors, varactors, filters,
regulators, and dividers.
The clock generator has built-in calibration and self-test circuits. Quadrature clock phases and phase noise
are optimized on chip with patented calibration algorithms which ensure the best performance of the receiver
and the transmitter.
4.3.2Wi-Fi
This subsection describes the chip’s Wi-Fi capabilities, which facilitate wireless communication at a high data
rate.
## Espressif Systems61
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
4.3.2.1Wi-Fi Radio and Baseband
The ESP32-S3 Wi-Fi radio and baseband support the following features:
## •802.11b/g/n
•802.11n MCS0-7 that supports 20 MHz and 40 MHz bandwidth
•802.11n MCS32
•802.11n 0.4μs guard-interval
•Data rate up to 150 Mbps
•RX STBC (single spatial stream)
•Adjustable transmitting power
•Antenna diversity:
ESP32-S3 supports antenna diversity with an external RF switch. This switch is controlled by one or
more GPIOs, and used to select the best antenna to minimize the effects of channel imperfections.
4.3.2.2Wi-Fi MAC
ESP32-S3 implements the full 802.11b/g/n Wi-Fi MAC protocol. It supports the Basic Service Set (BSS) STA
and SoftAP operations under the Distributed Control Function (DCF). Power management is handled
automatically with minimal host interaction to minimize the active duty period.
The ESP32-S3 Wi-Fi MAC applies the following low-level protocol functions automatically:
•Four virtual Wi-Fi interfaces
•Simultaneous Infrastructure BSS Station mode, SoftAP mode, and Station + SoftAP mode
•RTS protection, CTS protection, Immediate Block ACK
•Fragmentation and defragmentation
## •TX/RX A-MPDU, TX/RX A-MSDU
## •TXOP
## •WMM
•GCMP, CCMP, TKIP, WAPI, WEP, BIP, WPA2-PSK/WPA2-Enterprise, and WPA3-PSK/WPA3-Enterprise
•Automatic beacon monitoring (hardware TSF)
•802.11mc FTM
4.3.2.3Networking Features
Users are provided with libraries for TCP/IP networking, ESP-WIFI-MESH networking, and other networking
protocols over Wi-Fi. TLS 1.2 support is also provided.
4.3.3Bluetooth LE
This subsection describes the chip’s Bluetooth capabilities, which facilitate wireless communication for
low-power, short-range applications. ESP32-S3 includes a Bluetooth Low Energy subsystem that integrates a
## Espressif Systems62
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 4  Functional Description
hardware link layer controller, an RF/modem block and a feature-rich software protocol stack. It supports the
core features of Bluetooth 5 and Bluetooth Mesh.
4.3.3.1Bluetooth LE PHY
Bluetooth Low Energy radio and PHY in ESP32-S3 support:
•1 Mbps PHY
•2 Mbps PHY for high transmission speed and high data throughput
•Coded PHY for high RX sensitivity and long range (125 Kbps and 500 Kbps)
•Class 1 transmit power without external PA
•HW Listen Before Talk (LBT)
4.3.3.2Bluetooth LE Link Controller
Bluetooth Low Energy Link Layer Controller in ESP32-S3 supports:
•LE Advertising Extensions, to enhance broadcasting capacity and broadcast more intelligent data
•Multiple Advertising Sets
•Simultaneous Advertising and Scanning
•Multiple connections in simultaneous central and peripheral roles
•Adaptive Frequency Hopping (AFH) and Channel Assessment
•LE Channel Selection Algorithm #2
•Connection Parameter Update
•High Duty Cycle Non-Connectable Advertising
•LE Privacy v1.2
•LE Data Packet Length Extension
•Link Layer Extended Scanner Filter Policies
•Low Duty Cycle Directed Advertising
•Link Layer Encryption
•LE Ping
## Espressif Systems63
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
5Electrical Characteristics
5.1Absolute Maximum Ratings
Stresses above those listed in Table5-1Absolute Maximum Ratingsmay cause permanent damage to the
device. These are stress ratings only and normal operation of the device at these or any other conditions
beyond those indicated in Section5.2Recommended Operating Conditionsis not implied. Exposure to
absolute-maximum-rated conditions for extended periods may affect device reliability.
## Table 5-1. Absolute Maximum Ratings
ParameterDescriptionMinMaxUnit
Input power pins
## 1
Allowed input voltage0.33.6V
## I
output
## 2
Cumulative IO output current—1500mA
## T
## ST ORE
Storage temperature40150°C
## 1
For more information on input power pins, see Section2.5.1Power Pins.
## 2
The product proved to be fully functional after all its IO pins were pulled high
while being connected to ground for 24 consecutive hours at ambient tem-
perature of 25 °C.
5.2Recommended Operating Conditions
For recommended ambient temperature, see Section1ESP32-S3 Series Comparison.
## Table 5-2. Recommended Operating Conditions
## Parameter
## 1
DescriptionMinTypMaxUnit
VDDA, VDD3P3Recommended input voltage3.03.33.6V
## VDD3P3_RTC
## 2
Recommended input voltage3.03.33.6V
VDD_SPI (as input)—1.83.33.6V
## VDD3P3_CPU
## 3
Recommended input voltage3.03.33.6V
## I
## V DD
## 4
Cumulative input current0.5——A
## 1
See in conjunction with Section2.5Power Supply.
## 2
If VDD3P3_RTC is used to power VDD_SPI (see Section2.5.2Power Scheme),
the voltage drop on R
## SP I
should be accounted for. See also Section5.3VDD_SPI
## Output Characteristics
## .
## 3
If writing to eFuses, the voltage on VDD3P3_CPU should not exceed 3.3 V as the
circuits responsible for burning eFuses are sensitive to higher voltages.
## 4
If you use a single power supply, the recommended output current is 500 mA or
more.
## Espressif Systems64
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
5.3VDD_SPI Output Characteristics
Table 5-3. VDD_SPI Internal and Output Characteristics
ParameterDescription
## 1
TypUnit
## R
## SP I
VDD_SPI powered by VDD3P3_RTC via R
## SP I
for 3.3 V flash/PSRAM
## 2
## 14Ω
## I
## SP I
Output current when VDD_SPI is powered by
Flash Voltage Regulator for 1.8 V flash/PSRAM
40mA
## 1
See in conjunction with Section2.5.2Power Scheme.
## 2
VDD3P3_RTC must be more thanVDD_flash_min + I_flash_max * R
## SP I
## ;
where
•VDD_flash_min– minimum operating voltage of flash/PSRAM
•I_flash_max– maximum operating current of flash/PSRAM
5.4DC Characteristics (3.3 V, 25 °C)
Table 5-4. DC Characteristics (3.3 V, 25 °C)
ParameterDescriptionMinTypMaxUnit
## C
## IN
Pin capacitance—2—pF
## V
## IH
High-level input voltage0.75 × VDD
## 1
## —VDD
## 1
## + 0.3V
## V
## IL
Low-level input voltage0.3—0.25 × VDD
## 1
## V
## I
## IH
High-level input current——50nA
## I
## IL
Low-level input current——50nA
## V
## OH
## 2
High-level output voltage0.8 × VDD
## 1
## ——V
## V
## OL
## 2
Low-level output voltage——0.1 × VDD
## 1
## V
## I
## OH
High-level source current (VDD
## 1
## = 3.3 V,
## V
## OH
## >= 2.64 V, PAD_DRIVER = 3)
—40—mA
## I
## OL
Low-level sink current (VDD
## 1
## = 3.3 V, V
## OL
## =
## 0.495 V, PAD_DRIVER = 3)
—28—mA
## R
## P U
Internal weak pull-up resistor—45—kΩ
## R
## P D
Internal weak pull-down resistor—45—kΩ
## V
IH_nRST
Chip reset release voltage (CHIP_PU voltage
is within the specified range)
## 0.75 × VDD
## 1
## —VDD
## 1
## + 0.3V
## V
IL_nRST
Chip reset voltage (CHIP_PU voltage is within
the specified range)
## 0.3—0.25 × VDD
## 1
## V
## 1
VDD – voltage from a power pin of a respective power domain.
## 2
## V
## OH
and V
## OL
are measured using high-impedance load.
## Espressif Systems65
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
5.5ADC Characteristics
The measurements in this section are taken with an external 100 nF capacitor connected to the ADC, using DC
signals as input, and at an ambient temperature of 25 °C with disabled Wi-Fi.
Table 5-5. ADC Characteristics
SymbolMinMaxUnit
DNL (Differential nonlinearity)
## 1

## 4
## 4LSB
INL (Integral nonlinearity)88LSB
Sampling rate—100kSPS
## 2
## 1
To get better DNL results, you can sample multiple times and
apply a filter, or calculate the average value.
## 2
kSPS means kilo samples-per-second.
The calibrated ADC results after hardware calibration andsoftwarecalibrationare shown in Table5-6. For
higher accuracy, you may implement your own calibration methods.
Table 5-6. ADC Calibration Results
ParameterDescriptionMinMaxUnit
Total error
ATTEN0, effective measurement range of 0~85055mV
ATTEN1, effective measurement range of 0~110066mV
ATTEN2, effective measurement range of 0~16001010mV
ATTEN3, effective measurement range of 0~29005050mV
5.6Current Consumption
5.6.1Current Consumption in Active Mode
The current consumption measurements are taken with a 3.3 V supply at 25 °C ambient temperature.
TX current consumption is rated at a 100% duty cycle.
RX current consumption is rated when the peripherals are disabled and the CPU idle.
Table 5-7. Current Consumption for Wi-Fi (2.4 GHz) in Active Mode
Work ModeRF ConditionDescriptionPeak (mA)
Active (RF working)
## TX
802.11b, 1 Mbps, @21 dBm340
802.11g, 54 Mbps, @19 dBm291
802.11n, HT20, MCS7, @18.5 dBm283
802.11n, HT40, MCS7, @18 dBm286
## RX
802.11b/g/n, HT2088
802.11n, HT4091
## Espressif Systems66
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
Table 5-8. Current Consumption for Bluetooth LE in Active Mode
Work ModeRF ConditionDescriptionPeak (mA)
Active (RF working)
## TX
Bluetooth LE @ 21.0 dBm335
Bluetooth LE @ 9.0 dBm193
Bluetooth LE @ 0 dBm176
Bluetooth LE @15.0 dBm116
RXBluetooth LE93
5.6.2Current Consumption in Other Modes
The measurements below are applicable to ESP32-S3 and ESP32-S3FH8. Since ESP32-S3R2, ESP32-S3RH2,
ESP32-S3R8, ESP32-S3R8V, ESP32-S3R16V, and ESP32-S3FN4R2 are embedded with PSRAM, their current
consumption might be higher.
Table 5-9. Current Consumption in Modem-sleep Mode
Work mode
## Frequency
(MHz)
## Description
## Typ
## 1
(mA)
## Typ
## 2
(mA)
## Modem-sleep
## 3
## 40
WAITI (Dual core in idle state)13.218.8
Single core running 32-bit data access instructions, the
other core in idle state
## 16.221.8
Dual core running 32-bit data access instructions18.724.4
Single core running 128-bit data access instructions, the
other core in idle state
## 19.925.4
Dual core running 128-bit data access instructions23.028.8
## 80
## WAITI22.036.1
Single core running 32-bit data access instructions, the
other core in idle state
## 28.442.6
Dual core running 32-bit data access instructions33.147.3
Single core running 128-bit data access instructions, the
other core in idle state
## 35.149.6
Dual core running 128-bit data access instructions41.856.3
## 160
## WAITI27.642.3
Single core running 32-bit data access instructions, the
other core in idle state
## 39.954.6
Dual core running 32-bit data access instructions49.664.1
Single core running 128-bit data access instructions, the
other core in idle state
## 54.469.2
Dual core running 128-bit data access instructions66.781.1
## 240
## WAITI32.947.6
Single core running 32-bit data access instructions, the
other core in idle state
## 51.265.9
Dual core running 32-bit data access instructions66.281.3
Single core running 128-bit data access instructions, the
other core in idle state
## 72.487.9
Cont’d on next page
## Espressif Systems67
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
Table 5-9 – cont’d from previous page
Work mode
## Frequency
(MHz)
## Description
## Typ
## 1
(mA)
## Typ
## 2
(mA)
Dual core running 128-bit data access instructions91.7107.9
## 1
Current consumption when all peripheral clocks aredisabled.
## 2
Current consumption when all peripheral clocks areenabled. In practice, the current consumption might be
different depending on which peripherals are enabled.
## 3
In Modem-sleep mode, Wi-Fi is clock gated, and the current consumption might be higher when accessing
flash. For a flash rated at 80 Mbit/s, in SPI 2-line mode the consumption is 10 mA.
Table 5-10. Current Consumption in Low-Power Modes
Work modeDescriptionTyp (μA)
## Light-sleep
## 1
VDD_SPI and Wi-Fi are powered down, and all GPIOs are high-impedance.240
## Deep-sleep
The ULP co-processor
is powered on
## 2
## ULP-FSM170
## ULP-RISC-V190
ULP sensor-monitored pattern
## 3
## 18
RTC memory and RTC peripherals are powered up.8
RTC memory is powered up. RTC peripherals are powered down.7
Power offCHIP_PU is set to low level. The chip is shut down.1
## 1
In Light-sleep mode, all related SPI pins are pulled up. For chips embedded with PSRAM, please add
corresponding PSRAM consumption values, e.g., 140μA for 8 MB 8-line PSRAM (3.3 V), 200μA for
8 MB 8-line PSRAM (1.8 V) and 40μA for 2 MB 4-line PSRAM (3.3 V).
## 2
During Deep-sleep, when the ULP co-processor is powered on, peripherals such as GPIO and I2C
are able to operate.
## 3
The “ULP sensor-monitored pattern” refers to the mode where the ULP coprocessor or the sensor
works periodically. When touch sensors work with a duty cycle of 1%, the typical current consumption
is 18μA.
5.7Memory Specifications
The data below is sourced from the memory vendor datasheet. These values are guaranteed through design
and/or characterization but are not fully tested in production. Devices are shipped with the memory
erased.
## Table 5-11. Flash Specifications
ParameterDescriptionMinTypMaxUnit
## VCC
Power supply voltage (1.8 V)1.651.802.00V
Power supply voltage (3.3 V)2.73.33.6V
## F
## C
Maximum clock frequency80——MHz
—Program/erase cycles100,000——cycles
## T
## RET
Data retention time20——years
## T
## P P
Page program time—0.85ms
## T
## SE
Sector erase time (4 KB)—70500ms
Cont’d on next page
## Espressif Systems68
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 5  Electrical Characteristics
Table 5-11 – cont’d from previous page
ParameterDescriptionMinTypMaxUnit
## T
## BE1
Block erase time (32 KB)—0.22s
## T
## BE2
Block erase time (64 KB)—0.33s
## T
## CE
Chip erase time (16 Mb)—720s
Chip erase time (32 Mb)—2060s
Chip erase time (64 Mb)—25100s
Chip erase time (128 Mb)—60200s
Chip erase time (256 Mb)—70300s
Table 5-12. PSRAM Specifications
ParameterDescriptionMinTypMaxUnit
## VCC
Power supply voltage (1.8 V)1.621.801.98V
Power supply voltage (3.3 V)2.73.33.6V
## F
## C
Maximum clock frequency80——MHz
5.8Reliability
## Table 5-13. Reliability Qualifications
Test ItemTest ConditionsTest Standard
HTOL (High Temperature
## Operating Life)
125 °C, 1000 hoursJESD22-A108
ESD (Electro-Static
## Discharge Sensitivity)
HBM (Human Body Mode)
## 1
## ± 2000 VJS-001
CDM (Charge Device Mode)
## 2
## ± 1000 VJS-002
Latch up
Current trigger ± 200 mA
## JESD78
Voltage trigger 1.5 × VDD
max
## Preconditioning
Bake 24 hours @125 °C
Moisture soak (level 3: 192 hours @30 °C, 60% RH)
IR reflow solder: 260 + 0 °C, 20 seconds, three times
## J-STD-020, JESD47,
## JESD22-A113
TCT (Temperature Cycling
## Test)
65 °C / 150 °C, 500 cyclesJESD22-A104
uHAST (Highly
## Accelerated Stress Test,
unbiased)
130 °C, 85% RH, 96 hoursJESD22-A118
HTSL (High Temperature
## Storage Life)
150 °C, 1000 hoursJESD22-A103
LTSL (Low Temperature
## Storage Life)
40 °C, 1000 hoursJESD22-A119
## 1
JEDEC document JEP155 states that 500 V HBM allows safe manufacturing with a standard ESD control process.
## 2
JEDEC document JEP157 states that 250 V CDM allows safe manufacturing with a standard ESD control process.
## Espressif Systems69
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
6RF Characteristics
This section contains tables with RF characteristics of the Espressif product.
The RF data is measured at the antenna port, where RF cable is connected, including the front-end loss. The
front-end circuit is a 0Ωresistor.
Devices should operate in the center frequency range allocated by regional regulatory authorities. The target
center frequency range and the target transmit power are configurable by software. SeeESPRFTestTooland
TestGuidefor instructions.
Unless otherwise stated, the RF tests are conducted with a 3.3 V (±5%) supply at 25 ºC ambient temperature.
6.1Wi-Fi Radio
Table 6-1. Wi-Fi RF Characteristics
NameDescription
Center frequency range of operating channel2412~2484 MHz
Wi-Fi wireless standardIEEE 802.11b/g/n
6.1.1Wi-Fi RF Transmitter (TX) Characteristics
Table 6-2. TX Power with Spectral Mask and EVM Meeting 802.11 Standards
MinTypMax
Rate(dBm)(dBm)(dBm)
## 802.11b, 1 Mbps—21.0—
## 802.11b, 11 Mbps—21.0—
## 802.11g, 6 Mbps—20.5—
## 802.11g, 54 Mbps—19.0—
802.11n, HT20, MCS0—19.5—
802.11n, HT20, MCS7—18.5—
802.11n, HT40, MCS0—19.5—
802.11n, HT40, MCS7—18.0—
Table 6-3. TX EVM Test
## 1
MinTypLimit
Rate(dB)(dB)(dB)
802.11b, 1 Mbps, @21 dBm—24.510
802.11b, 11 Mbps, @21 dBm—24.510
802.11g, 6 Mbps, @20.5 dBm—21.55
802.11g, 54 Mbps, @19 dBm—28.025
Cont’d on next page
## Espressif Systems70
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
Table 6-3 – cont’d from previous page
MinTypLimit
Rate(dB)(dB)(dB)
802.11n, HT20, MCS0, @19.5 dBm—23.05
802.11n, HT20, MCS7, @18.5 dBm—29.527
802.11n, HT40, MCS0, @19.5 dBm—23.05
802.11n, HT40, MCS7, @18 dBm—29.527
## 1
EVM is measured at the corresponding typical TX power provided
in Table6-2TX Power with Spectral Mask and EVM Meeting 802.11
## Standardsabove.
6.1.2Wi-Fi RF Receiver (RX) Characteristics
For RX tests, the PER (packet error rate) limit is 8% for 802.11b, and 10% for 802.11g/n.
Table 6-4. RX Sensitivity
MinTypMax
Rate(dBm)(dBm)(dBm)
## 802.11b, 1 Mbps—98.4—
## 802.11b, 2 Mbps—95.4—
## 802.11b, 5.5 Mbps—93.0—
## 802.11b, 11 Mbps—88.6—
## 802.11g, 6 Mbps—93.2—
## 802.11g, 9 Mbps—91.8—
## 802.11g, 12 Mbps—91.2—
## 802.11g, 18 Mbps—88.6—
## 802.11g, 24 Mbps—86.0—
## 802.11g, 36 Mbps—82.4—
## 802.11g, 48 Mbps—78.2—
## 802.11g, 54 Mbps—76.5—
802.11n, HT20, MCS0—92.6—
802.11n, HT20, MCS1—91.0—
802.11n, HT20, MCS2—88.2—
802.11n, HT20, MCS3—85.0—
802.11n, HT20, MCS4—81.8—
802.11n, HT20, MCS5—77.4—
802.11n, HT20, MCS6—75.8—
802.11n, HT20, MCS7—74.2—
802.11n, HT40, MCS0—90.0—
802.11n, HT40, MCS1—88.0—
802.11n, HT40, MCS2—85.2—
802.11n, HT40, MCS3—82.0—
802.11n, HT40, MCS4—79.0—
802.11n, HT40, MCS5—74.4—
Cont’d on next page
## Espressif Systems71
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
Table 6-4 – cont’d from previous page
MinTypMax
Rate(dBm)(dBm)(dBm)
802.11n, HT40, MCS6—72.8—
802.11n, HT40, MCS7—71.4—
Table 6-5. Maximum RX Level
MinTypMax
Rate(dBm)(dBm)(dBm)
## 802.11b, 1 Mbps—5—
## 802.11b, 11 Mbps—5—
## 802.11g, 6 Mbps—5—
## 802.11g, 54 Mbps—0—
802.11n, HT20, MCS0—5—
802.11n, HT20, MCS7—0—
802.11n, HT40, MCS0—5—
802.11n, HT40, MCS7—0—
Table 6-6. RX Adjacent Channel Rejection
MinTypMax
Rate(dB)(dB)(dB)
## 802.11b, 1 Mbps—35—
## 802.11b, 11 Mbps—35—
## 802.11g, 6 Mbps—31—
## 802.11g, 54 Mbps—20—
802.11n, HT20, MCS0—31—
802.11n, HT20, MCS7—16—
802.11n, HT40, MCS0—25—
802.11n, HT40, MCS7—11—
6.2Bluetooth LE Radio
Table 6-7. Bluetooth LE Frequency
MinTypMax
Parameter(MHz)(MHz)(MHz)
Center frequency of operating channel2402—2480
## Espressif Systems72
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
6.2.1Bluetooth LE RF Transmitter (TX) Characteristics
Table 6-8. Transmitter Characteristics - Bluetooth LE 1 Mbps
ParameterDescriptionMinTypMaxUnit
RF transmit power
RF power control range24.00020.00dBm
Gain control step—3.00—dB
Carrier frequency offset and drift
## Max|f
n
## |
n=0,1,2, ..k
—2.50—kHz
## Max|f
## 0−
f
n
|—2.00—kHz
## Max|f
n
## −
f
n
## −
## 5
|—1.39—kHz
## |f
## 1−
f
## 0
|—0.80—kHz
Modulation characteristics
## ∆f1
avg
—249.00—kHz
## Min∆f2
max
(for at least
99.9% of all∆f2
max
## )
—198.00—kHz
## ∆f2
avg
## /∆f1
avg
## —0.86——
In-band spurious emissions
±2 MHz offset—37.00—dBm
±3 MHz offset—42.00—dBm
>±3 MHz offset—44.00—dBm
Table 6-9. Transmitter Characteristics - Bluetooth LE 2 Mbps
ParameterDescriptionMinTypMaxUnit
RF transmit power
RF power control range24.00020.00dBm
Gain control step—3.00—dB
Carrier frequency offset and drift
## Max|f
n
## |
n
## =0
## ,
## 1
## ,
## 2
## , ..k
—2.50—kHz
## Max|f
## 0−
f
n
|—1.90—kHz
## Max|f
n−
f
n−5
|—1.40—kHz
## |f
## 1−
f
## 0
|—1.10—kHz
Modulation characteristics
## ∆f1
avg
—499.00—kHz
## Min∆f2
max
(for at least
99.9% of all∆f2
max
## )
—416.00—kHz
## ∆f2
avg
## /∆f1
avg
## —0.89——
In-band spurious emissions
±4 MHz offset—43.80—dBm
±5 MHz offset—45.80—dBm
>±5 MHz offset—47.00—dBm
Table 6-10. Transmitter Characteristics - Bluetooth LE 125 Kbps
ParameterDescriptionMinTypMaxUnit
RF transmit power
RF power control range24.00020.00dBm
Gain control step—3.00—dB
Carrier frequency offset and drift
## Max|f
n
## |
n=0,1,2, ..k
—0.80—kHz
## Max|f
## 0−
f
n
|—0.98—kHz
## |f
n−
f
n−3
|—0.30—kHz
## |f
## 0−
f
## 3
|—1.00—kHz
Cont’d on next page
## Espressif Systems73
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
Table 6-10 – cont’d from previous page
ParameterDescriptionMinTypMaxUnit
Modulation characteristics
## ∆f1
avg
—248.00—kHz
## Min∆f1
max
(for at least
99.9% of all∆f1
max
## )
—222.00—kHz
In-band spurious emissions
±2 MHz offset—37.00—dBm
±3 MHz offset—42.00—dBm
>±3 MHz offset—44.00—dBm
Table 6-11. Transmitter Characteristics - Bluetooth LE 500 Kbps
ParameterDescriptionMinTypMaxUnit
RF transmit power
RF power control range24.00020.00dBm
Gain control step—3.00—dB
Carrier frequency offset and drift
## Max|f
n
## |
n=0,1,2, ..k
—0.70—kHz
## Max|f
## 0−
f
n
|—0.90—kHz
## |f
n−
f
n−3
|—0.85—kHz
## |f
## 0−
f
## 3
|—0.34—kHz
Modulation characteristics
## ∆f2
avg
—213.00—kHz
## Min∆f2
max
(for at least
99.9% of all∆f2
max
## )
—196.00—kHz
In-band spurious emissions
±2 MHz offset—37.00—dBm
±3 MHz offset—42.00—dBm
>±3 MHz offset—44.00—dBm
6.2.2Bluetooth LE RF Receiver (RX) Characteristics
Table 6-12. Receiver Characteristics - Bluetooth LE 1 Mbps
ParameterDescriptionMinTypMaxUnit
Sensitivity @30.8% PER——97.5—dBm
Maximum received signal @30.8% PER——8—dBm
Co-channel C/IF = F0 MHz—9—dB
Adjacent channel selectivity C/I
F = F0 + 1 MHz—3—dB
F = F0 – 1 MHz—3—dB
F = F0 + 2 MHz—28—dB
F = F0 – 2 MHz—30—dB
F = F0 + 3 MHz—31—dB
F = F0 – 3 MHz—33—dB
F>F0 + 3 MHz—32—dB
F>F0 – 3 MHz—36—dB
Image frequency——32—dB
Adjacent channel to image frequency
## F = F
image
+ 1 MHz—39—dB
## F = F
image
– 1 MHz—31—dB
Cont’d on next page
## Espressif Systems74
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
Table 6-12 – cont’d from previous page
ParameterDescriptionMinTypMaxUnit
Out-of-band blocking performance
30 MHz~2000 MHz—9—dBm
2003 MHz~2399 MHz—19—dBm
2484 MHz~2997 MHz—16—dBm
3000 MHz~12.75 GHz—5—dBm
Intermodulation——31—dBm
Table 6-13. Receiver Characteristics - Bluetooth LE 2 Mbps
ParameterDescriptionMinTypMaxUnit
Sensitivity @30.8% PER——93.5—dBm
Maximum received signal @30.8% PER——3—dBm
Co-channel C/IF = F0 MHz—10—dB
Adjacent channel selectivity C/I
F = F0 + 2 MHz—8—dB
F = F0 – 2 MHz—5—dB
F = F0 + 4 MHz—31—dB
F = F0 – 4 MHz—33—dB
F = F0 + 6 MHz—37—dB
F = F0 – 6 MHz—37—dB
F>F0 + 6 MHz—40—dB
F>F0 – 6 MHz—40—dB
Image frequency——31—dB
Adjacent channel to image frequency
## F = F
image
+ 2 MHz—37—dB
## F = F
image
– 2 MHz—8—dB
Out-of-band blocking performance
30 MHz~2000 MHz—16—dBm
2003 MHz~2399 MHz—20—dBm
2484 MHz~2997 MHz—16—dBm
3000 MHz~12.75 GHz—16—dBm
Intermodulation——30—dBm
Table 6-14. Receiver Characteristics - Bluetooth LE 125 Kbps
ParameterDescriptionMinTypMaxUnit
Sensitivity @30.8% PER——104.5—dBm
Maximum received signal @30.8% PER——8—dBm
Co-channel C/IF = F0 MHz—6—dB
Adjacent channel selectivity C/I
F = F0 + 1 MHz—6—dB
F = F0 – 1 MHz—5—dB
F = F0 + 2 MHz—32—dB
F = F0 – 2 MHz—39—dB
F = F0 + 3 MHz—35—dB
F = F0 – 3 MHz—45—dB
F>F0 + 3 MHz—35—dB
Cont’d on next page
## Espressif Systems75
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

6  RF Characteristics
Table 6-14 – cont’d from previous page
ParameterDescriptionMinTypMaxUnit
F>F0 – 3 MHz—48—dB
Image frequency——35—dB
Adjacent channel to image frequency
## F = F
image
+ 1 MHz—49—dB
## F = F
image
– 1 MHz—32—dB
Table 6-15. Receiver Characteristics - Bluetooth LE 500 Kbps
ParameterDescriptionMinTypMaxUnit
Sensitivity @30.8% PER——101—dBm
Maximum received signal @30.8% PER——8—dBm
Co-channel C/IF = F0 MHz—4—dB
Adjacent channel selectivity C/I
F = F0 + 1 MHz—5—dB
F = F0 – 1 MHz—5—dB
F = F0 + 2 MHz—28—dB
F = F0 – 2 MHz—36—dB
F = F0 + 3 MHz—36—dB
F = F0 – 3 MHz—38—dB
F>F0 + 3 MHz—37—dB
F>F0 – 3 MHz—41—dB
Image frequency——37—dB
Adjacent channel to image frequency
## F = F
image
+ 1 MHz—44—dB
## F = F
image
– 1 MHz—28—dB
## Espressif Systems76
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 7  Packaging
7Packaging
•For information about tape, reel, and product marking, please refer to
ESP32-S3 Chip Packaging Information.
•The pins of the chip are numbered in anti-clockwise order starting from Pin 1 in the top view. For pin
numbers and pin names, see also Figure2-1ESP32-S3 Pin Layout (Top View).
•The recommended land patternsourcefile(asc)is available for download. You can import the file with
software such as PADS and Altium Designer.
•All ESP32-S3 chip variants have identical land pattern (see Figure7-1) except ESP32-S3FH4R2 has a
bigger EPAD (see Figure7-2). Thesourcefile(asc)may be adopted for ESP32-S3FH4R2 by altering the
size of the EPAD (see dimensions D2 and E2 in Figure7-2).
## Pin 1
## Pin 2
## Pin 3
## Pin 1
## Pin 2
## Pin 3
Figure 7-1. QFN56 (7×7 mm) Package
## Espressif Systems77
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## 7  Packaging
## 
ӗ૱ཆᖒമ
## 6,*1$785($5($
## ᳼჋ދᏳ
## FOREHOPE ELECTRONIC
## )25(+23(&21),'(17,$/%
7KLVGRFXPHQWDQGLWVLQIRUPDWLRQKHUHLQDUHWKHSURSHUW\RI)RUHKRSHDQGDOOXQDXWKRUL]HGXVHDQGUHSURGXFWLRQDUHSURKLELWHG
## 6KDZQ
## 3DGUDLF
## 4)1:%h/%
## 37
## 
## $
## 2)
g
Figure 7-2. QFN56 (7×7 mm) Package (Only for ESP32-S3FH4R2)
## Espressif Systems78
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

ESP32-S3 Consolidated Pin Overview
ESP32-S3 Consolidated Pin Overview
## Table 7-1. Consolidated Pin Overview
## Pin Settings
RTC IO MUX Function
## Analog Function
IO MUX Function
## Pin No.
## Pin Name
## Pin Type
## Pin Providing Power
## At Reset
## After Reset
## F0
## F3
## F0
## F1
## F0
## Type
## F1
## Type
## F2
## Type
## F3
## Type
## F4
## Type
## 1
## LNA_IN
## Analog
## 2
## VDD3P3
## Power
## 3
## VDD3P3
## Power
## 4
## CHIP_PU
## Analog
## VDD3P3_RTC
## 5
## GPIO0
## IO
## VDD3P3_RTC
## WPU, IE
## WPU, IE
## RTC_GPIO0
sar_i2c_scl_0
## GPIO0
## I/O/T
## GPIO0
## I/O/T
## 6
## GPIO1
## IO
## VDD3P3_RTC
## IE
## IE
## RTC_GPIO1
sar_i2c_sda_0
## TOUCH1
## ADC1_CH0
## GPIO1
## I/O/T
## GPIO1
## I/O/T
## 7
## GPIO2
## IO
## VDD3P3_RTC
## IE
## IE
## RTC_GPIO2
sar_i2c_scl_1
## TOUCH2
## ADC1_CH1
## GPIO2
## I/O/T
## GPIO2
## I/O/T
## 8
## GPIO3
## IO
## VDD3P3_RTC
## IE
## IE
## RTC_GPIO3
sar_i2c_sda_1
## TOUCH3
## ADC1_CH2
## GPIO3
## I/O/T
## GPIO3
## I/O/T
## 9
## GPIO4
## IO
## VDD3P3_RTC
## RTC_GPIO4
## TOUCH4
## ADC1_CH3
## GPIO4
## I/O/T
## GPIO4
## I/O/T
## 10
## GPIO5
## IO
## VDD3P3_RTC
## RTC_GPIO5
## TOUCH5
## ADC1_CH4
## GPIO5
## I/O/T
## GPIO5
## I/O/T
## 11
## GPIO6
## IO
## VDD3P3_RTC
## RTC_GPIO6
## TOUCH6
## ADC1_CH5
## GPIO6
## I/O/T
## GPIO6
## I/O/T
## 12
## GPIO7
## IO
## VDD3P3_RTC
## RTC_GPIO7
## TOUCH7
## ADC1_CH6
## GPIO7
## I/O/T
## GPIO7
## I/O/T
## 13
## GPIO8
## IO
## VDD3P3_RTC
## RTC_GPIO8
## TOUCH8
## ADC1_CH7
## GPIO8
## I/O/T
## GPIO8
## I/O/T
## SUBSPICS1
## O/T
## 14
## GPIO9
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO9
## TOUCH9
## ADC1_CH8
## GPIO9
## I/O/T
## GPIO9
## I/O/T
## SUBSPIHD
## I1/O/T
## FSPIHD
## I1/O/T
## 15
## GPIO10
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO10
## TOUCH10
## ADC1_CH9
## GPIO10
## I/O/T
## GPIO10
## I/O/T
## FSPIIO4
## I1/O/T
## SUBSPICS0
## O/T
## FSPICS0
## I1/O/T
## 16
## GPIO11
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO11
## TOUCH11
## ADC2_CH0
## GPIO11
## I/O/T
## GPIO11
## I/O/T
## FSPIIO5
## I1/O/T
## SUBSPID
## I1/O/T
## FSPID
## I1/O/T
## 17
## GPIO12
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO12
## TOUCH12
## ADC2_CH1
## GPIO12
## I/O/T
## GPIO12
## I/O/T
## FSPIIO6
## I1/O/T
## SUBSPICLK
## O/T
## FSPICLK
## I1/O/T
## 18
## GPIO13
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO13
## TOUCH13
## ADC2_CH2
## GPIO13
## I/O/T
## GPIO13
## I/O/T
## FSPIIO7
## I1/O/T
## SUBSPIQ
## I1/O/T
## FSPIQ
## I1/O/T
## 19
## GPIO14
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO14
## TOUCH14
## ADC2_CH3
## GPIO14
## I/O/T
## GPIO14
## I/O/T
## FSPIDQS
## O/T
## SUBSPIWP
## I1/O/T
## FSPIWP
## I1/O/T
## 20
## VDD3P3_RTC
## Power
## 21
## XTAL_32K_P
## IO
## VDD3P3_RTC
## RTC_GPIO15
## XTAL_32K_P
## ADC2_CH4
## GPIO15
## I/O/T
## GPIO15
## I/O/T
## U0RTS
## O
## 22
## XTAL_32K_N
## IO
## VDD3P3_RTC
## RTC_GPIO16
## XTAL_32K_N
## ADC2_CH5
## GPIO16
## I/O/T
## GPIO16
## I/O/T
## U0CTS
## I1
## 23
## GPIO17
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO17
## ADC2_CH6
## GPIO17
## I/O/T
## GPIO17
## I/O/T
## U1TXD
## O
## 24
## GPIO18
## IO
## VDD3P3_RTC
## IE
## RTC_GPIO18
## ADC2_CH7
## GPIO18
## I/O/T
## GPIO18
## I/O/T
## U1RXD
## I1
## CLK_OUT3
## O
## 25
## GPIO19
## IO
## VDD3P3_RTC
## RTC_GPIO19
## USB_D-
## ADC2_CH8
## GPIO19
## I/O/T
## GPIO19
## I/O/T
## U1RTS
## O
## CLK_OUT2
## O
## 26
## GPIO20
## IO
## VDD3P3_RTC
## USB_PU
## USB_PU
## RTC_GPIO20
## USB_D+
## ADC2_CH9
## GPIO20
## I/O/T
## GPIO20
## I/O/T
## U1CTS
## I1
## CLK_OUT1
## O
## 27
## GPIO21
## IO
## VDD3P3_RTC
## RTC_GPIO21
## GPIO21
## I/O/T
## GPIO21
## I/O/T
## 28
## SPICS1
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPICS1
## O/T
## GPIO26
## I/O/T
## 29
## VDD_SPI
## Power
## 30
## SPIHD
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPIHD
## I1/O/T
## GPIO27
## I/O/T
## 31
## SPIWP
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPIWP
## I1/O/T
## GPIO28
## I/O/T
## 32
## SPICS0
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPICS0
## O/T
## GPIO29
## I/O/T
## 33
## SPICLK
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPICLK
## O/T
## GPIO30
## I/O/T
## 34
## SPIQ
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPIQ
## I1/O/T
## GPIO31
## I/O/T
## 35
## SPID
## IO
## VDD_SPI
## WPU, IE
## WPU, IE
## SPID
## I1/O/T
## GPIO32
## I/O/T
## 36
## SPICLK_N
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## IE
## SPICLK_P_DIFF
## O/T
## GPIO48
## I/O/T
## SUBSPICLK_P_DIFF
## O/T
## 37
## SPICLK_P
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## IE
## SPICLK_N_DIFF
## O/T
## GPIO47
## I/O/T
## SUBSPICLK_N_DIFF
## O/T
## 38
## GPIO33
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## GPIO33
## I/O/T
## GPIO33
## I/O/T
## FSPIHD
## I1/O/T
## SUBSPIHD
## I1/O/T
## SPIIO4
## I1/O/T
## 39
## GPIO34
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## GPIO34
## I/O/T
## GPIO34
## I/O/T
## FSPICS0
## I1/O/T
## SUBSPICS0
## O/T
## SPIIO5
## I1/O/T
## 40
## GPIO35
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## GPIO35
## I/O/T
## GPIO35
## I/O/T
## FSPID
## I1/O/T
## SUBSPID
## I1/O/T
## SPIIO6
## I1/O/T
## 41
## GPIO36
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## GPIO36
## I/O/T
## GPIO36
## I/O/T
## FSPICLK
## I1/O/T
## SUBSPICLK
## O/T
## SPIIO7
## I1/O/T
## 42
## GPIO37
## IO
## VDD_SPI/VDD3P3_CPU
## IE
## GPIO37
## I/O/T
## GPIO37
## I/O/T
## FSPIQ
## I1/O/T
## SUBSPIQ
## I1/O/T
## SPIDQS
## I0/O/T
## 43
## GPIO38
## IO
## VDD3P3_CPU
## IE
## GPIO38
## I/O/T
## GPIO38
## I/O/T
## FSPIWP
## I1/O/T
## SUBSPIWP
## I1/O/T
## 44
## MTCK
## IO
## VDD3P3_CPU
## IE
## MTCK
## I1
## GPIO39
## I/O/T
## CLK_OUT3
## O
## SUBSPICS1
## O/T
## 45
## MTDO
## IO
## VDD3P3_CPU
## IE
## MTDO
## O/T
## GPIO40
## I/O/T
## CLK_OUT2
## O
## 46
## VDD3P3_CPU
## Power
## 47
## MTDI
## IO
## VDD3P3_CPU
## IE
## MTDI
## I1
## GPIO41
## I/O/T
## CLK_OUT1
## O
## 48
## MTMS
## IO
## VDD3P3_CPU
## IE
## MTMS
## I1
## GPIO42
## I/O/T
## 49
## U0TXD
## IO
## VDD3P3_CPU
## WPU, IE
## WPU, IE
## U0TXD
## O
## GPIO43
## I/O/T
## CLK_OUT1
## O
## 50
## U0RXD
## IO
## VDD3P3_CPU
## WPU, IE
## WPU, IE
## U0RXD
## I1
## GPIO44
## I/O/T
## CLK_OUT2
## O
## 51
## GPIO45
## IO
## VDD3P3_CPU
## WPD, IE
## WPD, IE
## GPIO45
## I/O/T
## GPIO45
## I/O/T
## 52
## GPIO46
## IO
## VDD3P3_CPU
## WPD, IE
## WPD, IE
## GPIO46
## I/O/T
## GPIO46
## I/O/T
## 53
## XTAL_N
## Analog
## 54
## XTAL_P
## Analog
## 55
## VDDA
## Power
## 56
## VDDA
## Power
## 57
## GND
## Power
## *
For details, see Section
## 2
## Pins
## . Regarding
highlighted
cells, see Section
## 2.3.4
Restrictions for GPIOs and RTC_GPIOs
## .
## Espressif Systems79
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Datasheet Versioning
## Datasheet Versioning
## Datasheet
## Version
StatusWatermarkDefinition
v0.1 ~ v0.5
(excluding v0.5)
DraftConfidential
This datasheet is under development for products
in the design stage. Specifications may change
without prior notice.
v0.5 ~ v1.0
(excluding v1.0)
## Preliminary
release
## Preliminary
This datasheet is actively updated for products in
the verification stage. Specifications may change
before mass production, and the changes will be
documentation in the datasheet’s Revision History.
v1.0 and higherOfficial release—
This datasheet is publicly released for products in
mass production. Specifications are finalized, and
major changes will be communicated viaProduct
ChangeNotifications(PCN).
Any version—
## Not
## Recommended
for New Design
## (NRND)
## 1
This datasheet is updated less frequently for
products not recommended for new designs.
Any version—
End of Life
## (EOL)
## 2
This datasheet is no longer mtained for products
that have reached end of life.
## 1
Watermark will be added to the datasheet title page only when all the product variants covered by this
datasheet are not recommended for new designs.
## 2
Watermark will be added to the datasheet title page only when all the product variants covered by this
datasheet have reached end of life.
## Espressif Systems80
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Glossary
## Glossary
module
A self-contained unit integrated within the chip to extend its capabilities, such as cryptographic modules,
RF modules2
peripheral
A hardware component or subsystem within the chip to interface with the outside world2
in-package flash
Flash integrated directly into the chip’s package, and external to the chip die4,13
off-package flash
Flash external to the chip’s package20
strapping pin
A type of GPIO pin used to configure certain operational settings during the chip’s power-up, and can be
reconfigured as normal GPIO after the chip’s reset32
eFuse parameter
A parameter stored in an electrically programmable fuse (eFuse) memory within a chip. The parameter
can be set by programming EFUSE_PGM_DATAn_REG registers, and read by reading a register field
named after the parameter
## 32
SPI boot mode
A boot mode in which users load and execute the existing code from SPI flash33
joint download boot mode
A boot mode in which users can download code into flash via the UART or other interfaces (see Table3-3
Chip Boot Mode Control> Note), and load and execute the downloaded code from the flash or SRAM33
eFuse
A one-time programmable (OTP) memory which stores system and user parameters, such as MAC
address, chip revision number, flash encryption key, etc. Value 0 indicates the default state, and value 1
indicates the eFuse has been programmed39
## Espressif Systems81
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

Related Documentation and Resources
Related Documentation and Resources
## Related Documentation
•ESP32-S3 Technical Reference Manual–Detailed information on how to use the ESP32-S3 memory and periph-
erals.
•ESP32-S3 Hardware Design Guidelines–Guidelines on how to integrate the ESP32-S3 into your hardware prod-
uct.
•ESP32-S3 Series SoC Errata–Descriptions of known errors in ESP32-S3 series of SoCs.
•Certificates
https://espressif.com/en/support/documents/certificates
•ESP32-S3 Product/Process Change Notifications (PCN)
https://espressif.com/en/support/documents/pcns?keys=ESP32-S3
•ESP32-S3 Advisories–Information on security, bugs, compatibility, component reliability.
https://espressif.com/en/support/documents/advisories?keys=ESP32-S3
•Documentation Updates and Update Notification Subscription
https://espressif.com/en/support/download/documents
## Developer Zone
•ESP-IDFProgrammingGuideforESP32-S3–Extensive documentation for the ESP-IDF development framework.
•ESP-IDFand other development frameworks on GitHub.
https://github.com/espressif
•ESP32 BBS Forum–Engineer-to-Engineer (E2E) Community for Espressif products where you can post questions,
share knowledge, explore ideas, and help solve problems with fellow engineers.
https://esp32.com/
•ESP-FAQ–A summary document of frequently asked questions released by Espressif.
https://espressif.com/projects/esp-faq/en/latest/index.html
•The ESP Journal–Best Practices, Articles, and Notes from Espressif folks.
https://blog.espressif.com/
•See the tabsSDKs and Demos,Apps,Tools,AT Firmware.
https://espressif.com/en/support/download/sdks-demos
## Products
•ESP32-S3 Series SoCs–Browse through all ESP32-S3 SoCs.
https://espressif.com/en/products/socs?id=ESP32-S3
•ESP32-S3 Series Modules–Browse through all ESP32-S3-based modules.
https://espressif.com/en/products/modules?id=ESP32-S3
•ESP32-S3 Series DevKits–Browse through all ESP32-S3-based devkits.
https://espressif.com/en/products/devkits?id=ESP32-S3
•ESP Product Selector–Find an Espressif hardware product suitable for your needs by comparing or applying filters.
https://products.espressif.com/#/product-selector?language=en
## Contact Us
•See the tabsSales Questions,Technical Enquiries,Circuit Schematic & PCB Design Review,Get Samples
(Online stores),Become Our Supplier,Comments & Suggestions.
https://espressif.com/en/contact-us/sales-questions
## Espressif Systems82
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Revision History
## Revision History
DateVersionRelease notes
## 2026-03-05v2.2
•Renamed the Digital Signature module to “RSA Digital Signature Peripheral
## (RSA_DS)”
•Updated Figure4-1Address Mapping Structure
•Added a note in Section4.2.1.9SD/MMC Host Controller
•Updated table5-10Current Consumption in Low-Power Modes
## 2025-11-28v2.1
•Updated the status of ESP32-S3R2 to End of Life and added chip variant
## ESP32-S3RH2
•Updated “Ordering Code” to “Part Number” in Table1-1ESP32-S3 Series
## Comparison
•Added Section1.3Chip Revisionand chip version information in Table1-1
ESP32-S3 Series Comparison
•Added Section2.3.5Peripheral Pin Assignmentand updated thePin As-
signmentpart for each subsection in Section4.2Peripherals
•Updated Figure3-1Visualization of Timing Parameters for the Strapping
## Pins
•Added Section5.7Memory Specifications
•Added Table5-8Current Consumption for Bluetooth LE in Active Mode
in Section5.6Current Consumption
•Added AppendixDatasheet Status DefinitionsandGlossary
•Other structural, formatting, and content improvements
## 2025-04-24v2.0
•Updated the status of ESP32-S3R8V to End of Life
•Updated the CoreMark
## ®
score in SectionCPU and Memory
•Updated Figure4.1.2Memory Organizationin Section4-1Address Map-
ping Structure
•Updated the temperature sensor’s measurement range in Section4.2.2.2
## Temperature Sensor
•Added some notes in Chapter6RF Characteristics
•Updated the source file link for the recommended land pattern in Chapter
7Packaging
Cont’d on next page
## Espressif Systems83
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Revision History
Cont’d from previous page
DateVersionRelease notes
## 2024-09-11v1.9
•Updated descriptions on the title page
•Updated feature descriptions in SectionFeaturesand adjusted the format
•Updated the pin introduction in Section2.2Pin Overviewand adjusted
the format
•Updated descriptions in Section2.3IOPins, and divided SectionRTCand
Analog Pin Functionsinto Section2.3.3Analog Functionsand Section
2.3.2RTC Functions
•Updated SectionStrapping Pinsto Section3Boot Configurations
•Adjusted the structure and section order in Section4Functional Descrip-
tion, deleted SectionPeripheral Pin Configurations, and added thePin
## Assignment
part in each subsection in Section
4.2Peripherals
## 2023-11-24v1.8
•Added chip variant ESP32-S3R16V and updated related information
•Added the second and third table notes in Table1-1ESP32-S3 Series
## Comparison
•Updated Section3.1Chip Boot Mode Control
•Updated Section5.5ADC Characteristics
•Other minor updates
## 2023-06v1.7
•Removed the sample status for ESP32-S3FH4R2
•Updated FigureESP32-S3FunctionalBlockDiagramand Figure4-2Com-
ponents and Power Domains
•Added the predefined settings at reset and after reset for GPIO20 in Table
2-1Pin Overview
•Updated notes for Table2-4IO MUX Functions
•Updated the clock name “FOSC_CLK” to “RC_FAST_CLK” in Section
4.1.3.5Power Management Unit (PMU)
•Updated descriptions in Section4.2.1.5Serial Peripheral Interface (SPI)
and Section4.1.4.3RSA Accelerator
•Other minor updates
Cont’d on next page
## Espressif Systems84
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Revision History
Cont’d from previous page
DateVersionRelease notes
## 2023-02v1.6
•Improved the content in the following sections:
–SectionProduct Overview
–Section2Pins
–Section4.1.3.5Power Management Unit (PMU)
–Section4.2.1.5Serial Peripheral Interface (SPI)
–Section5.1Absolute Maximum Ratings
–Section5.2Recommended Operating Conditions
–Section5.3VDD_SPI Output Characteristics
–Section5.5ADC Characteristics
•AddedESP32-S3 Consolidated Pin Overview
•Updated the notes in Section1ESP32-S3SeriesComparisonand Section
7Packaging
•Updated the effective measurement range in Table5-5ADC Characteris-
tics
•Updated the Bluetooth maximum transmit power
•Other minor updates
## 2022-12v1.5
•Removed the “External PA is supported” feature from SectionFeatures
•Updated the ambient temperature for ESP32-S3FH4R2 from40∼105
°C to40∼85 °C
•Added two notes in Section7
## 2022-11v1.4
•Added the package information for ESP32-S3FH4R2 in Section7
•AddedESP32-S3SeriesSoCErratain Section
•Other minor updates
## 2022-09v1.3
•Added a note about the maximum ambient temperature of R8 series chips
to Table1-1and Table5-2
•Added information about power-up glitches for some pins in Section2.2
•Added the information about VDD3P3 power pins to Table2.2and Sec-
tion
## 2.5.2
•Updated section4.3.3.1
•Added the fourth note in Table2-1
•Updated the minimum and maximum values of Bluetooth LE RF transmit
power in Section6.2.1
•Other minor updates
## 2022-07v1.2
•Updated description of ROM code printing in Section3
•Updated FigureESP32-S3 Functional Block Diagram
•Update Section5.6
•Deleted the hyperlinks inApplication
Cont’d on next page
## Espressif Systems85
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

## Revision History
Cont’d from previous page
DateVersionRelease notes
## 2022-04v1.1
•Synchronized eFuse size throughout
•Updated pin description in Table2-1
•Updated SPI resistance in Table5-3
•Added information about chip ESP32-S3FH4R2
## 2022-01v1.0
•Added wake-up sources for Deep-sleep mode
•Added Table3-4for default configurations of VDD_SPI
•Added ADC calibration results in Table5-5
•Added typical values when all peripherals and peripheral clocks are en-
abled to Table5-9
•Added more descriptions of modules/peripherals in Section4
•Updated FigureESP32-S3 Functional Block Diagram
•Updated JEDEC specification
•Updated Wi-Fi RF data in Section5.6
•Updated temperature for ESP32-S3R8 and ESP32-S3R8V
•Updated description of Deep-sleep mode in Table5-10
•Updated wording throughout
2021-10-12v0.6.1Updated text description
## 2021-09-30v0.6
•Updated to chip revision 1 by swapping pin 53 and pin 54 (XTAL_P and
## XTAL_N)
•Updated FigureESP32-S3 Functional Block Diagram
•Added CoreMark score in section Features
•Updated Section3
•Added data for cumulative IO output current in Table5-1
•Added data for Modem-sleep current consumption in Table5-9
•Updated data in section5.6,6.1, and6.2
•Updated wording throughout
## 2021-07-19v0.5.1
•Added “for chip revision 0” on cover, in footer and watermark to indicate
that the current and previous versions of this datasheet are for chip ver-
sion 0
•Corrected a few typos
2021-07-09v0.5Preliminary version
## Espressif Systems86
## Submit Documentation Feedback
ESP32-S3 Series Datasheet v2.2

Disclaimer and Copyright Notice
Information in this document, including URL references, is subject to change without notice.
ALL THIRD PARTY’S INFORMATION IN THIS DOCUMENT IS PROVIDED AS IS WITH NO WARRANTIES TO ITS AUTHENTICITY AND
## ACCURACY.
NO WARRANTY IS PROVIDED TO THIS DOCUMENT FOR ITS MERCHANTABILITY, NON-INFRINGEMENT, FITNESS FOR ANY PARTICULAR
PURPOSE, NOR DOES ANY WARRANTY OTHERWISE ARISING OUT OF ANY PROPOSAL, SPECIFICATION OR SAMPLE.
All liability, including liability for infringement of any proprietary rights, relating to use of information in this document is disclaimed. No
licenses express or implied, by estoppel or otherwise, to any intellectual property rights are granted herein.
The Wi-Fi Alliance Member logo is a trademark of the Wi-Fi Alliance. The Bluetooth logo is a registered trademark of Bluetooth SIG.
All trade names, trademarks and registered trademarks mentioned in this document are property of their respective owners, and are
hereby acknowledged.
Copyright © 2026 Espressif Systems (Shanghai) Co., Ltd. All rights reserved.
www.espressif.com

---

```
---
type: Plan
title: ESP32 → ESP32-S3 Migration Plan
description: Comprehensive migration plan for porting the firmware from classic ESP32 (Xtensa LX6) to ESP32-S3 (Xtensa LX7)
tags: [migration, esp32-s3, esp32, plan]
timestamp: 2026-07-06
status: pending
---
```

# ESP32 → ESP32-S3 Migration Plan

## Summary

Port the existing Rust/ESP-IDF v6 firmware from classic ESP32 (Xtensa LX6 dual-core, QFN48) to ESP32-S3 (Xtensa LX7 dual-core, QFN56). The S3 adds BLE 5.0, USB Serial/JTAG, split I/D cache, PIE SIMD extensions, and removes Bluetooth Classic. **Key blocking finding: GPIO25 does not exist on S3** — the STEP motor pin must be relocated.

### Chip comparison highlights

| Feature | ESP32 | ESP32-S3 |
|---|---|---|
| CPU | Xtensa LX6 dual-core @ 240 MHz | Xtensa LX7 dual-core @ 240 MHz (higher IPC, HW divide) |
| SRAM | 520 KB | 512 KB |
| DRAM | ~320 KB | ~360 KB |
| IRAM | ~192 KB | ~160 KB |
| Cache | 64 KB unified | Split I$ (16–32K) + D$ (32–64K) |
| RMT RAM | 512 × 32-bit (8 ch dynamic) | 384 × 32-bit shared (4 TX + 4 RX fixed) |
| USB Debug | External UART bridge only | Built-in USB Serial/JTAG (CDC-ACM + JTAG) |
| Bluetooth | 4.2 BR/EDR + BLE | 5.0 LE only (no Classic) |
| BLE PHY | 1 Mbps | 1 Mbps, 2 Mbps, Coded (125/500 Kbps) |
| WiFi | 802.11b/g/n, WPA2 | Same + WPA3, FTM, antenna diversity |
| ADC | 2 × 9 ch, 200 kSPS, 0–3900 mV @ DB_11 | 2 × 10 ch, 100 kSPS, 0–2900 mV @ DB_12 |
| GPIO | 34 (0–33, 25 exists) | 45 (0–21, 26–48, **25 absent**) |
| Strapping pins | 0, 2, 5, 12, 15 | 0, 3, 45, 46 |

---

## Steps / Execution log

### Phase 1: Build system & toolchain

| Step | File | Change | Risk |
|---|---|---|---|
| 1.1 | `rust-toolchain.toml` | `targets = ["xtensa-esp32s3-espidf", "x86_64-unknown-linux-gnu"]` | Critical |
| 1.2 | `.cargo/config.toml` | All `xtensa-esp32-espidf` → `xtensa-esp32s3-espidf`; `MCU = "esp32s3"` | Critical |
| 1.3 | `sdkconfig.defaults` | Add `CONFIG_IDF_TARGET="esp32s3"`; remove `CONFIG_ESP32_IRAM_AS_DRAM`, `CONFIG_BTDM_CTRL_MODE_BLE_ONLY` | Critical |
| 1.4 | `components_esp32.lock` | Delete (auto-regenerated by `idf.py reconfigure`) | Medium |
| 1.5 | `scripts/build.sh` | `TARGET="xtensa-esp32s3-espidf"`; default port `/dev/ttyACM0` | High |

Install the S3 target:
```bash
rustup target add xtensa-esp32s3-espidf
python3 $IDF_PATH/tools/idf_tools.py install esp32s3
idf.py reconfigure
```

### Phase 2: Source code — GPIO pin re-assignment

**GPIO25 does not exist on ESP32-S3** (datasheet §2, Figure 2-1). Must relocate `PIN_STEP`.

| Signal | Current GPIO | S3 Exists | S3 Priority | Action |
|---|---|---|---|---|
| STEP (RMT) | **25** | **NO** ❌ | — | Move to **GPIO21** (P2, free) |
| DIR | 26 | YES | P4 (SPI flash) | Keep — verify flash uses 1-line mode |
| EN | 27 | YES | P4 (SPI flash) | Keep — same caveat |
| LIMIT_FULL | 32 | YES | P4 (SPI flash) | Keep |
| LIMIT_EMPTY | 35 | YES | P3 (8-line SPI) | Keep — P3 acceptable |
| ADC (pH) | 34 | YES | P3 (8-line SPI) | Keep |
| DS18B20 | 33 | YES | P3 (8-line SPI) | Keep |
| LED | 2 | YES | P2 (free) | Keep — no longer a strapping pin on S3 ✅ |
| VALVE | 14 | YES | P2 (free) | Keep |

**Priority legend** (datasheet §2.3.5): P2 = free use; P3 = caution (strapping/USB/JTAG/UART/8-line SPI); P4 = flash/PSRAM interface.

### Phase 3: Source code — memory map constants

All addresses in `src/esp_safe.rs` must change (datasheet §4.1.2, Figure 4-1):

```
is_sane_sp():  0x3FFB_0000..0x4000_0000  →  0x3FC8_8000..0x3FF0_0000 [+ RTC fast/slow]
is_exec IRAM:  0x4000_0000..0x400C_2000  →  0x4037_0000..0x403E_0000
is_exec flash: 0x400D_0000..0x4040_0000  →  0x4200_0000..0x43FF_FFFF
UART0_BASE:    0x3FF4_0000               →  0x6000_0000
```

### Phase 4: Peripheral-specific changes

**RMT** (§4.2.1.11): 384 × 32-bit shared RAM (8 channels). Current 128-symbol chunks = 256 words (67%). Add assertion:
```rust
const _: () = assert!(RMT_CHUNK_MAX <= 192, "S3 RMT shared RAM = 384 words");
```

**ADC** (§4.2.2.1, Table 5-6): DB_12 range = 0–2900 mV (vs 0–3900 mV on ESP32). Sampling rate = 100 kSPS (vs 200 kSPS). Recalibrate `adc_cal` coefficients.

**BLE** (§4.3.3): No Bluetooth Classic. Remove `ESP_COEX_PREFER_BT` references. BLE 5.0 features are optional (2 Mbps PHY, Coded PHY, Advertising Extensions).

**WiFi** (§4.3.2): WPA3 available as optional upgrade. No breaking changes.

### Phase 5: Scripts & tools

| File | Change |
|---|---|
| `scripts/crash_analyzer.py:646,653` | `xtensa-esp32-espidf` → `xtensa-esp32s3-espidf` |
| `scripts/decode_backtrace.sh:8` | Same |
| `scripts/analyze_last_crash.sh:13` | Same |
| `scripts/serial_monitor.py:53` | Same |
| `scripts/pre_commit.sh:35,38` | Same |

### Phase 6: First build & smoke test

```bash
cargo build --target xtensa-esp32s3-espidf
cargo clippy --target xtensa-esp32s3-espidf -- -D warnings
```

Flash to ESP32-S3 dev board via USB Serial/JTAG (`/dev/ttyACM0` or `COMx`):
```bash
espflash flash --monitor target/xtensa-esp32s3-espidf/debug/ecotiter
```

### Phase 7: Validation

| Test | Acceptance |
|---|---|
| Boot without Guru Meditation | Serial output shows normal boot sequence |
| ADC readings on GPIO34 | Valid pH values, no saturation above 2900 mV |
| RMT stepper pulses on GPIO21 | Oscilloscope confirms correct pulse train |
| Limit switch ISR (GPIO32, GPIO35) | Flags set within expected latency |
| DS18B20 temperature (GPIO33) | Valid temperature readings |
| WiFi AP mode | Connect to SSID, get IP on 192.168.4.0/24 |
| BLE advertising | Discoverable with NUS service |
| HTTP REST API | All endpoints return valid JSON |
| WebSocket `/ws/stream` | Status events received |
| 30-min stability | No Guru Meditation, no WDT, no panics |

---

## Verification

### Build verification
- `scripts/build.sh` — 0 errors, 0 warnings
- `scripts/build.sh clippy` — 0 warnings, no `undocumented_unsafe_blocks` violations
- `scripts/build.sh test` — all host tests pass

### Hardware verification
- Flash to actual ESP32-S3 hardware
- Run smoke tests (Phase 7 table)
- Check stack watermarks (`uxTaskGetStackHighWaterMark`) for all threads
- Verify ADC calibration against known reference voltage
- 30-minute stability soak

### Safety rules verification (per AGENTS.md)
- [ ] GR-1: No blocking in main loop (verified: RMT moved to motor thread)
- [ ] GR-2: Every RMT motion accepts stop flag (verified: `move_steps_intervals` signature)
- [ ] GR-3: Init order = WiFi → HTTP → BLE (no change needed)
- [ ] GR-4: No `ESP_COEX_PREFER_BT` (removed from sdkconfig)
- [ ] GR-5: No stored C pointers across FFI boundary (no change needed)
- [ ] GR-6: Stack budgets verified (S3 slightly less SRAM but budget table still valid)
- [ ] GR-7: Diagnostic instrumentation present (ffi_guard, preconditions, stack_monitor, state_tracer, tick_watchdog)

---

## Files affected

### Critical changes (must fix, will break build or runtime)

| # | File | Change | Datasheet ref |
|---|---|---|---|
| 1 | `rust-toolchain.toml` | Target triple | — |
| 2 | `.cargo/config.toml` | Target triple + MCU env | — |
| 3 | `sdkconfig.defaults` | `CONFIG_IDF_TARGET`, remove ESP32 keys | — |
| 4 | `src/config.rs:20` | `PIN_STEP`: GPIO25 → GPIO21 | Figure 2-1, §2 |
| 5 | `src/esp_safe.rs:474` | `is_sane_sp()` DRAM range | Figure 4-1, lines 2548–2554 |
| 6 | `src/esp_safe.rs:481` | `is_executable()` IRAM range | Figure 4-1, lines 2559–2560 |
| 7 | `src/esp_safe.rs:483` | `is_executable()` flash cache range | Figure 4-1, lines 2563–2564 |
| 8 | `src/esp_safe.rs:565` | `UART0_BASE` | Figure 4-1, lines 2571–2572 |

### High priority (correctness or data loss)

| # | File | Change | Datasheet ref |
|---|---|---|---|
| 9 | `src/infrastructure/drivers/stepper.rs` | Add RMT 384-word assertion | §4.2.1.11, line 3468 |
| 10 | `src/infrastructure/drivers/adc.rs` | Verify DB_12 range (0–2900 mV), recalibrate | Table 5-6, line 3856 |
| 11 | `scripts/build.sh` | Target triple + default port | — |
| 12 | `scripts/crash_analyzer.py` | ELF path pattern | — |
| 13 | `scripts/decode_backtrace.sh` | ELF path | — |
| 14 | `scripts/analyze_last_crash.sh` | ELF path | — |
| 15 | `scripts/serial_monitor.py` | ELF path | — |
| 16 | `scripts/pre_commit.sh` | Target triple references | — |

### Medium priority (correctness, not safety-critical)

| # | File | Change | Datasheet ref |
|---|---|---|---|
| 17 | `components_esp32.lock` | Delete (auto-regenerate) | — |
| 18 | `sdkconfig.defaults` | Remove BT coex preference | §4.3.3 (BLE 5 only) |
| 19 | `docs/refs/project.md` | Memory map, pinout table | Figure 4-1, §2 |

### Low priority (documentation, optional features)

| # | File | Change | Datasheet ref |
|---|---|---|---|
| 20 | `Cargo.toml` | Review `esp32-nimble` S3 support | §4.3.3 |
| 21 | `build.rs` | Verify NimBLE/HAL patches work for S3 | — |

### Files with no changes required

- `src/lib.rs`, `src/main.rs`, `src/errors.rs`
- `src/motor_task.rs`, `src/temp_monitor.rs`
- `src/infrastructure/drivers/onewire.rs`, `limitswitch.rs`, `led.rs`, `valve.rs`
- `src/infrastructure/network/wifi.rs`, `http_server.rs`, `ble.rs`
- `src/infrastructure/storage/nvs.rs`
- `src/esp_mutex.rs`, `src/logger.rs`
- `src/interface/serial.rs`
- `src/diag/*`

---

## Related modules

- `docs/refs/project.md` — Hardware pinout and memory map (update target)
- `docs/esp_idf_v6/esp32-s3_datasheet_en.md` — This document (datasheet + migration plan)
- `AGENTS.md` — Project rules (GR-1 through GR-7)
- `docs/API/SERIAL_API.md` — Frozen API contract (verify no changes)
- `docs/API/HTTP_API.md` — REST API contract (verify no changes)

---

## Citations

[1] [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/documentation/esp32-s3_datasheet_en.pdf) — Sections referenced inline.
[2] [ESP32-S3 Technical Reference Manual](https://www.espressif.com/en/support/documents/technical-documents) — For UART0_BASE verification and peripheral register offsets.
[3] [ESP-IDF Migration Guides (ESP32 → ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/index.html) — IDF-level migration.
[4] [ESP-IDF Get Started (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) — Toolchain setup.
[5] [XTensa LX7 Instruction Set](https://www.cadence.com/en_US/home/tools/ip/tensilica-ip.html) — HW divide, MIN/MAX, SEXT instructions (compiler-managed).