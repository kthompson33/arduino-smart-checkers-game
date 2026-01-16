/*
 * ======================================================================================
 * PROJECT: Arduino Smart Checkers Board (Voice & AI Enabled)
 * ======================================================================================
 * * DESCRIPTION:
 * This sketch powers a fully automated, physical checkers board. A person can play a game of
 * Checkers against the computer which has 7 levels.  The game board I created was hand-made
 * with a piece of particle board as the bottom, a thin sheet of plastic on top onto which
 * red and black squares were painted.  The eletronics for the 32 active squares were between the 
 * particle board and the plastic top sheeet.  To the left of the board I build a wooden box that 
 * contained the other electronic components such as the Arduino, MP3 player, speaker, I/O expansion
 * boards, and power management components.  I bought 1.5" diameter checker pieces and hot glued
 * 1" diameter magnets to the bottom of each piece. It detects piece 
 * movement using magnetic sensors, provides visual guidance via NeoPixel LEDs under 
 * the board, and offers vocal feedback using a DFPlayer Mini. The system features 
 * a robust AI opponent with multiple difficulty levels, ranging from random moves 
 * to a "Grandmaster" level utilizing Minimax, Alpha-Beta pruning, Quiescence search, 
 * dynamic depth scaling, and an opening book.
 *
 * * MAJOR FUNCTIONS:
 * 1. Game State Machine: Manages turns, move validation, and game flow (Pickup -> Placement -> Capture).
 * 2. Board Sensing: Reads 32 magnetic sensors via two I/O expanders to track physical piece locations.
 * 3. AI Engine: Calculates optimal moves using recursive search algorithms and heuristic evaluation.
 * 4. User Interface: Controls LED effects for valid moves/errors and plays voice clips for events.
 * 5. Rule Enforcement: Enforces mandatory jumps, King promotion, and turn order.
 * 6. Power Management: Enters a low-power sleep mode after inactivity.
 *
 * * HARDWARE COMPONENTS:
 * - Microcontroller: Arduino Nano Every (or compatible)
 * - Sensors: 32x Reed Switches or Hall Effect Sensors (one per square)
 * - I/O Expansion: 2x MCP23017 I2C Port Expanders (Address 0x20 and 0x21)
 * - Lighting: Addressable NeoPixel LEDs (WS2812B), one per square (only 32 squares are used in the game of Checkers)
 * - Audio: DFPlayer Mini MP3 Player + Speaker
 * - Inputs: 2x Push Buttons (Level Select on D3, Recover/Start on D4)
 *
 * * REQUIRED LIBRARIES:
 * - Wire.h (I2C Communication)
 * - Adafruit_NeoPixel.h (LED Control)
 * - Adafruit_MCP23X17.h (Sensor Reading)
 * - SoftwareSerial.h (Communication with DFPlayer)
 * - DFRobotDFPlayerMini.h (Audio Control)
 * * ======================================================================================
 */

// --- Libraries ---
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_MCP23X17.h>
#include <SoftwareSerial.h>       // For DFPlayer communication
#include <DFRobotDFPlayerMini.h>  // For DFPlayer control

// --- Game and Hardware Definitions ---
#define LED_PIN 6             // Arduino pin connected to the NeoPixel data input.
#define NUM_SQUARES 32        // Total number of sensors/pixels (4 squares x 8 rows)
#define PLAYABLE_ROWS 8       // Total rows on the checkerboard (0 to 7)
#define MCP_INT_PIN 2         // Arduino pin D2 connected to the INTA pins of BOTH MCPs (using mirroring).
#define DEBOUNCE_DELAY 50     // Milliseconds for debouncing reed switch/magnet bounce.
#define PINS_PER_MCP 16       // Constant for pins per expander (0-15)
#define AI_MOVE_PAUSE 1000    // Pause duration before prompting for AI move 

// --- Button Definitions ---
#define LEVEL_BUTTON_PIN 3    // Pin D3 for the difficulty selection button
#define RECOVER_BUTTON_PIN 4  // Pin D4 for the start game and error recovery button
#define BUTTON_DEBOUNCE_DELAY 50 // Debounce time in ms for buttons
#define MAX_AI_LEVEL 7        // Maximum available AI difficulty level

// --- Piece Definitions ---
const byte EMPTY_SQUARE = 0;
const byte AI_PIECE = 1;              // AI pieces start on Rows 5-7 (move toward 0)
const byte PLAYER_PIECE = 2;          // Player pieces start on Rows 0-2 (move toward 7)
const byte KING_MODIFIER = 10;        // Added to piece type to denote a king (e.g., AI_KING = 11)
const byte AI_KING = (AI_PIECE + KING_MODIFIER);
const byte PLAYER_KING = (PLAYER_PIECE + KING_MODIFIER);

const byte MP3_VOLUME = 25;           // Default volume for DFPlayer (0-30)

// --- Sleep Mode Definitions ---
#define SLEEP_TIMEOUT 300000  // Timeout duration: 5 minutes (in milliseconds)
unsigned long lastInputTime = 0; // Tracks the timestamp of the last user activity
bool isSleeping = false;      // Flag to track if the system is currently in sleep mode

// --- Data Structures ---

// Represents a single move on the board
struct Move {
  int8_t startPin = -1;       // The pin/square the piece is moving FROM
  int8_t endPin = -1;         // The pin/square the piece is moving TO
  int8_t capturedPin = -1;    // The pin/square of the captured piece (-1 if non-jump move)
};

// Container for all valid moves available to the player
struct ValidDestinations {
    Move moves[16];           // Array to store up to 16 potential moves
    int count = 0;            // Current number of valid moves found
};

