# Human vs. Computer Smart Checkers Board

A physical game of Checkers powered by an Arduino Nano Every. The board features a physical game board with real checkers pieces that have magnets glued to the bottom.  The game electronic game board has magnetic piece detection, LED lighting for move guidance, voice feedback, and an embedded CPU opponent with 7 difficulty levels.

This was built in Dec 2025.  I did not intend on making this a publicly avaiable step-by-step project so there are key things missing right now
such as schematics, diagrams, and some of the specific resistor and capacitor values.  If you're serious buliding this project, you can contact
me and I'll try to get you more detailed information.  As we get future away from Dec 2025 though my memory will also get futher away.

## Features
* **7 game play levels:** Ranges from Random (Level 1) to Grandmaster (Level 7).
    * *Level 7:* Uses Minimax, Alpha-Beta pruning, Quiescence search, opening books, and dynamic depth scaling.
* **Voice Feedback:** A DFPlayer Mini provides sound effects and spoken commentary for turns, errors, and game states ("Your turn", "King me", "You have a mandatory jump!").
* **Interactive Lighting:** LEDs under every square highlight valid moves, captures, and board errors.  When the human player picks up a piece
the LED lights indicate valid places the piece can go to.  On the CPU's turn the square that it wants to move from lights up and once you pick it
up, the destination square will light up.
* **Magnetic Sensing:** Uses Hall effect sensors and two MCP23017 I/O expanders to read detect piece movement.
* **Smart Power:** Auto-sleep mode turns off lights and audio after 5 minutes of inactivity.
* **Robust Error Handling:** Detects if pieces are placed incorrectly and vocally guides the user to fix the board.
* **Rules Handling:** Supports kings when the back row is reached.  Supports the mandatory jump rule.

## Hardware
* **Microcontroller:** Arduino Nano Every (the regular Nano does not have enough RAM)
* **I/O Expansion:** 2x MCP23017
* **Audio:** DFPlayer Mini + Speaker
* **LEDs:** 32x WS2812B (NeoPixels)
* **Sensors:** 32x Reed Switches / Hall Effect Sensors
* **Buttons:** 2x momentary push buttons
* **Power:** On/Off toggle switch
* **Misc:** Capacitors, resistors, wire, acrylic sheet, wood

## How to Play
1.  **Select Level:** Press the Level button (Pin 3) to cycle difficulty (1-7).
2.  **Start Game:** Press the Start/Recover button (Pin 4).
3.  **Playing:** Lift a piece to see valid moves (green lights). Place the piece to confirm.
4.  **Errors:** If you make an illegal move or violate a mandatory jump, the board will flash purple and correct you vocally.
