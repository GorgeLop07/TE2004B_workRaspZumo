# Zumo 2040 route follower

Pico C firmware for one camera-guided lap of the ordered route
`P14 -> P1 -> P2 -> ... -> P13 -> P14`. Camera pose is received over CAN
(`0x101`, 8-byte `lp_can.h` packet); line sensors center the robot on straight
sections and guide it around the seven physical curves.

## Hardware and build

- Target board: Raspberry Pi Pico on the Zumo 2040.
- CAN: MCP2515 over SPI0 using the same pin mapping and driver as the existing
  `ZumoLocalPositioningCAN` example (MISO 0, CS 1, SCK 2, MOSI 3). INT uses Pico
  physical header pin 16 (GPIO12).
- Install/initialize the official Pololu `zumo-2040-robot` repository at
  `zumo-2040-robot/`, including its `c/pololu_zumo_2040_robot` library. Or pass
  `-DZUMO_ROBOT_LIBRARY_DIR=/absolute/path/to/c/pololu_zumo_2040_robot` to CMake.
- Configure with the Raspberry Pi Pico SDK, then build the
  `ZumoRouteFollower` target. CMake checks that the Pololu library is available
  before configuring.

The MCP2515 driver and `lp_can.h` were copied from the existing Zumo CAN
receiver example. CAN data is big-endian signed x/y millimeters and angle in
tenths of degrees, followed by validity/freshness bits and a camera sequence
byte. Both CAN arrival timeout and camera sequence progress timeout are checked;
repeated CAN frames do not count as new camera measurements.

## Required track survey before motion

The route is deliberately disabled by default. In `route_config.h`:

1. Survey and enter all P1-P14 x/y coordinates in the camera's coordinate
   frame. The start heading is derived from P14 to P1; live camera heading is
   used on straight sections, while line sensors steer through curves.
2. Record the coordinate origin, axis directions, angle zero and sign, and
   marker-to-robot-center offset in `ROUTE_COORDINATE_NOTES`. The code currently
   assumes angle zero is +X and positive is counterclockwise; update
   `pose_to_math_angle()` if the surveyed camera convention differs.
3. Confirm the marker pose represents robot center, or account for its offset.
4. Confirm line polarity. The current setting assumes the calibrated readings
   are larger over the dark line.
5. Only then set `ROUTE_CALIBRATED` to `1`.

Zeroed waypoint coordinates are placeholders, not estimates from the route
drawing. A start is refused unless the route is enabled, line sensors have been
calibrated, the pose and sequence are fresh, and the robot is near P14 facing
toward P1.

## On-robot procedure

1. Power on the Zumo and open its USB serial terminal. It starts printing live
   CAN frames and decoded pose values immediately. Type `c` for a CAN test TX.
2. Type `a` to begin line calibration. Move the robot so each sensor sees both the line and the
   surrounding surface, then type `a` again to finish.
   Physical button A works too.
3. Place the robot at P14 facing P1. Type `b` to request start; physical button B works too.
   Pose data must be fresh.
4. The robot stops after the ordered final P14 capture. A pose timeout stops
   both motors and resumes the current waypoint only when fresh sequence data
   returns. On straight sections, line loss allows up to 350 ms of camera and
   encoder guided recovery; curves and longer losses stop the robot.

While the terminal monitor is active, press C to send one diagnostic CAN frame.
The Zumo prints every received frame as `RX`, decoded pose frames as `RX POSE`,
and its diagnostic frame as `TX` over USB serial.
The diagnostic uses standard CAN ID `0x102` with payload `5A 55 <sequence> A5`;
the route pose remains on ID `0x101`. A failed `TX` reports that the MCP2515
could not complete transmission, commonly because no other CAN node ACKed it.
The Pico 2 W sender already logs its pose frames as `TX` over its own USB serial.

The route firmware reads the Zumo's built-in quadrature encoders on GP12/13
(left) and GP8/9 (right). Disconnect the MCP2515 INT wire from GP12: the CAN
driver polls over SPI and does not need INT. During a run, `enc=...` reports
encoder count changes and `trim=...` reports the wheel speed feedback applied
to each motor. The initial encoder scale and gains in `route_config.h` may need
tuning for the motor and battery setup.

For troubleshooting, the Zumo startup log must show `CANSTAT after reset=0x80`.
The sender uses SPI0 MISO/CS/SCK/MOSI GPIO16/17/18/19 and INT GPIO20; this Zumo
firmware uses GPIO0/1/2/3 and polls CAN without INT. If the sender prints valid
`TX ID=0x101` data but the Zumo does not print matching `RX ID=0x101` data,
check the CAN transceiver wiring, CANH/CANL, shared ground, termination, and
500 kbps bitrate. If Zumo CANSTAT is not `0x80`, troubleshoot its SPI wiring,
CS, power, and pin mapping first.

The controller limits motor commands to the configured supported range.
Serial diagnostics include state, target waypoint, sequence, progress,
cross-track error, line error, encoder deltas, encoder trim, and motor commands.