// --- Class Instances ---
Adafruit_NeoPixel strip(NUM_SQUARES, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_MCP23X17 mcp_a;      // Address 0x20 (Default) - Handles Pins 0-15
Adafruit_MCP23X17 mcp_b;      // Address 0x21 (A0 wired to VCC) - Handles Pins 16-31

// --- DFPlayer Instances ---
SoftwareSerial mySoftwareSerial(10, 11); // RX on 10, TX on 11 for DFPlayer
DFRobotDFPlayerMini myDFPlayer;

// --- Color Definitions (NeoPixel) ---
const uint32_t PLAYER_COLOR = strip.Color(255, 0, 0);     // Red for Player pieces
const uint32_t AI_COLOR = strip.Color(255, 255, 0);       // Yellow for AI pieces
const uint32_t START_COLOR = strip.Color(255, 100, 0);    // Orange for pick up location
const uint32_t END_COLOR = strip.Color(0, 255, 0);        // Green for placement location
const uint32_t INCORRECT_COLOR = strip.Color(255, 255, 255); // White for error feedback
const uint32_t CAPTURE_COLOR = strip.Color(255, 0, 100);  // Pink for captured piece removal prompt
const uint32_t WIN_COLOR = strip.Color(255, 255, 0);      // Yellow for win animation
const uint32_t BOARD_CORRECTION_COLOR = strip.Color(128, 0, 128); // Purple for board errors

// Array of colors for the win animation sequence
const uint32_t WIN_SEQUENCE_COLORS[] = {
  strip.Color(255, 0, 0),     // Red
  strip.Color(255, 165, 0),   // Orange
  strip.Color(255, 255, 0),   // Yellow
  strip.Color(0, 255, 0),     // Green
  strip.Color(0, 0, 255),     // Blue
  strip.Color(128, 0, 128),   // Purple
  strip.Color(0, 255, 255),   // Cyan
  strip.Color(255, 192, 203)  // Pink
};
const int NUM_WIN_COLORS = 8; // Size of the color array

// --- Game State Variables ---
enum GameState { 
  INITIALIZING,                       // System startup
  IDLE,                               // Waiting for turn logic to begin
  WAITING_FOR_HUMAN_PICKUP,           // Human turn: Waiting for player to lift a piece
  HUMAN_PICKUP_VALIDATED,             // Human turn: Valid piece lifted, showing destinations
  AWAITING_HUMAN_DOUBLE_JUMP_PICKUP,  // Human turn: Multi-jump sequence active
  AI_VALIDATION_PICKUP,               // AI turn: Waiting for human to move AI piece
  AI_VALIDATION_PLACEMENT,            // AI turn: Waiting for human to place AI piece
  AWAITING_CAPTURE_REMOVAL,           // Capture: Waiting for human to remove captured piece
  AWAITING_BOARD_CORRECTION,          // Error: Board state does not match memory
  AWAITING_LEVEL_SELECTION,           // Setup: Waiting for difficulty selection
  GAME_OVER                           // Game finished
};

GameState gameState = INITIALIZING;
GameState previousGameState = IDLE;   // Stores state to return to after error correction
bool aiMoveChosen = false;            // Flag to ensure AI calculates only one move per turn

enum PlayerTurn { HUMAN_PLAYER, AI_OPPONENT };
PlayerTurn currentTurn = HUMAN_PLAYER;

Move currentMove;                     // Stores the specific move being processed
ValidDestinations legalMoves;         // Stores all legal moves for the human player's current turn
int errorPin = -1;                    // Tracks the specific pin causing a board error
byte boardState[NUM_SQUARES];         // Array representing the logical state of the board

// --- Mandatory Jump Enforcement ---
int mandatoryJumpPins[12];            // List of pieces that are required to jump
int mandatoryJumpCount = 0;           // Count of pieces with mandatory jumps

// --- AI Difficulty Setting ---
int aiDifficultyLevel = 0;

// --- Audio State Variables ---
bool isSoundPlaying = false;          // Flag to prevent overlapping audio commands
bool playKingSoundAfterCapture = false; // Flag to delay "King Me" sound until capture is resolved

bool isGameStarting = true;           // Flag to track if the game is in initial setup phase

// --- Global Error Tracking ---
int boardErrorPins[NUM_SQUARES];      // Stores list of pins that are physically incorrect
int boardErrorCount = 0;              // Count of current physical errors

// --- Volatile Variables (Interrupt Context) ---
volatile bool interruptFlag = false;  // Set true by hardware interrupt when sensor state changes
unsigned long lastFlashTime = 0;      // Timer for non-blocking LED flashing
bool flashOn = false;                 // State for LED flashing effect

// --- Button State Variables ---
int levelButtonState = HIGH;          // Stable state for level button
int lastLevelButtonReading = HIGH;    // Raw reading for level button (for debouncing)
unsigned long lastLevelDebounceTime = 0; // Debounce timer for level button
unsigned long aiThinkingTimer = 0;    // Timer for "thinking" sound effects
int aiThinkingSoundCount = 0;         // Counter to vary "thinking" voice clips

int recoverButtonState = HIGH;        // Stable state for recover button
int lastRecoverButtonReading = HIGH;  // Raw reading for recover button
unsigned long lastRecoverDebounceTime = 0; // Debounce timer for recover button

// --- Function Prototypes ---
void handleInterrupt();
void initialFadeSequence();
void setInitialBoard();
void generateAIMove();
int readSensorState(int globalPin);
void readCurrentPhysicalState();
void handleInterruptEvent();
void handleHumanPickup(byte pinIndex, int sensorValue);
void handleHumanPlacement(int pinIndex, int sensorValue);
void handleAIPickup(int pinIndex, int sensorValue);
void handleAIPlacement(int pinIndex, int sensorValue);
void handleCaptureRemoval(int pinIndex, int sensorValue);
void handleBoardCorrection(int pinIndex, int sensorValue);
void handleBoardCorrectionFlashing();
void clearAllLEDs();
void flashPossibleDestinations();
void runWinAnimation();
int checkWinCondition();
int pinToRow(int globalPin);
int pinToCol(int globalPin);
int rowColToPin(int row, int col);
int findAllMoves(byte startPin, Move movesList[], int startIndex);
void updateLEDsToBoardState();
int getLastInterruptedPin();
void checkLevelButton();
void checkRecoverButton();
void playSound(int folder, int file);
void playUrgentSound(int folder, int file);
void playBlockingSound(int folder, int file);
void printDetail(byte type, int value); 
long evaluateBoard(); 
long evaluateBoard_Advanced(); 
long evaluateBoard_Grandmaster(); 
long minimax(int depth, bool isMaximizingPlayer, long alpha, long beta); 
long quiescence(bool isMaximizingPlayer, long alpha, long beta);
void findMandatoryJumps();
void clearAllMCPInterrupts();


// =======================================================
// SETUP & LOOP
// =======================================================

// Initial setup of hardware pins, I2C, and peripherals
void setup() {
  // Wait for all hardware (MCPs, DFPlayer) to stabilize on power-on
  delay(1500);
  Serial.begin(9600); 
  while (!Serial && millis() < 2000); // Wait for Serial (but not forever)
  Serial.println("Checkerboard Full Game Logic - Program Start.");
  
  // --- Initialize Random Seed ---
  // Use unconnected analog pins to generate noise for true randomness
  randomSeed(analogRead(A0) ^ analogRead(A1));

  // --- Initialize I2C and MCPs ---
  Wire.begin();
  delay(100);
  Wire.setClock(100000L); 
  if (!mcp_a.begin_I2C(0x20)) {
    Serial.println("MCP A (0x20) not found! Check wiring."); while (1);
  }
  if (!mcp_b.begin_I2C(0x21)) {
    Serial.println("MCP B (0x21) not found! Check A0 wiring and address."); while (1);
  }

  // Configure ALL 32 sensor pins across both MCP chips
  for (int i = 0; i < NUM_SQUARES; i++) {
    Adafruit_MCP23X17& current_mcp = (i < PINS_PER_MCP) ? mcp_a : mcp_b;
    int mcp_pin = (i < PINS_PER_MCP) ? i : (i - PINS_PER_MCP);
    current_mcp.pinMode(mcp_pin, INPUT);
    current_mcp.setupInterruptPin(mcp_pin, CHANGE);
    delay(1);
  }

  // Configure MCP interrupt output (Open-Drain, Active Low)
  mcp_a.setupInterrupts(true, false, LOW);
  mcp_b.setupInterrupts(true, false, LOW);
  pinMode(MCP_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MCP_INT_PIN), handleInterrupt, CHANGE);
  
  // --- Initialize Button Pins ---
  pinMode(LEVEL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RECOVER_BUTTON_PIN, INPUT_PULLUP);

  // --- Initialize DFPlayer ---
  delay(1000); // Waiting for USB noise to settle
  mySoftwareSerial.begin(9600); // Start serial for DFPlayer
  Serial.println(F("Initializing DFPlayer ... (Attempting for 5 seconds)"));
  unsigned long startTime = millis();
  bool dfPlayerOnline = false;

  while (millis() - startTime < 5000) { // Try connection loop for up to 5 seconds
    if (myDFPlayer.begin(mySoftwareSerial, false)) { // false = No ACK to prevent freezing
      dfPlayerOnline = true;
      Serial.println(F("DFPlayer Mini online."));
      myDFPlayer.volume(MP3_VOLUME); // Set volume
      break; 
    }
    delay(500); 
  }

  // If the loop finished without the player coming online
  if (!dfPlayerOnline) {
    Serial.println(F("Unable to begin DFPlayer (timeout):"));
    // We continue anyway, as it might wake up later.
  }

  // Initialize the NeoPixel strip
  strip.begin();
  strip.show();
  
  // Initialize internal board logic to starting positions
  setInitialBoard();

  // --- Hardware Stability Reset (Run for BOTH MCPs) ---
  clearAllMCPInterrupts();
  
  // --- STARTUP SEQUENCE ---
  
  // 1. Run visual startup sequence (LED wipe)
  initialFadeSequence();
  
  // 2. Check for invalid piece locations and wait for them all to be resolved
  while(true) {
    readCurrentPhysicalState(); // Scan the board for mismatch
    
    if (boardErrorCount > 0) {
      handleBoardCorrectionFlashing(); // Flash error lights
      
      // We must manually process interrupts and sound here since we are not in the main loop yet
      if (myDFPlayer.available()) {
        byte type = myDFPlayer.readType();
        int value = myDFPlayer.read();
        if (type == DFPlayerPlayFinished) isSoundPlaying = false;
        printDetail(type, value);
      }
      if (interruptFlag) {
        delay(DEBOUNCE_DELAY);
        int pinIndex = getLastInterruptedPin();
        int sensorValue = readSensorState(pinIndex);
        if(pinIndex != -1) {
          handleBoardCorrection(pinIndex, sensorValue); // This will decrement boardErrorCount
        }
        interruptFlag = false;
      }
    } else {
      break; // Board is clean, exit loop
    }
  } // End of board validation loop

  clearAllLEDs();
  strip.show();
  isSoundPlaying = false; // Clear any sound flags

  // 3. Play startup sound (blocking)
  playBlockingSound(1, 1); // Play startup sound effect

  // 4. Instruct user to select level and press Start (blocking)
  playBlockingSound(5, MAX_AI_LEVEL);
  // "Use the green button to select a level from 1 to x, then the yellow button to begin."
  
  gameState = AWAITING_LEVEL_SELECTION;
  
  // Start the sleep timer
  lastInputTime = millis();
  
  // Give the hardware time to settle before the main loop starts
  delay(250);
}

// Main program loop
void loop() {
  // --- Check for button presses ---
  checkLevelButton();
  checkRecoverButton();
  
  // --- Check for DFPlayer messages ---
  if (myDFPlayer.available()) {
    byte type = myDFPlayer.readType();
    int value = myDFPlayer.read();     
    if (type == DFPlayerPlayFinished) {
      isSoundPlaying = false;
    }
    printDetail(type, value); 
  }

  // 2. Check Interrupts (Movements wake up the game from sleep)
  if (interruptFlag) {
    resetSleepTimer(); // Wake up on piece move
    
    if (gameState != AWAITING_BOARD_CORRECTION) {
      delay(DEBOUNCE_DELAY);
      handleInterruptEvent();
      interruptFlag = false;
    }
  }

  // 3. Sleep Mode Logic
  if (!isSleeping && (millis() - lastInputTime > SLEEP_TIMEOUT)) {
    enterSleepMode();
  }
  
  if (isSleeping) {
    return; // Stop execution here if sleeping
  }

  // If waiting for level selection, do nothing else; buttons handle the state change
  if (gameState == AWAITING_LEVEL_SELECTION) {
    return; 
  }

  // Main IDLE state: Check for game conditions, wins, and turn management
  if (gameState == IDLE) {
    readCurrentPhysicalState(); // Ensure board is valid
    if (gameState == AWAITING_BOARD_CORRECTION) {
        return;
    }

    int winner = checkWinCondition();
    if (winner != 0) {
        if (winner == PLAYER_PIECE) {
          // --- Randomly select a win message from Folder 03 ---
          int randomWinFile = random(1, 14);
          playUrgentSound(3, randomWinFile);
          delay(500); 
          //playSound(1, 16); // Play win SFX (Optional)
        } else {
          // Randomly select an AI win message from Folder 04
          int randomWinFile = random(1, 11);
          playUrgentSound(4, randomWinFile);
        }
        gameState = GAME_OVER;
        clearAllLEDs();
        runWinAnimation(); 
        return;
    }

    if (currentTurn == HUMAN_PLAYER) {
      // Find all mandatory jumps BEFORE waiting for pickup
      findMandatoryJumps();
      
      gameState = WAITING_FOR_HUMAN_PICKUP;
      currentMove.startPin = -1;
      currentMove.endPin = -1;
      currentMove.capturedPin = -1;
      legalMoves.count = 0;
    } else { // AI_OPPONENT turn
      delay(AI_MOVE_PAUSE);
      Serial.println("\n--- AI TURN: Calculating move. ---");
      // Only generate a *new* move if one hasn't been chosen for this turn.
      if (!aiMoveChosen) {
        generateAIMove();
        aiMoveChosen = true; // Flag that we have our move for this turn
      }
      if (currentMove.startPin != -1) {
        gameState = AI_VALIDATION_PICKUP;
      } else {
        // No valid moves found for AI implies loss, usually caught by checkWinCondition
        gameState = GAME_OVER;
        clearAllLEDs();
        runWinAnimation();
        return;
      }
    }
  }

  if (gameState == GAME_OVER) {
      return;
  }

  // Handle lingering interrupts if not in error correction mode
  if (interruptFlag && gameState != AWAITING_BOARD_CORRECTION) {
    delay(DEBOUNCE_DELAY);
    handleInterruptEvent();
    interruptFlag = false;
  }

  // --- Flashing Logic (Non-blocking) ---
  // Flashes valid moves for human
  if (gameState == HUMAN_PICKUP_VALIDATED || gameState == AWAITING_HUMAN_DOUBLE_JUMP_PICKUP) { 
    flashPossibleDestinations();
  }
  // Flashes AI source/destination
  if (gameState == AI_VALIDATION_PICKUP || gameState == AI_VALIDATION_PLACEMENT) {
    if (millis() - lastFlashTime > 250) {
      flashOn = !flashOn;
      clearAllLEDs();
      if (flashOn) {
        if (gameState == AI_VALIDATION_PICKUP) strip.setPixelColor(currentMove.startPin, AI_COLOR);
        if (gameState == AI_VALIDATION_PLACEMENT) {
            strip.setPixelColor(currentMove.endPin, END_COLOR);
            if (currentMove.capturedPin != -1) strip.setPixelColor(currentMove.capturedPin, CAPTURE_COLOR);
        }
      }
      strip.show();
      lastFlashTime = millis();
    }
  }
  // Flashes captured piece to be removed
  if (gameState == AWAITING_CAPTURE_REMOVAL) {
    if (millis() - lastFlashTime > 250) {
      flashOn = !flashOn;
      clearAllLEDs();
      if (flashOn) {
        strip.setPixelColor(currentMove.capturedPin, CAPTURE_COLOR);
      } else {
        clearAllLEDs();
      }
      strip.show();
      lastFlashTime = millis();
    }
  }
  // Flashes error correction pins
  if (gameState == AWAITING_BOARD_CORRECTION) {
    handleBoardCorrectionFlashing();
    if (interruptFlag) {
        delay(DEBOUNCE_DELAY);
        handleInterruptEvent();
        interruptFlag = false;
    }
  }
}

