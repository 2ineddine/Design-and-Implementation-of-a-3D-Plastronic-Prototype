# 3D Plastronic Prototype with Arduino Control

## Project Overview
This project focuses on the design and implementation of an interactive electronic prototype that combines 3D plastronics with capacitive touch sensing. It consists of two functional 3D-printed plastic shells:

- A car shell equipped with programmable RGB LEDs
- A remote control shell featuring capacitive touch buttons using the MPR121 sensor

Both shells interface with an Arduino, enabling dynamic lighting effects triggered by user interactions through the touch interface.

## Objectives

- Design the electronic schematic and PCB layout using KiCad
- Integrate capacitive touch sensing via the MPR121 component
- Control RGB lighting effects using SK6812 addressable LEDs
- Manufacture two interactive shells using 3D printing: one for the car, one for the controller
- Demonstrate full system interaction through Arduino-based logic

## Components Used

| Component         | Description                          |
|-------------------|--------------------------------------|
| MPR121            | Capacitive touch sensor (I2C)        |
| SK6812 RGB LEDs   | Individually addressable LEDs        |
| Resistors         | 0805 and 1206 packages               |
| Capacitors        | 0805 decoupling capacitors           |
| Arduino           | Microcontroller (e.g., UNO or Nano)  |
| 3D-printed shells | Car and remote control enclosures    |

## Tools and Software

- **KiCad**: For schematic capture and PCB layout
- **Arduino IDE**: For programming and uploading firmware
- **3D design software**: e.g., OpenSCAD or Fusion 360 for shell modeling
- **3D printer**: For producing the physical enclosures

## Project Structure

```
/ProjectFolder
├── code/                    # Arduino sketches for controlling LEDs and input logic
├── the_car_shell_model/    # STEP file of the car shell
├── file_of_prototypes/     # 3D models of the electronic boards and routing
└── detailed_report/        # Complete technical report in PDF format
```

## Example Features

- Touch input on the remote triggers LED animations on the car shell
- Multiple lighting modes: blink, fade, rainbow
- Modular and adaptable Arduino code


## References

- MPR121 Datasheet – Adafruit Guide  
- KiCad Documentation – https://docs.kicad.org  
- SK6812 RGB LED Datasheet

## Author

- Project by: 2ineddine  
- Date: May 2024  
- Academic Level: Bachelor's in Electronics and Embedded Systems

## License

This project is intended for educational and demonstration purposes only.  
Feel free to reuse and adapt with proper credit.