// =======================================================
// WIN CONDITION AND ANIMATION
// =======================================================

// Scans board to check if one side has 0 pieces remaining
int checkWinCondition() {
    int playerCount = 0;
    int aiCount = 0;
    for (int pin = 0; pin < NUM_SQUARES; pin++) {
        if (boardState[pin] == PLAYER_PIECE || boardState[pin] == PLAYER_PIECE + KING_MODIFIER) {
            playerCount++;
        } else if (boardState[pin] == AI_PIECE || boardState[pin] == AI_PIECE + KING_MODIFIER) {
            aiCount++;
        }
    }
    if (aiCount == 0) return PLAYER_PIECE;
    if (playerCount == 0) return AI_PIECE;
    return 0; // Game continues
}

// Plays a color-cycling animation on victory
void runWinAnimation() {
    const int CYCLE_DELAY = 50;
    int colorIndex = 0;
    for (int fullCycle = 0; fullCycle < 2; fullCycle++) {
        for (int rep = 0; rep < 2; rep++) {
            for (int row = 0; row < PLAYABLE_ROWS; row++) {
                clearAllLEDs();
                uint32_t currentColor = WIN_SEQUENCE_COLORS[colorIndex % NUM_WIN_COLORS];
                colorIndex++;
                for (int col = 0; col < 8; col++) {
                    int pin = rowColToPin(row, col);
                    if (pin != -1) strip.setPixelColor(pin, currentColor);
                }
                strip.show();
                delay(CYCLE_DELAY * 2);
                clearAllLEDs();
                strip.show();
                delay(CYCLE_DELAY);
            }
        }
        for (int rep = 0; rep < 2; rep++) {
            for (int col = 0; col < 8; col++) {
                clearAllLEDs();
                uint32_t currentColor = WIN_SEQUENCE_COLORS[colorIndex % NUM_WIN_COLORS];
                colorIndex++;
                for (int row = 0; row < PLAYABLE_ROWS; row++) {
                    int pin = rowColToPin(row, col);
                    if (pin != -1) strip.setPixelColor(pin, currentColor);
                }
                strip.show();
                delay(CYCLE_DELAY * 2);
                clearAllLEDs();
                strip.show();
                delay(CYCLE_DELAY);
            }
        }
    }
}


// =======================================================
// INTERRUPT & COMMUNICATION
// =======================================================

// Hardware interrupt service routine (ISR)
void handleInterrupt() {
  interruptFlag = true;
}

// Reads the digital state of a specific sensor pin (0-31)
int readSensorState(int globalPin) {
    Adafruit_MCP23X17& source_mcp = (globalPin < PINS_PER_MCP) ? mcp_a : mcp_b;
    int source_mcp_pin = (globalPin < PINS_PER_MCP) ? globalPin : (globalPin - PINS_PER_MCP);
    return source_mcp.digitalRead(source_mcp_pin);
}

// Identifies which pin caused the interrupt from either MCP chip
int getLastInterruptedPin() {
    int mcp_pin_a = mcp_a.getLastInterruptPin();
    delay(1);
    int mcp_pin_b = mcp_b.getLastInterruptPin();
    if (mcp_pin_a >= 0 && mcp_pin_a < PINS_PER_MCP) {
        return mcp_pin_a;
    }
    
    if (mcp_pin_b >= 0 && mcp_pin_b < PINS_PER_MCP) {
        return mcp_pin_b + PINS_PER_MCP;
    }
    return -1;
}

// =======================================================
// INTERRUPT EVENT HANDLER (The Main Logic Dispatcher)
// =======================================================

// Processes a single interrupt event and routes it based on game state
void handleInterruptEvent() {
  
  // 1. Get and clear the single interrupt that triggered this event
  int pinIndex = getLastInterruptedPin();
  if (pinIndex == -1) {
    return; // Spurious interrupt, ignore.
  }

  // 2. Get the current state of that pin
  int sensorValue = readSensorState(pinIndex);
  
  // 3. Dispatch the event based on the current game state
  switch(gameState) {
    case WAITING_FOR_HUMAN_PICKUP:
      handleHumanPickup(pinIndex, sensorValue);
      break;
    case HUMAN_PICKUP_VALIDATED:
      handleHumanPlacement(pinIndex, sensorValue);
      break;
    case AWAITING_HUMAN_DOUBLE_JUMP_PICKUP:
      if (sensorValue == HIGH && pinIndex == currentMove.startPin) {
        gameState = HUMAN_PICKUP_VALIDATED; // Now we wait for placement
      } else {
        readCurrentPhysicalState();
      }
      break;
    case AI_VALIDATION_PICKUP:
      handleAIPickup(pinIndex, sensorValue);
      break;
    case AI_VALIDATION_PLACEMENT:
      handleAIPlacement(pinIndex, sensorValue);
      break;
    case AWAITING_CAPTURE_REMOVAL:
      handleCaptureRemoval(pinIndex, sensorValue);
      break;
    case AWAITING_BOARD_CORRECTION:
      handleBoardCorrection(pinIndex, sensorValue);
      break;
    default:
      // Ignore interrupts in IDLE or GAME_OVER
      break;
  }
  // 4. After processing the event, clear any stray interrupts
  // that may have occurred *during* the processing.
  clearAllMCPInterrupts();
}

// Scans entire board to compare physical sensors vs logical board state
void readCurrentPhysicalState() {
    // First scan
    boardErrorCount = 0;
    for (int pin = 0; pin < NUM_SQUARES; pin++) {
        int sensorValue = readSensorState(pin);
        // Allow I2C lines & reed switch to settle
        delayMicroseconds(50);
        if (pin == 15) delayMicroseconds(200);   // Delay after finishing MCP_A before reading MCP_B

        if (sensorValue == LOW && boardState[pin] == EMPTY_SQUARE) {
            boardErrorPins[boardErrorCount++] = pin;
        }
        else if (sensorValue == HIGH && boardState[pin] != EMPTY_SQUARE) {
             boardErrorPins[boardErrorCount++] = pin;
        }
    }

    if (boardErrorCount > 0) {
        // Debounce delay to confirm errors are real and not transient
        delay(300);
        clearAllMCPInterrupts();
        
        // Re-scan to confirm
        int confirmedErrors = 0;
        for (int i = 0; i < boardErrorCount; i++) {
            int pin = boardErrorPins[i];
            int sv = readSensorState(pin);

            // Allow I2C lines & reed switch to settle
            delayMicroseconds(50);
            bool stillError = (sv == LOW && boardState[pin] == EMPTY_SQUARE) ||
                             (sv == HIGH && boardState[pin] != EMPTY_SQUARE);
            if (stillError) {
                boardErrorPins[confirmedErrors++] = pin;
            }
        }
        boardErrorCount = confirmedErrors;
        
        // Only trigger error state if errors are STILL present
        if (boardErrorCount > 0) {
            
            if (gameState != AWAITING_BOARD_CORRECTION) {
              previousGameState = gameState;
              gameState = AWAITING_BOARD_CORRECTION;
              lastFlashTime = 0;
              flashOn = true;

              clearAllMCPInterrupts();
              Serial.println("There are errors to be corrected - before play the mp3");
              if (isGameStarting) {
                  // It's the beginning of the game, play the special "setup" sound
                  playBlockingSound(2, 11);
              } else {
                  // It's mid-game, play the normal "wrong piece" sound
                  playBlockingSound(2, 14);
              }
              Serial.println("There are errors to be corrected - after play the mp3");
              
              // Check if the error was resolved WHILE the sound was playing.
              bool allResolved = true;
              for(int i = 0; i < boardErrorCount; i++) {
                  int pin = boardErrorPins[i];
                  int val = readSensorState(pin);
                  
                  // Check if the physical state now matches the expected board memory
                  // (Empty square should be HIGH, Occupied square should be LOW)
                  bool matches = (boardState[pin] == EMPTY_SQUARE && val == HIGH) ||
                                 (boardState[pin] != EMPTY_SQUARE && val == LOW);
                  
                  if(!matches) {
                      allResolved = false;
                      break; // Found an error still existing, stop checking
                  }
              }

              if (allResolved) {
                  // The user put the piece back during the voice prompt!
                  // Cancel the error mode immediately.
                  gameState = previousGameState;
                  boardErrorCount = 0;
                  
                  clearAllLEDs();
                  strip.show();
                  clearAllMCPInterrupts();
                  // Clear interrupts triggered by the correction
                  return;
                  // EXIT FUNCTION NOW - Skip the flashing lights
              }
            }
        } else {
            Serial.println("  → All errors self-corrected, continuing game");
            clearAllMCPInterrupts();
        }
    }
}

// =======================================================
// HUMAN PLAYER LOGIC
// =======================================================

// Handles the action of the human lifting a piece
void handleHumanPickup(byte pinIndex, int sensorValue) {
    if (sensorValue == HIGH) { // Piece was lifted
        int pieceType = boardState[pinIndex];
        if (pieceType == PLAYER_PIECE || pieceType == PLAYER_PIECE + KING_MODIFIER) {
            // MANDATORY JUMP CHECK
            if (mandatoryJumpCount > 0) {
                bool validJumpPiece = false;
                for (int i = 0; i < mandatoryJumpCount; i++) {
                    if (pinIndex == mandatoryJumpPins[i]) {
                        validJumpPiece = true;
                        break;
                    }
                }
                
                if (!validJumpPiece) {
                    playBlockingSound(2, 21); // "You have a mandatory jump"
                    
                    // Check if error was already corrected
                    // If the sensor is LOW, the piece is back on its square.
                    // We simply clear interrupts and return, skipping the error mode/flashing.
                    if (readSensorState(pinIndex) == LOW) {
                        clearAllMCPInterrupts();
                        return; 
                    }

                    clearAllMCPInterrupts();
                    readCurrentPhysicalState(); // Trigger error (only if piece is still lifted)
                    return;
                    // Exit
                }
            }

            currentMove.startPin = -1;
            currentMove.endPin = -1;
            currentMove.capturedPin = -1;
            legalMoves.count = 0;
            int totalMoves = findAllMoves(pinIndex, legalMoves.moves, 0);
            legalMoves.count = totalMoves;
            if (legalMoves.count > 0) {
                currentMove.startPin = pinIndex;
                gameState = HUMAN_PICKUP_VALIDATED;
                clearAllMCPInterrupts();
            } else {
                clearAllMCPInterrupts();
                readCurrentPhysicalState();
            }
        } else {
            // Error: Picked up opponent/empty square
            Serial.println(pinIndex);
            clearAllMCPInterrupts();
            readCurrentPhysicalState();
        }
    }
}

// Handles the action of the human placing a piece
void handleHumanPlacement(int pinIndex, int sensorValue) {
    if (sensorValue == LOW) { // A piece was placed (This is the expected action)
        
        // Check if the player "canceled" the move by placing it back
        if (pinIndex == currentMove.startPin) {
          gameState = WAITING_FOR_HUMAN_PICKUP; 
          clearAllLEDs();
          strip.show();
          clearAllMCPInterrupts();
          return; 
        }

        // Check if the placement pin is one of the legal destinations
        bool isValidPlacement = false;
        int moveIndex = -1;
        for(int i = 0; i < legalMoves.count; i++) {
            if (pinIndex == legalMoves.moves[i].endPin) {
                isValidPlacement = true;
                moveIndex = i;
                break;
            }
        }

        if (isValidPlacement) {
            // --- This is the normal, valid move logic ---
            currentMove = legalMoves.moves[moveIndex];
            int pieceType = boardState[currentMove.startPin]; 
            boardState[currentMove.startPin] = EMPTY_SQUARE;

            playKingSoundAfterCapture = false;
            // Promotion check
            if (pinToRow(pinIndex) == PLAYABLE_ROWS - 1 && pieceType != PLAYER_KING) {
                boardState[currentMove.endPin] = PLAYER_KING;
                if (currentMove.capturedPin != -1) {
                    playKingSoundAfterCapture = true;
                } else {
                    playBlockingSound(2, 10);
                    // "You're a king now"
                }
            } else {
                boardState[currentMove.endPin] = pieceType;
            }

            if (currentMove.capturedPin != -1) {
                Serial.println(currentMove.capturedPin);
                strip.setPixelColor(currentMove.capturedPin, AI_COLOR);
                strip.show();
                gameState = AWAITING_CAPTURE_REMOVAL;
                playUrgentSound(2, 26); // "Remove captured piece"
                clearAllMCPInterrupts();
            } else {
                if (isGameStarting) { isGameStarting = false; }
                currentTurn = AI_OPPONENT;
                playTurnChangeSound(AI_OPPONENT); // "My turn"
                clearAllLEDs();
                strip.show();
                gameState = IDLE;
                clearAllMCPInterrupts();
            }
        } else {
            // Error: Placed on an illegal square
            Serial.println(pinIndex);
            clearAllMCPInterrupts();
            readCurrentPhysicalState();
        }
    } else {
        // A piece was LIFTED (sensorValue == HIGH) instead of placed.
        if (pinIndex == currentMove.startPin) {
            // This is just the user "fumbling" or re-lifting the piece.
            // It's not an error. Just ignore it and stay in this state.
            return;
        } else {
            // This is a REAL error.
            // The user lifted a *different* piece
            // when they were supposed to be placing one.
            clearAllMCPInterrupts();
            readCurrentPhysicalState();
            // Trigger board correction
        }
    }
}

// Logic to handle the human removing a captured piece from the board
void handleCaptureRemoval(int pinIndex, int sensorValue) {
    if (pinIndex == currentMove.capturedPin && sensorValue == HIGH) {
        boardState[currentMove.capturedPin] = EMPTY_SQUARE;
        Serial.println("Capture complete. Checking for double jump...");

        // 1. Turn off the flashing light immediately.
        clearAllLEDs();
        strip.show();
        // 2. Play the blocking sound effect.
        playBlockingSound(1, 6); // Sound effect: capture
        
        Move secondaryMoves[4];
        int jumpsFound = findAllMoves(currentMove.endPin, secondaryMoves, 0);

        // Enforce "Stop on Promotion" Rule
        // If playKingSoundAfterCapture is true, we just Promoted.
        // Standard rules say the turn MUST end, so we ignore any further jumps.
        bool isPromotion = playKingSoundAfterCapture;
        if (!isPromotion && jumpsFound > 0 && secondaryMoves[0].capturedPin != -1) {
        // ---------------------------------------------
            
            // --- DOUBLE JUMP LOGIC ---
            Serial.print((currentTurn == HUMAN_PLAYER) ? "Player" : "AI");
            Serial.println(" has a mandatory double jump! Continuing turn.");
            currentMove.startPin = currentMove.endPin;
            legalMoves.count = jumpsFound;
            for (int i = 0; i < jumpsFound; i++) {
                legalMoves.moves[i] = secondaryMoves[i];
            }
            clearAllLEDs();
            strip.show();
            if (currentTurn == HUMAN_PLAYER) {
                gameState = AWAITING_HUMAN_DOUBLE_JUMP_PICKUP;
                playUrgentSound(2, 23); // "You have another jump"
            } else {
                playUrgentSound(2, 22);
                // "I have another jump"
                if (jumpsFound > 0) {
                    currentMove = secondaryMoves[0];
                    Serial.print("AI selects double jump: Pin ");
                    Serial.print(currentMove.startPin);
                    Serial.print(" -> Pin ");
                    Serial.println(currentMove.endPin);
                    if(currentMove.capturedPin != -1){
                         Serial.print("  (Capturing pin "); Serial.print(currentMove.capturedPin); Serial.println(")");
                    }
                } else {
                    currentMove.startPin = -1;
                }
                gameState = AI_VALIDATION_PICKUP;
            }
            
        } else {
            // --- END TURN ---
            Serial.print("No double jump (or turn ended by promotion). ");
            // --- PLAY DELAYED KING SOUND HERE ---
            if (playKingSoundAfterCapture) {
                if (currentTurn == HUMAN_PLAYER) {
                    Serial.println("Playing delayed Player King sound.");
                    playBlockingSound(2, 10); // "You're a king now"
                } else {
                    Serial.println("Playing delayed AI King sound.");
                    playBlockingSound(2, 9); // "King me"
                }
                playKingSoundAfterCapture = false;
                // Reset flag
            }

            // Check for win condition BEFORE switching turn
            int winner = checkWinCondition();
            if (winner != 0) {
                Serial.println("Game Over detected immediately after capture. Setting state to IDLE.");
                gameState = IDLE; // Set to IDLE so the main loop's win check will catch it.
                return;
                // Return to skip turn switch sounds
            } 
            
            Serial.print("Switching to ");
            if (isGameStarting) { isGameStarting = false; }
            
            if (currentTurn == HUMAN_PLAYER) {
                currentTurn = AI_OPPONENT;
                Serial.println("AI.");
                playTurnChangeSound(AI_OPPONENT); // "My turn"
            } else {
                currentTurn = HUMAN_PLAYER;
                Serial.println("Human.");
                aiMoveChosen = false; // RESET AI FLAG
                playTurnChangeSound(HUMAN_PLAYER);
                // "Your turn"
            }
            clearAllLEDs();
            strip.show();
            gameState = IDLE;
        }
    } else if (sensorValue == HIGH) {
        Serial.print("Error: Picked up wrong piece during removal prompt at pin ");
        Serial.println(pinIndex);
        clearAllMCPInterrupts();
        readCurrentPhysicalState();
    } else {
        Serial.println("Error: Placed a piece when expecting capture removal.");
        clearAllMCPInterrupts();
        readCurrentPhysicalState();
    }
}

// Handles piece replacement during error mode
void handleBoardCorrection(int pinIndex, int sensorValue) {
  // Check if the pin that triggered the interrupt is one of the error pins
  int errorIndex = -1;
  for(int i = 0; i < boardErrorCount; i++) {
      if (boardErrorPins[i] == pinIndex) {
          errorIndex = i;
          break;
      }
  }

  // If the interrupt wasn't from a currently known error pin, ignore it
  if (errorIndex == -1) {
      return;
  }

  // Check: Does the physical state now match the expected state?
  bool physicalMatchesExpected = false;
  if (boardState[pinIndex] == EMPTY_SQUARE) {
      if (sensorValue == HIGH) { // Expected empty, sensor is now high (correct)
          physicalMatchesExpected = true;
      }
  } else { 
      if (sensorValue == LOW) { // Expected piece, sensor is now low (correct)
          physicalMatchesExpected = true;
      }
  }

  if (physicalMatchesExpected) {
    // *** DEBOUNCE: Confirm after settling ***
    delay(150);
    int confirmValue = readSensorState(pinIndex);
    bool stillMatches = (boardState[pinIndex] == EMPTY_SQUARE) ? 
                        (confirmValue == HIGH) : (confirmValue == LOW);
    if (!stillMatches) {
        return;
    }
    
    // Remove this pin from the boardErrorPins array
    for (int i = 0; i < boardErrorCount - 1; i++) {
        boardErrorPins[i] = boardErrorPins[i + 1];
    }
    boardErrorCount--;

    // If all errors are resolved, exit correction state
    if (boardErrorCount == 0) {
      errorPin = -1;
      clearAllLEDs();
      strip.show();
      
      // *** FINAL FULL BOARD SCAN ***
      delay(200);
      int finalErrors = 0;
      for (int pin = 0; pin < NUM_SQUARES; pin++) {
          int sv = readSensorState(pin);
          bool error = (sv == LOW && boardState[pin] == EMPTY_SQUARE) ||
                       (sv == HIGH && boardState[pin] != EMPTY_SQUARE);
          if (error) finalErrors++;
      }
      
      if (finalErrors == 0) {
          gameState = IDLE;
          previousGameState = IDLE;
          //Serial.println("Board validated. Resuming game.");
      } else {
          readCurrentPhysicalState(); // Re-populate error list
      }
    }
  }
}


// =======================================================
// AI LOGIC (Including Minimax)
// =======================================================

/**
 * Scans the entire board for all player pieces
 * and populates the mandatoryJumpPins array if any jumps are found.
 */
void findMandatoryJumps() {
    mandatoryJumpCount = 0;
    Move pieceMoves[16];
    // Temp storage for one piece's moves
        
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
        if (boardState[pin] == PLAYER_PIECE || boardState[pin] == PLAYER_KING) {
            int moveCount = findAllMoves(pin, pieceMoves, 0);
            // findAllMoves() already prioritizes jumps.
            // If the first move is a jump, this piece is mandatory.
            if (moveCount > 0 && pieceMoves[0].capturedPin != -1) {
                if (mandatoryJumpCount < 12) { // Safety check
                    mandatoryJumpPins[mandatoryJumpCount++] = pin;
                }
            }
        }
    }
}

/**
 * Evaluates the current board state from the AI's perspective.
 * Positive score = AI is winning. Negative score = Player is winning.
 * INCLUDES a positional bonus for advancing pieces.
 */
long evaluateBoard() {
  float score = 0.0;
  // Use float for small bonuses
  
  for (int pin = 0; pin < NUM_SQUARES; pin++) {
    int piece = boardState[pin];
    int row = pinToRow(pin); // Get the piece's row

    if (piece == AI_PIECE) {
      // AI pieces move from high-row (7) to low-row (0)
      // Give a bonus for how "advanced" it is (closer to row 0)
      score += 1.0 + ( (7.0 - row) * 0.1 );
      // Base 1.0, bonus 0.0 to 0.7
      
    } else if (piece == AI_KING) {
      // Kings get the same advancement bonus
      score += 3.0 + ( (7.0 - row) * 0.1 );
      // Base 3.0, bonus 0.0 to 0.7
      
    } else if (piece == PLAYER_PIECE) {
      // Player pieces move from low-row (0) to high-row (7)
      // Give them a bonus (from AI's perspective, a penalty) for advancing
      score -= 1.0 + ( row * 0.1 );
      // Base -1.0, penalty -0.0 to -0.7
      
    } else if (piece == PLAYER_KING) {
      // Player kings get the same advancement penalty
      score -= 3.0 + ( row * 0.1 );
      // Base -3.0, penalty -0.0 to -0.7
    }
  }
  
  // Return the score, multiplied so we can use integers.
  // This avoids float-to-int conversion issues in minimax.
  return (long)(score * 10);
}

/**
 * A much "smarter" evaluation for Level 5, 6, and 7.
 * Includes: Piece Values, Position (Advancement), Mobility, AND Center Control.
 */
long evaluateBoard_Advanced() {
  // 1. Get the basic score from the simple evaluation
  long baseScore = evaluateBoard();
  
  int aiMobility = 0;
  int playerMobility = 0;
  
  // New: Center Control scores
  int aiCenterControl = 0;
  int playerCenterControl = 0;

  Move tempMoves[16];

  for (byte pin = 0; pin < NUM_SQUARES; pin++) {
    int piece = boardState[pin];
    
    // Check mobility & center control for AI
    if (piece == AI_PIECE || piece == AI_KING) {
      aiMobility += findAllMoves(pin, tempMoves, 0);
      
      // Center Control: Columns 2, 3, 4, 5 (The middle 4 columns)
      int col = pinToCol(pin);
      if (col >= 2 && col <= 5) {
          aiCenterControl++; 
      }
      
    // Check mobility & center control for Player
    } else if (piece == PLAYER_PIECE || piece == PLAYER_KING) {
      playerMobility += findAllMoves(pin, tempMoves, 0);
      
      int col = pinToCol(pin);
      if (col >= 2 && col <= 5) {
          playerCenterControl++; 
      }
    }
  }

  // 3. Weighting
  // Mobility: 3 points per move option
  int mobilityScore = (aiMobility - playerMobility) * 3;
  
  // Center Control: 2 points per piece in the center
  // This is small enough not to cause suicidal moves, but big enough to break ties.
  int centerScore = (aiCenterControl - playerCenterControl) * 2;
  
  return baseScore + mobilityScore + centerScore;
}

/**
 * "Grandmaster" evaluation for Level 6.
 * Adds "King Safety" to the Level 5 logic.
 * King Safety penalty is ignored if the AI is losing.
 */
long evaluateBoard_Grandmaster() {
  // 1. Get the score from the Level 5 "Advanced" brain
  long score = evaluateBoard_Advanced();
  
  const int KING_DANGER_PENALTY = -50;
  // -5.0 points

  // 2. Count the pieces to determine if we're winning, tied, or losing
  int aiPieceCount = 0;
  int playerPieceCount = 0;
  for (byte pin = 0; pin < NUM_SQUARES; pin++) { // Changed to byte
    int piece = boardState[pin];
    if (piece == AI_PIECE || piece == AI_KING) {
      aiPieceCount++;
    } else if (piece == PLAYER_PIECE || piece == PLAYER_KING) {
      playerPieceCount++;
    }
  }

  // 3. Apply King Safety if we are winning/tied OR if we are desperate (low pieces).
  //    We only take risks (ignore safety) if we are losing but still have resources (3+ pieces).
  if (aiPieceCount >= playerPieceCount || aiPieceCount <= 2) {
    
    // Loop through all AI Kings and check for immediate danger
    for (byte pin = 0; pin < NUM_SQUARES; pin++) { // Changed to byte
      if (boardState[pin] == AI_KING) {
        int r = pinToRow(pin);
        int c = pinToCol(pin);

        // Check all 4 diagonal directions for a "jump-over" threat
        for (int dr = -1; dr <= 1; dr += 2) { // dr = -1 (up), +1 (down)
          for (int dc = -1; dc <= 1; dc += 2) { // dc = -1 (left), +1 (right)
            
            int adjacentPin = rowColToPin(r + dr, c + dc);
            int attackerPin = rowColToPin(r + (dr*2), c + (dc*2));

            // Check if an adjacent square is EMPTY and the square beyond it
            // contains a piece that can capture the king
            if (adjacentPin != -1 && boardState[adjacentPin] == EMPTY_SQUARE && attackerPin != -1) {
              
              int attackerPiece = boardState[attackerPin];
              if (attackerPiece == PLAYER_KING) {
                // A Player King can attack from any diagonal
                score += KING_DANGER_PENALTY;
              } else if (attackerPiece == PLAYER_PIECE) {
                // A Player Man can only attack if it's moving "forward" (increasing row)
                // So, the player piece must be "above" the empty square
                // (dr = +1) means the empty square is *below* the king, so the attacker is even further down
      
                if (dr == 1) { 
                  score += KING_DANGER_PENALTY;
                }
              }
            }
          }
        }
      }
    }
  } // End of "if AI is winning or tied" block
  
  return score;
}

/**
 * Quiescence Search for Level 7.
 * Extends the search BEYOND depth 0, but ONLY for capture moves.
 * This prevents the "Horizon Effect" where the AI stops thinking mid-capture sequence.
 */
long quiescence(bool isMaximizingPlayer, long alpha, long beta) {
  
  // 1. Stand-Pat: What is the score if we just stop capturing right now?
  // We use the best evaluation function we have (Grandmaster).
  long standPat = evaluateBoard_Grandmaster();
  // 2. Pruning with Stand-Pat
  // If the board is already "good enough" that the opponent would avoid this path,
  // or "bad enough" that we would avoid it, we can stop.
  if (isMaximizingPlayer) {
    if (standPat >= beta) {
      return beta;
    }
    if (standPat > alpha) {
      alpha = standPat;
    }
  } else {
    if (standPat <= alpha) {
      return alpha;
    }
    if (standPat < beta) {
      beta = standPat;
    }
  }

  // 3. Explore ONLY Capture Moves (Jumps)
  // We reuse the memory-efficient loop structure from minimax.
  int pieceToCheck = isMaximizingPlayer ? AI_PIECE : PLAYER_PIECE;
  int kingToCheck = isMaximizingPlayer ? AI_KING : PLAYER_KING;
  for (byte pin = 0; pin < NUM_SQUARES; pin++) {
    if (boardState[pin] == pieceToCheck || boardState[pin] == kingToCheck) {
      Move pieceMoves[16];
      int moveCount = findAllMoves(pin, pieceMoves, 0);

      for (int i = 0; i < moveCount; i++) {
        Move m = pieceMoves[i];
        // CRITICAL: In Quiescence, we ONLY consider Captures (Jumps).
        // If it's a simple step, we ignore it and rely on the Stand-Pat score.
        if (m.capturedPin == -1) {
           continue;
        }

        // Make the Move
        int startPiece = boardState[m.startPin];
        int endPiece = boardState[m.endPin]; 
        int capturedPiece = boardState[m.capturedPin]; // We know capturedPin != -1
        
        boardState[m.startPin] = EMPTY_SQUARE;
        boardState[m.endPin] = startPiece; 
        boardState[m.capturedPin] = EMPTY_SQUARE;

        // Recursive Step (Call Quiescence again)
        long score = quiescence(!isMaximizingPlayer, alpha, beta);
        // Undo the Move
        boardState[m.startPin] = startPiece;
        boardState[m.endPin] = endPiece; 
        boardState[m.capturedPin] = capturedPiece;
        // Minimax/Alpha-Beta Logic
        if (isMaximizingPlayer) {
          if (score >= beta) return beta;
          // Prune
          if (score > alpha) alpha = score;
        } else {
          if (score <= alpha) return alpha;
          // Prune
          if (score < beta) beta = score;
        }
      }
    }
  }
  
  // If we are maximizing, alpha holds the best score found (or stand-pat).
  // If minimizing, beta holds the best score found (or stand-pat).
  return isMaximizingPlayer ? alpha : beta;
}

/**
 * Minimax algorithm implementation with Alpha-Beta Pruning.
 * Enforces Mandatory Jump rule and is memory-efficient
 * (no large local arrays) to prevent stack overflow.
 */
long minimax(int depth, bool isMaximizingPlayer, long alpha, long beta) {

  checkAndPlayThinkingSound();
  // Base Case: Maximum depth reached
  if (depth == 0) {
    // Level 7: Continue searching ONLY if the board is "noisy" (captures available)
    if (aiDifficultyLevel == 7) {
        return quiescence(isMaximizingPlayer, alpha, beta);
    }

    // If we are on Level 6, use the "Grandmaster" brain.
    if (aiDifficultyLevel == 6) {
      return evaluateBoard_Grandmaster();
    }
    // If we are on Level 5, use the "Advanced" brain.
    if (aiDifficultyLevel == 5) {
      return evaluateBoard_Advanced();
    } else {
      // Levels 3 & 4 use the fast, simple brain.
      return evaluateBoard();
    }
  }

  // --- RECURSIVE CASE ---
  if (isMaximizingPlayer) {
    // --- AI's Turn (Maximize Score) ---
    long bestScore = -100001L;
    bool foundAnyMove = false;

    // First, check for any mandatory jumps *anywhere* on the board
    bool jumpsExist = false;
    Move pieceMoves[16];
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
      if (boardState[pin] == AI_PIECE || boardState[pin] == AI_KING) {
          int moveCount = findAllMoves(pin, pieceMoves, 0);
          if (moveCount > 0 && pieceMoves[0].capturedPin != -1) {
              jumpsExist = true;
              break;
          }
      }
    }

    // Now, loop through all pieces and evaluate their moves
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
      if (boardState[pin] == AI_PIECE || boardState[pin] == AI_KING) {
          int moveCount = findAllMoves(pin, pieceMoves, 0);
          for (int i = 0; i < moveCount; i++) {
              Move m = pieceMoves[i];
              // If jumps exist, we can ONLY evaluate jump moves
              if (jumpsExist && m.capturedPin == -1) {
                  continue;
                  // This is a step move, but we must jump. Skip it.
              }
              
              foundAnyMove = true;
              // We have at least one valid move to check

              // 1. MAKE THE MOVE (Simulate)
              int startPiece = boardState[m.startPin];
              int endPiece = boardState[m.endPin]; 
              int capturedPiece = (m.capturedPin != -1) ? boardState[m.capturedPin] : EMPTY_SQUARE;
              boardState[m.startPin] = EMPTY_SQUARE;
              boardState[m.endPin] = startPiece;
              if (m.capturedPin != -1) boardState[m.capturedPin] = EMPTY_SQUARE;

              // 2. RECURSIVE CALL
              long score = minimax(depth - 1, false, alpha, beta);
              // 3. UNDO THE MOVE (Restore board)
              boardState[m.startPin] = startPiece;
              boardState[m.endPin] = endPiece; 
              if (m.capturedPin != -1) boardState[m.capturedPin] = capturedPiece;
              if (score > bestScore) {
                bestScore = score;
              }
              alpha = max(alpha, bestScore);
              if (beta <= alpha) break; // Pruning
          }
          if (beta <= alpha) break;
          // Pruning
      }
    }
    // If no moves were found at all, return the current board score.
    return foundAnyMove ? bestScore : evaluateBoard();

  } else {
    // --- Human's Turn (Minimize Score) ---
    long bestScore = 100001L;
    bool foundAnyMove = false;

    // First, check for any mandatory jumps *anywhere* on the board
    bool jumpsExist = false;
    Move pieceMoves[16];
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
      if (boardState[pin] == PLAYER_PIECE || boardState[pin] == PLAYER_KING) {
          int moveCount = findAllMoves(pin, pieceMoves, 0);
          if (moveCount > 0 && pieceMoves[0].capturedPin != -1) {
              jumpsExist = true;
              break;
          }
      }
    }

    // Now, loop through all pieces and evaluate their moves
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
      if (boardState[pin] == PLAYER_PIECE || boardState[pin] == PLAYER_KING) {
          int moveCount = findAllMoves(pin, pieceMoves, 0);
          for (int i = 0; i < moveCount; i++) {
              Move m = pieceMoves[i];
              // If jumps exist, we can ONLY evaluate jump moves
              if (jumpsExist && m.capturedPin == -1) {
                  continue;
                  // This is a step move, but we must jump. Skip it.
              }

              foundAnyMove = true;
              // We have at least one valid move to check

              // 1. MAKE THE MOVE (Simulate)
              int startPiece = boardState[m.startPin];
              int endPiece = boardState[m.endPin]; 
              int capturedPiece = (m.capturedPin != -1) ? boardState[m.capturedPin] : EMPTY_SQUARE;
              boardState[m.startPin] = EMPTY_SQUARE;
              boardState[m.endPin] = startPiece;
              if (m.capturedPin != -1) boardState[m.capturedPin] = EMPTY_SQUARE;

              // 2. RECURSIVE CALL
              long score = minimax(depth - 1, true, alpha, beta);
              // 3. UNDO THE MOVE (Restore board)
              boardState[m.startPin] = startPiece;
              boardState[m.endPin] = endPiece; 
              if (m.capturedPin != -1) boardState[m.capturedPin] = capturedPiece;
              if (score < bestScore) {
                bestScore = score;
              }
              beta = min(beta, bestScore);
              if (beta <= alpha) break; // Pruning
          }
          if (beta <= alpha) break;
          // Pruning
      }
    }
    // If no moves were found at all, return the current board score.
    return foundAnyMove ? bestScore : evaluateBoard();
  }
}

/**
 * Checks for specific opening scenarios (Opening Book).
 * Returns a valid Move if a book move is found, otherwise returns a Move with startPin = -1.
 */
Move getOpeningBookMove() {
  Move m;
  m.startPin = -1;

  // 1. Verify AI is untouched (All pieces on home squares 20-31)
  // If we have lost a piece or moved, the opening book is closed.
  for(int i=20; i<=31; i++) {
    if(boardState[i] != AI_PIECE) return m;
  }

  // 2. Check Player Status
  int playerMovedCount = 0;
  int movedFrom = -1;
  int movedTo = -1;
  for(int i=0; i<=11; i++) {
    if(boardState[i] == EMPTY_SQUARE) {
       playerMovedCount++;
       movedFrom = i;
    }
  }

  // --- CASE A: AI GOES FIRST (Player Untouched) ---
  if (playerMovedCount == 0) {
     // "Old Faithful" Opener: Move Pin 21 to 17
     Serial.println("Book Move: AI First (21->17)");
     m.startPin = 21; m.endPin = 17; m.capturedPin = -1;
     return m;
  }

  // --- CASE B: AI GOES SECOND (Response) ---
  if (playerMovedCount == 1) {
      // Find where they moved TO (scan rows 3, roughly pins 12-15)
      for(int i=12; i<=15; i++) {
          if(boardState[i] == PLAYER_PIECE) {
              movedTo = i;
              break;
          }
      }

      // Response 1: Player moved 9 -> 13
      if (movedFrom == 9 && movedTo == 13) {
          Serial.println("Book Response: 9->13 detected. Playing 22->18.");
          m.startPin = 22; m.endPin = 18; m.capturedPin = -1;
          return m;
      }

      // Response 2: Player moved 9 -> 14
      if (movedFrom == 9 && movedTo == 14) {
          Serial.println("Book Response: 9->14 detected. Playing 21->17.");
          m.startPin = 21; m.endPin = 17; m.capturedPin = -1;
          return m;
      }
      
      // Response 3: Player moved 10 -> 14
      if (movedFrom == 10 && movedTo == 14) {
          Serial.println("Book Response: 10->14 detected. Playing 22->18.");
          m.startPin = 22; m.endPin = 18; m.capturedPin = -1;
          return m;
      }
  }

  return m;
  // No book move found
}

/**
 * Calculates AI move based on difficulty level.
 */
void generateAIMove() {
    Move movesWithJump[40];
    byte jumpCount = 0;
    Move movesWithStep[40];
    byte stepCount = 0;
    // 1. Collect ALL possible legal moves for AI
    for (byte pin = 0; pin < NUM_SQUARES; pin++) {
        if (boardState[pin] == AI_PIECE || boardState[pin] == AI_KING) {
            Move pieceMoves[16];
            byte pieceMoveCount = findAllMoves(pin, pieceMoves, 0);
            if (pieceMoveCount > 0) {
                if (pieceMoves[0].capturedPin != -1) { 
                    for (byte i = 0; i < pieceMoveCount; i++) {
                        if(jumpCount < 40) movesWithJump[jumpCount++] = pieceMoves[i];
                    }
                } else { 
                    for (byte i = 0; i < pieceMoveCount; i++) {
                        if(stepCount < 40) movesWithStep[stepCount++] = pieceMoves[i];
                    }
                }
            }
        }
    }

    Move* selectionArray = nullptr;
    byte selectionCount = 0;
    currentMove.startPin = -1; // Default to no move found

    // --- DIFFICULTY LOGIC ---
    // --- Levels 1 Logic (Purely Random) ---
    if (aiDifficultyLevel == 1) { 
        if (jumpCount > 0) {
            selectionArray = movesWithJump;
            selectionCount = jumpCount;
            //Serial.println("AI Level 2: Prioritizing JUMP moves.");
        } else {
            selectionArray = movesWithStep;
            selectionCount = stepCount;
            //Serial.println("AI Level 2: No jumps, taking random step.");
        }
    
        if (selectionCount > 0) {
            byte selectedIndex = random(0, selectionCount);
            currentMove = selectionArray[selectedIndex];
        }

    } else {
      // --- Levels 2 and higher Logic (Minimax w/ Random Tie-Breaking) ---
      Serial.print("AI Level ");
      Serial.print(aiDifficultyLevel); Serial.println(": Calculating best move (Minimax)...");
      
      byte minimaxDepth;
      switch(aiDifficultyLevel) {
          case 2: minimaxDepth = 2;
          break;
          case 3: minimaxDepth = 4; break;
          case 4: minimaxDepth = 6; break;
          case 5: minimaxDepth = 6; break;
          case 6: minimaxDepth = 6; break;
          case 7:
            // 1. Try Opening Book first
            {
                Move bookMove = getOpeningBookMove();
                if (bookMove.startPin != -1) {
                    currentMove = bookMove;
                    Serial.println(">> Executing Opening Book Move");
                    return; // EXIT FUNCTION IMMEDIATELY
                }
            }

            // 2. Fallback to Dynamic Depth Minimax
              int totalPieces = 0;
              for(byte i=0; i<NUM_SQUARES; i++) {
                  if(boardState[i] != EMPTY_SQUARE) totalPieces++;
              }
              
              if (totalPieces <= 6) {
                  minimaxDepth = 8;
                  // Endgame Genius (Very Deep)
              } else if (totalPieces <= 14) {
                  minimaxDepth = 6;
                  // Midgame (Strong)
              } else {
                  minimaxDepth = 4;
                  // Opening (Fast)
              }
              Serial.print("Dynamic Depth: ");
              Serial.println(minimaxDepth);
              break;
          
          default: minimaxDepth = 2; break;
      } 

      if (jumpCount > 0) {
          selectionArray = movesWithJump;
          selectionCount = jumpCount;
          Serial.println("Minimax: Prioritizing Jumps.");
      } else {
          selectionArray = movesWithStep;
          selectionCount = stepCount;
          Serial.println("Minimax: No jumps, checking steps.");
      }

      long bestMoveScore = -100001L;
      // --- Tie-Breaking Logic ---
      byte bestMoveIndices[40];
      // Array to store the *indices* of all tying moves
      byte bestMoveCount = 0;
      // How many moves are tied for best
      // --- END Tie-Breaking ---

      unsigned long startTime = millis();
      aiThinkingTimer = millis();   // --- Initialize Thinking Timer
      aiThinkingSoundCount = 0;
      for (byte i = 0; i < selectionCount; i++) {
          Move m = selectionArray[i];
          byte startPiece = boardState[m.startPin];
          byte endPiece = boardState[m.endPin]; 
          byte capturedPiece = (m.capturedPin != -1) ? boardState[m.capturedPin] : EMPTY_SQUARE;
          boardState[m.startPin] = EMPTY_SQUARE;
          boardState[m.endPin] = startPiece;
          if (m.capturedPin != -1) boardState[m.capturedPin] = EMPTY_SQUARE;
          long score = minimax(minimaxDepth - 1, false, -100001L, 100001L);

          boardState[m.startPin] = startPiece;
          boardState[m.endPin] = endPiece;
          if (m.capturedPin != -1) boardState[m.capturedPin] = capturedPiece;

          // --- Tie-Breaking Logic ---
          if (score > bestMoveScore) {
              // This is a new best move
              bestMoveScore = score;
              bestMoveCount = 0; // Reset the list of tied moves
              bestMoveIndices[bestMoveCount++] = i;
              // Add this move as the first
          } else if (score == bestMoveScore) {
              // This move is *tied* for the best
              if(bestMoveCount < 40) { // Safety check
                  bestMoveIndices[bestMoveCount++] = i;
                  // Add it to the list
              }
          }
      } 
      
      unsigned long endTime = millis();
      Serial.print("Minimax calculation time: "); Serial.print(endTime - startTime); Serial.println(" ms");
      Serial.print("bestMoveScore = "); Serial.println(bestMoveScore);
      // --- Select from all tied moves ---
      if (bestMoveCount > 0) {
          // We have one or more "best" moves.
          // Pick one randomly.
          byte selectedIndex = random(0, bestMoveCount);
          byte bestMoveIndex = bestMoveIndices[selectedIndex];
          currentMove = selectionArray[bestMoveIndex];
      } else if (selectionCount > 0) {
          // Failsafe (shouldn't happen, but good to have)
          Serial.println("Minimax failed to find a best move, picking random.");
          currentMove = selectionArray[random(0, selectionCount)];
      }
      
      // Print the selected move (or lack thereof)
      if (currentMove.startPin != -1) {
          Serial.print("AI selects move: Pin ");
          Serial.print(currentMove.startPin);
          Serial.print(" -> Pin ");
          Serial.println(currentMove.endPin);
          if (currentMove.capturedPin != -1) {
              Serial.print("  (Capturing pin "); Serial.print(currentMove.capturedPin); Serial.println(")");
          }
      } else {
          Serial.println("AI found no legal moves.");
          currentMove.endPin = -1;
          currentMove.capturedPin = -1;
      }
  }
}

// Handles AI piece logic when a piece is physically lifted
void handleAIPickup(int pinIndex, int sensorValue) {
  if (sensorValue == HIGH) { // A piece was lifted (This is the expected action)
    if (pinIndex == currentMove.startPin) {
      // --- CORRECT ACTION ---
      //Serial.println("AI Move Step 1: CORRECT PICKUP.");
      gameState = AI_VALIDATION_PLACEMENT;
      clearAllLEDs();
      strip.setPixelColor(currentMove.endPin, END_COLOR);
      if (currentMove.capturedPin != -1) {
          strip.setPixelColor(currentMove.capturedPin, CAPTURE_COLOR);
      }
      strip.show();
      clearAllMCPInterrupts();
    } else {
      // --- ERROR: Lifted the WRONG piece ---
      Serial.println(pinIndex);
      clearAllMCPInterrupts();
      readCurrentPhysicalState();
    }
  } else {
    // This is an ERROR.
    // We expected a piece to be LIFTED (HIGH),
    // but instead a piece was PLACED (LOW).
    //Serial.println("Error: Placed a piece when expecting AI pickup.");
    clearAllMCPInterrupts();
    readCurrentPhysicalState();
  }
}

// Handles AI piece logic when a piece is physically placed
void handleAIPlacement(int pinIndex, int sensorValue) {
  if (sensorValue == LOW) { // A piece was placed (This is the expected action)

    // Check if the user "canceled" the AI move
    if (pinIndex == currentMove.startPin) {
      //Serial.println("AI Move Canceled by user. Waiting for pickup again.");
      gameState = AI_VALIDATION_PICKUP; // Go back to the pickup state
      clearAllMCPInterrupts();
      return;
    }

    if (pinIndex == currentMove.endPin) {
      // --- CORRECT ACTION ---
      //Serial.println("AI Move Step 2: CORRECT PLACEMENT.");
      int pieceType = boardState[currentMove.startPin]; 
      boardState[currentMove.startPin] = EMPTY_SQUARE;

      playKingSoundAfterCapture = false;
      if (pinToRow(pinIndex) == 0 && pieceType != AI_KING) { 
          boardState[currentMove.endPin] = AI_KING;
          //Serial.println("AI PIECE PROMOTED TO KING!");
          if (currentMove.capturedPin != -1) {
              playKingSoundAfterCapture = true;
          } else {
              playBlockingSound(2, 9);
              // "King me"
          }
      } else {
          boardState[currentMove.endPin] = pieceType;
      }

      if (currentMove.capturedPin != -1) {
          strip.setPixelColor(currentMove.capturedPin, PLAYER_COLOR);
          strip.show();
          //Serial.print("AI captured piece at pin: "); Serial.println(currentMove.capturedPin);
          playUrgentSound(2, 26);
          // "Remove captured piece"
          gameState = AWAITING_CAPTURE_REMOVAL;
          clearAllMCPInterrupts();
      } else {
          //Serial.println("AI turn complete. Switching to Human.");
          if (isGameStarting) { isGameStarting = false; }
          playTurnChangeSound(HUMAN_PLAYER);
          // "Your turn"
          currentTurn = HUMAN_PLAYER;
          aiMoveChosen = false;
          // Reset AI move flag
          clearAllLEDs();
          strip.show();
          gameState = IDLE;
          clearAllMCPInterrupts();
      }
    } else {
      // --- ERROR: Placed on the WRONG square ---
      Serial.println(pinIndex);
      clearAllMCPInterrupts();
      readCurrentPhysicalState();
    }
  } else {
    // A piece was LIFTED (sensorValue == HIGH).
    if (pinIndex == currentMove.startPin) {
        // This is just the user "fumbling" or re-lifting the AI piece.
        // It's not an error. Just ignore it and stay in this state.
        //Serial.println("Re-lifted the current AI piece. Ignoring.");
        return;
    } else {
        // This is a REAL error.
        // The user lifted a *different* piece
        // when they were supposed to be placing the AI piece.
        //Serial.println("Error: Lifted a DIFFERENT piece when expecting AI placement.");
        clearAllMCPInterrupts();
        readCurrentPhysicalState();
        // Trigger board correction
    }
  }
}


// =======================================================
// INITIALIZATION & DISPLAY
// =======================================================

// Resets the internal logical board state for a new game
void setInitialBoard() {
    //Serial.println("Setting initial board state: 12 Player (0-11), 12 AI (20-31).");
    for (int i = 0; i < NUM_SQUARES; i++) {
        boardState[i] = EMPTY_SQUARE;
    }
    for (int pin = 0; pin < 12; pin++) {
        boardState[pin] = PLAYER_PIECE;
    }
    for (int pin = 20; pin < NUM_SQUARES; pin++) {
        boardState[pin] = AI_PIECE;
    }
}

// Updates physical LEDs to match the internal logic state
void updateLEDsToBoardState() {
    clearAllLEDs();
    for (int i = 0; i < NUM_SQUARES; i++) {
        uint32_t color = strip.Color(0, 0, 0);
        int piece = boardState[i];
        if (piece == PLAYER_PIECE) color = PLAYER_COLOR;
        else if (piece == PLAYER_KING) color = strip.Color(255, 255, 0);
        // Yellow king
        else if (piece == AI_PIECE) color = AI_COLOR;
        else if (piece == AI_KING) color = strip.Color(0, 255, 255);
        // Cyan king
        if (piece != EMPTY_SQUARE) {
            strip.setPixelColor(i, color);
        }
    }
}

// Performs a startup visual sequence with LEDs
void initialFadeSequence() {
  const uint32_t INIT_COLOR = strip.Color(180, 0, 0);
  const int STEP_DURATION = 50;
  for (int i = 0; i < NUM_SQUARES; i++) {
    strip.setPixelColor(i, INIT_COLOR);
    strip.show();
    delay(STEP_DURATION);
    strip.setPixelColor(i, strip.Color(0, 0, 0));
    strip.show();
    delay(STEP_DURATION);
  }
}


// =======================================================
// UTILITY & ANIMATION
// =======================================================

// Non-blocking flash effect for valid destinations during human turn
void flashPossibleDestinations() {
  const int FLASH_RATE = 200;
  if (millis() - lastFlashTime > FLASH_RATE) {
    flashOn = !flashOn;
    clearAllLEDs();
    if (flashOn) {
      for(int i = 0; i < legalMoves.count; i++) {
          strip.setPixelColor(legalMoves.moves[i].endPin, END_COLOR);
          if (legalMoves.moves[i].capturedPin != -1) {
              strip.setPixelColor(legalMoves.moves[i].capturedPin, CAPTURE_COLOR);
          }
      }
    } else {
      clearAllLEDs();
    }
    strip.show();
    lastFlashTime = millis();
  }
}

// Non-blocking flash effect for error correction pins
void handleBoardCorrectionFlashing() {
  const int FLASH_RATE = 200;
  if (millis() - lastFlashTime > FLASH_RATE) {
    flashOn = !flashOn;
    clearAllLEDs();
    if (flashOn) {
      for (int i = 0; i < boardErrorCount; i++) {
          strip.setPixelColor(boardErrorPins[i], BOARD_CORRECTION_COLOR);
      }
    }
    strip.show();
    lastFlashTime = millis();
  }
}

// Turns off all LEDs on the strip
void clearAllLEDs() {
  for (int i = 0; i < NUM_SQUARES; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 0));
  }
}

// =======================================================
// BUTTONS & AUDIO
// =======================================================

// Polls the Level Selection button and updates difficulty
void checkLevelButton() {
  int reading = digitalRead(LEVEL_BUTTON_PIN);
  if (reading != lastLevelButtonReading) {
    lastLevelDebounceTime = millis();
    resetSleepTimer();
  }
  if ((millis() - lastLevelDebounceTime) > BUTTON_DEBOUNCE_DELAY) {
    if (reading != levelButtonState) {
      levelButtonState = reading;
      if (levelButtonState == LOW) {
        resetSleepTimer();
        aiDifficultyLevel++;
        if (aiDifficultyLevel > MAX_AI_LEVEL) {
          aiDifficultyLevel = 1;
        }
        Serial.println("---------------------------------");
        Serial.print("--- AI Difficulty Level set to: ");
        Serial.println(aiDifficultyLevel);
        Serial.println("---------------------------------");
        // Use playUrgentSound so it interrupts
        if (aiDifficultyLevel == 1) playUrgentSound(2, 1);
        else if (aiDifficultyLevel == 2) playUrgentSound(2, 2);
        else if (aiDifficultyLevel == 3) playUrgentSound(2, 3);
        else if (aiDifficultyLevel == 4) playUrgentSound(2, 4);
        else if (aiDifficultyLevel == 5) playUrgentSound(2, 5);
        else if (aiDifficultyLevel == 6) playUrgentSound(2, 6);
        else if (aiDifficultyLevel == 7) playUrgentSound(2, 31);

        delay(50);
      }
    }
  }
  lastLevelButtonReading = reading;
}

/**
 * Checks for the "Recover/Start Game" button (Pin 4).
 * Handles both starting a new game and recovering from error states.
 */
void checkRecoverButton() {
  int reading = digitalRead(RECOVER_BUTTON_PIN);
  if (reading != lastRecoverButtonReading) {
    lastRecoverDebounceTime = millis();
    resetSleepTimer();
  }
  
  if ((millis() - lastRecoverDebounceTime) > BUTTON_DEBOUNCE_DELAY) {
    if (reading != recoverButtonState) {
      recoverButtonState = reading;
      if (recoverButtonState == LOW) {
        resetSleepTimer();
        // --- LOGIC FOR GAME START (Only runs when AWAITING_LEVEL_SELECTION) ---
        if (gameState == AWAITING_LEVEL_SELECTION) {
            if (aiDifficultyLevel == 0) {
              playBlockingSound(5, MAX_AI_LEVEL);
              // "Use the green button to select a level from 1 to x, then the yellow button to begin."
              return;
            }
            Serial.println("\n!!! GAME STARTED BY USER !!!");
            // Randomize starting player and Announce Turn
            Serial.println("Randomizing starting player...");
            if (random(0, 2) == 0) {
              currentTurn = AI_OPPONENT;
              Serial.println("AI goes first.");
              playBlockingSound(2, 24); // "The computer will go first" 
            } else {
              currentTurn = HUMAN_PLAYER;
              Serial.println("Player goes first.");
              playBlockingSound(2, 25); // "You will go first" 
            }
            
            // Game is officially underway.
            // Reset state to IDLE.
            gameState = IDLE;
            return;
        }
        // --- END GAME START LOGIC ---

        Serial.println("\n!!! RECOVER TRIGGERED !!!");
        playBlockingSound(2, 28);   // "Entering recovery mode"

        // 1. RE-INITIALIZE AUDIO (Recovery for dead audio module)
        // If the player crashed, this wakes it back up.
        if (myDFPlayer.begin(mySoftwareSerial, false)) {
            Serial.println("Audio System Re-initialized.");
            myDFPlayer.volume(MP3_VOLUME);
        } else {
            Serial.println("Audio System failed to restart.");
        }
        isSoundPlaying = false;
        // 2. Force-Clear Hardware Interrupts
        clearAllMCPInterrupts();
        interruptFlag = false;
        // 3. Reset Logic Flags
        aiMoveChosen = false;
        playKingSoundAfterCapture = false;
        // 4. Clear visual artifacts
        clearAllLEDs();
        strip.show();
        // 5. Force a Board Scan to find the current reality
        readCurrentPhysicalState();
        if (boardErrorCount > 0) {
           Serial.println(" -> Errors found. Entering correction mode.");
           // If errors exist, we stay in AWAITING_BOARD_CORRECTION
           // but we set the return state to IDLE so the game restarts fresh when fixed.
           previousGameState = IDLE; 
        } else {
           Serial.println(" -> Board is correct. Resetting to IDLE.");
           playBlockingSound(2, 29); // "Board recovery complete"
           gameState = IDLE;
           // Give the sound a moment, then announce whose turn it is
           delay(1000);
           if (currentTurn == HUMAN_PLAYER) playTurnChangeSound(HUMAN_PLAYER);
           else playTurnChangeSound(AI_OPPONENT);
        }
      }
    }
  }
  lastRecoverButtonReading = reading;
}

// Play sound non-blocking (unless already playing)
void playSound(int folder, int file) {

  if (!isSoundPlaying) { 
    myDFPlayer.playFolder(folder, file);
    delay(30);
    isSoundPlaying = true;
  }
}

// Play sound, interrupting whatever was playing before
void playUrgentSound(int folder, int file) {
  //myDFPlayer.stop(); 
  myDFPlayer.playFolder(folder, file);
  delay(30);
  isSoundPlaying = true;
}

/**
 * Plays a sound and WAITS for it to finish (Blocking).
 * Useful for instructional messages that shouldn't be talked over.
 */
void playBlockingSound(int folder, int file) {
  //myDFPlayer.stop(); // Stop whatever is playing
  myDFPlayer.playFolder(folder, file);

  delay(30);
  isSoundPlaying = true;
  // Set the flag
  
  // Wait here until the sound is finished
  unsigned long soundStartTime = millis();
  while (isSoundPlaying) {
    // Check for DFPlayer messages
    if (myDFPlayer.available()) {
      byte type = myDFPlayer.readType();
      int value = myDFPlayer.read();

      // Check for *either* a finish OR an error
      if (type == DFPlayerPlayFinished) {
        isSoundPlaying = false;
        // This will break the loop
      } else if (type == DFPlayerError) {
        // Serial.print("DFPlayer Error during blocking play, exiting loop. Error code: ");
        // Serial.println(value);
        isSoundPlaying = false; // Also break the loop on error
      }
      printDetail(type, value);
      // Handle other messages while we wait
    }
    
    // Update Visuals *while* blocking
    // The flashing needs to run continuously while we wait.
    if (gameState == AWAITING_BOARD_CORRECTION) {
      //  handleBoardCorrectionFlashing();
    }

    // Failsafe: Timeout after 3 seconds
    if (millis() - soundStartTime > 3000) {
      Serial.println("!!! PlayBlockingSound TIMEOUT !!!");
      isSoundPlaying = false;
      break;
    }
    delay(10); // Don't spam the player with checks
  }
}

/**
 * Checks if the AI has been thinking for more than 6 seconds.
 * Plays different sounds based on how long it's been thinking.
 */
void checkAndPlayThinkingSound() {
  // Change to 6000 ms (6 seconds)
  if (millis() - aiThinkingTimer > 6000) {
    
    aiThinkingSoundCount++;
    // Increment the counter
    
    int fileToPlay;
    if (aiThinkingSoundCount == 1) {
      fileToPlay = 35;
      // 1st time: "Thinking"
    } else if (aiThinkingSoundCount <= 4) {
      fileToPlay = 37;
      // 2nd, 3rd, 4th times: "I'm still thinking"
    } else {
      fileToPlay = 38;
      // 5th time+: "I'm still thinking, sorry"
    }

    // Serial.print("AI Thinking... count: "); Serial.println(aiThinkingSoundCount);
    // Use playUrgentSound so it interrupts any previous thinking sound
    playUrgentSound(2, fileToPlay); 
    
    aiThinkingTimer = millis();
    // Reset timer for the next 6 seconds
  }
}

/**
 * Plays a randomized turn announcement.
 * Folder 06 = "Your Turn" (Human)
 * Folder 07 = "My Turn" (AI)
 * Logic: 50% chance for File 1, 10% chance each for Files 2-6.
 */
void playTurnChangeSound(PlayerTurn nextPlayer) {
  int folder = (nextPlayer == HUMAN_PLAYER) ? 6 : 7;
  int file = 1;
  // Default to file 1

  // Generate random number from 0 to 9
  int r = random(0, 10);
  // 0, 1, 2, 3, 4 (50%) -> Keep file 1
  // 5, 6, 7, 8, 9 (50%) -> Map to files 2-6
  if (r >= 5) {
    file = r - 3;
    // Maps 5->2, 6->3, 7->4, 8->5, 9->6
  }

  playSound(folder, file);
}

// =======================================================
// PIN MAPPING (Serpentine Logic)
// =======================================================

// Maps global pin number to Row index (0-7)
int pinToRow(int globalPin) {
  return globalPin / 4;
}

// Maps global pin number to Column index (0-7)
int pinToCol(int globalPin) {
  int row = pinToRow(globalPin);
  int offset = globalPin % 4;
  if (row % 2 == 0) {
    return 1 + (offset * 2);
  } else {
    return 6 - (offset * 2);
  }
}

// Maps Row/Column coordinates back to global pin number
int rowColToPin(int row, int col) {
  if (row < 0 || row >= PLAYABLE_ROWS || col < 0 || col >= 8) return -1;
  if ((row % 2) == (col % 2)) return -1;
  int basePin = row * 4;
  if (row % 2 == 0) {
    return basePin + ((col - 1) / 2);
  } else {
    return basePin + ((6 - col) / 2);
  }
}

// =======================================================
// MOVE FINDER (Core Logic)
// =======================================================

// Generates list of all possible valid moves for a given piece
int findAllMoves(byte startPin, Move movesList[], int startIndex) {
    int pieceType = boardState[startPin];
    if (pieceType == EMPTY_SQUARE) return 0;
    int directions[2];
    int numDirections = 1;
    if (pieceType == AI_PIECE || pieceType == AI_PIECE + KING_MODIFIER) {
        directions[0] = -1;
    } else if (pieceType == PLAYER_PIECE || pieceType == PLAYER_PIECE + KING_MODIFIER) {
        directions[0] = 1;
    }
    if (pieceType == AI_PIECE + KING_MODIFIER || pieceType == PLAYER_PIECE + KING_MODIFIER) {
        directions[1] = (directions[0] == 1) ?
        -1 : 1;
        numDirections = 2;
    }
    int startRow = pinToRow(startPin);
    int startCol = pinToCol(startPin);
    int moveCount = 0;
    Move jumps[4];
    int jumpCount = 0;
    Move steps[4];
    int stepCount = 0;
    int opponentType = (pieceType == PLAYER_PIECE || pieceType == PLAYER_PIECE + KING_MODIFIER) ? AI_PIECE : PLAYER_PIECE;
    if (pieceType >= KING_MODIFIER) {
      opponentType = (pieceType == PLAYER_PIECE + KING_MODIFIER) ?
      AI_PIECE : PLAYER_PIECE;
    }
    for (int d = 0; d < numDirections; d++) {
        int direction = directions[d];
        for (int colOffset = -1; colOffset <= 1; colOffset += 2) {
            int targetRow = startRow + direction;
            int targetCol = startCol + colOffset;
            int stepPin = rowColToPin(targetRow, targetCol);
            if (stepPin != -1 && boardState[stepPin] == EMPTY_SQUARE) {
                steps[stepCount].startPin = startPin;
                steps[stepCount].endPin = stepPin;
                steps[stepCount].capturedPin = -1;
                stepCount++;
            }
            int capturePin = rowColToPin(targetRow, targetCol);
            int capturedPiece = boardState[capturePin];
            if (capturePin != -1 && capturedPiece != EMPTY_SQUARE && (capturedPiece % KING_MODIFIER) == opponentType) {
                int landRow = startRow + 2 * direction;
                int landCol = startCol + 2 * colOffset;
                int landPin = rowColToPin(landRow, landCol);
                if (landPin != -1 && boardState[landPin] == EMPTY_SQUARE) {
                    jumps[jumpCount].startPin = startPin;
                    jumps[jumpCount].endPin = landPin;
                    jumps[jumpCount].capturedPin = capturePin;
                    jumpCount++;
                }
            }
        }
    }
    if (jumpCount > 0) {
        for (int i = 0; i < jumpCount; i++) {
            movesList[startIndex + moveCount] = jumps[i];
            moveCount++;
        }
    } else {
        for (int i = 0; i < stepCount; i++) {
            movesList[startIndex + moveCount] = steps[i];
            moveCount++;
        }
    }
    return moveCount;
}

// =======================================================
// DFPLAYER HELPER FUNCTION
// =======================================================

// Debug function to print DFPlayer status messages to Serial
void printDetail(byte type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("File Not Found"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

/**
 * Clears all pending MCP interrupt flags and resets the interrupt flag.
 * Call this whenever you need to ensure a clean interrupt state.
 */
void clearAllMCPInterrupts() {
  mcp_a.getLastInterruptPin();
  mcp_a.readGPIOA();
  mcp_a.readGPIOB();
  mcp_b.getLastInterruptPin();
  mcp_b.readGPIOA();
  mcp_b.readGPIOB();
}

// --- SLEEP MODE FUNCTIONS ---
void enterSleepMode() {
  isSleeping = true;
  clearAllLEDs();
  strip.show();
  playBlockingSound(2, 39);
  // "I'm going to sleep now"
  Serial.println("--- SLEEP MODE ACTIVATED ---");
}

void exitSleepMode() {
  isSleeping = false;
  playBlockingSound(2, 40); // "I'm awake now"
  Serial.println("--- WAKING UP ---");
  // Lights will restore automatically on next loop cycle
}

void resetSleepTimer() {
  lastInputTime = millis();
  if (isSleeping) {
    exitSleepMode();
  }
}