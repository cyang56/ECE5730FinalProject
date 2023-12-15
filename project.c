
/**
 * Sanath Kumar (sk2794@cornell.edu)
 * Chris Yang (cy524@cornell.edu)
 * 
 * Audio credit: 8 Bit Menu by David Renda
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 15 ---> Button
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 330 ohm resistor ---> VGA Red
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - RP2040 GND ---> VGA GND
 *
 */

// Include the VGA grahics library
#include "vga_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
// Include protothreads
#include "pt_cornell_rp2040_v1.h"
// Include 8-bit audio file and death crash sound
#include "audio_cropped_8bit.c"
#include "death_crash_cropped.c" 

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))



// Number of samples per period in sine table
#define array_size 414544
#define death_array_size 5957

// Pointer to the address of the DAC data table
unsigned const short * DAC_pointer = &audio_cropped_8bit[0] ;
unsigned const short * Death_Pointer = &death_crash_cropped[0] ;
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  2
#define PIN_MOSI 3
#define LDAC     22
#define SPI_PORT spi0

// Number of DMA transfers per event
const uint32_t transfer_count = array_size ;
const uint32_t death_transfer_count = death_array_size ;


// Select DMA channels
int data_chan = 2;
int ctrl_chan = 3;

// Create arrays for printing to VGA
char score_array [30];
char high_score_array [30];
char select_mode_array [30];
char one_player_array [30];
char two_player_array [30];
char you_died_array [30];
char press_button_array [40];

// Create global variables
unsigned int endgame = 0;
unsigned int barriers_passed = 0;
unsigned int gamemode = 1;
unsigned int start = 1;
unsigned int high_score = 0;
unsigned int player1win = 0;
unsigned int player2win = 0;
unsigned int reset = 0;
unsigned int audio_speed = 0xffff;
unsigned int p1_x_offset = 0;
unsigned int p1_y_offset = 0;
unsigned int p2_x_offset = 0;
unsigned int p2_y_offset = 0;

// Struct that controls player position
typedef struct
{
  int xpos;
  int ypos;
} Player;
Player player1 = {
  .xpos = 100,
  .ypos = 210
};
Player player2 = {
  .xpos = 100,
  .ypos = 240
};


void drawPlayer1() {
  fillRect(player1.xpos, player1.ypos, 30, 30, RED);
  
  fillCircle(player1.xpos + 11, player1.ypos + 11, 5, WHITE);
  fillCircle(player1.xpos + 23, player1.ypos + 11, 5, WHITE);
  
  fillCircle(player1.xpos + 11 + p1_x_offset, player1.ypos + 11 + p1_y_offset, 2, BLACK);
  fillCircle(player1.xpos + 23 + p1_x_offset, player1.ypos + 11 + p1_y_offset, 2, BLACK);
}

void drawPlayer2() {
  fillRect(player2.xpos, player2.ypos, 30, 30, CYAN);
  
  fillCircle(player2.xpos + 11, player2.ypos + 11, 5, WHITE);
  fillCircle(player2.xpos + 23, player2.ypos + 11, 5, WHITE);
  
  fillCircle(player2.xpos + 11 + p2_x_offset, player2.ypos + 11 + p2_y_offset, 2, BLACK);
  fillCircle(player2.xpos + 23 + p2_x_offset, player2.ypos + 11 + p2_y_offset, 2, BLACK);
}


// Start menu screen
void StartGame() {

  // Text for mode selection
  setCursor(75,240);
  setTextSize(4);
  setTextColor(WHITE);
  sprintf(select_mode_array, "Select a player mode:");
  writeString(select_mode_array);

  setCursor(225,310);
  setTextSize(4);
  setTextColor(WHITE);
  sprintf(one_player_array, "1 Player");
  writeString(one_player_array);

  setCursor(225,360);
  setTextSize(4);
  setTextColor(WHITE);
  sprintf(two_player_array, "2 Player");
  writeString(two_player_array);

  // Draws box around selected mode, default single player
  if (start == 1) {
    drawRect(215,300,210,50,WHITE);
    start = 0;
    gamemode = 1;
  }

  // Switches mode if either joystick goes up or down
  if (gpio_get(11) == 0 || gpio_get(8) == 0) {
    drawRect(215,300,210,50,WHITE);
    drawRect(215,350,210,50,BLACK);
    gamemode = 1;
  }
  if (gpio_get(10) == 0 || gpio_get(9) == 0) {
    drawRect(215,350,210,50,WHITE);
    drawRect(215,300,210,50,BLACK);
    gamemode = 2;
  }
}

// Creates and moves barriers
void UpdateBarriers() {
  // Static parameters and arrays for barriers
  static int xcoord[3] = {640, 640, 640};
  static int barrier_length[3];
  static unsigned int tunnel_height[3] = {200, 200, 200};
  static unsigned int top_height[3];
  static unsigned int bottom_height[3];
  static unsigned int gap_length[3];
  static unsigned int active_barriers[3] = {1, 0, 0};
  static unsigned int passed[3] = {0, 0, 0};
  static unsigned int speed = 5;
  static unsigned int draw[3] = {1, 1, 1};


  // If game ends and restarts, reset all parameters to beginning of game
  if (reset == 1) {
    xcoord[0] = xcoord[1] = xcoord[2] = 640;
    tunnel_height[0] = tunnel_height[1] = tunnel_height[2] = 200;
    active_barriers[0] = 1;
    active_barriers[1] = active_barriers[2] = passed[0] = passed[1] = passed[2] = 0;
    speed = 5;
    reset = 0;
    barriers_passed = 0;
    audio_speed = 0xffff;
    dma_timer_set_fraction(0, 0x0004, audio_speed) ;
  }

  // Go through all 3 potential barriers
  for (int i = 0; i < 3; i++) {
    // Check if a certain barrier is active
    if (active_barriers[i] == 1) {
      // Create barrier with random parameters
      if (xcoord[i] == 640) {
        barrier_length[i] = 100 + (rand() % 400);
        top_height[i] = (rand() % (480 - tunnel_height[i]));
        bottom_height[i] = (480 - tunnel_height[i]) - top_height[i];
        gap_length[i] = 150 + (rand() % 350);
      }

      // Erase previously drawn barrier
      drawRect(xcoord[i], 0, barrier_length[i], top_height[i], BLACK);
      drawRect(xcoord[i], 480-bottom_height[i], barrier_length[i], bottom_height[i], BLACK);
      
      // Move barrier right to left
      if (xcoord[i] > 0) {
        xcoord[i] -= speed;
      }
      // Shrink barrier right to left once at far left
      else if (barrier_length[i] > 0) {
        barrier_length[i] -= speed;
      }
      // Reset barrier
      else {
        xcoord[i] = 640;
        active_barriers[i] = 0;
        passed[i] = 0;
        draw[i] = 0;
        // Increase game speed every 5 barriers
        if (barriers_passed % 5 == 0) {
          speed++;
        }
        // Increase audio speed every 15 barriers (no one has gotten to 45)
        if (barriers_passed == 15) {
          audio_speed = 0xd903;
          dma_timer_set_fraction(0, 0x0004, audio_speed) ;
        }
        if (barriers_passed == 30) {
          audio_speed = 0xc350;
          dma_timer_set_fraction(0, 0x0004, audio_speed) ;
        }
        // Decrease tunnel height every 3 barriers
        if (tunnel_height[i] > 50) {
          tunnel_height[i] -= 10;
        }
      }

      // Activate next barrier once the gap length has passed
      if ((xcoord[i] + barrier_length[i] + gap_length[i] <= 640) && (xcoord[i] + barrier_length[i] + gap_length[i] >= (641 - speed))) {
        if (i == 0 || i == 1) {
          active_barriers[i+1] = 1;
        }
        else {
          active_barriers[0] = 1;
        }
      }

      // 1 player control
      if (gamemode == 1) {
        // Player has passed barrier
        if (player1.xpos >= xcoord[i] + barrier_length[i] && passed[i] == 0) {
          passed[i] = 1;
          barriers_passed++;
        }

        // Check for collision with barrier and end game
        if (((player1.xpos + 30) >= xcoord[i] && player1.xpos <= (xcoord[i] + barrier_length[i])) && (player1.ypos <= top_height[i] || (player1.ypos+30) >= (480-bottom_height[i]))) {
          endgame = 1;
          if (barriers_passed > high_score) {
            high_score = barriers_passed;
          }
        }
      }

      // 2 player control
      else {
        // Either player has passed barrier
        if (((player1.xpos >= xcoord[i] + barrier_length[i]) || (player2.xpos >= xcoord[i] + barrier_length[i])) && passed[i] == 0) {
          passed[i] = 1;
          barriers_passed++;
        }

        // Check for player 1 collision with barrier, end game and player 2 wins
        if (((player1.xpos + 30) >= xcoord[i] && player1.xpos <= (xcoord[i] + barrier_length[i])) && (player1.ypos <= top_height[i] || (player1.ypos+30) >= (480-bottom_height[i]))) {
          endgame = 1;
          player2win = 1;
          if (barriers_passed > high_score) {
            high_score = barriers_passed;
          }
        }
        
        // Check for player 2 collision with barrier, end game and player 1 wins
        if (((player2.xpos + 30) >= xcoord[i] && player2.xpos <= (xcoord[i] + barrier_length[i])) && (player2.ypos <= top_height[i] || (player2.ypos+30) >= (480-bottom_height[i]))) {
          endgame = 1;
          player1win = 1;
          if (barriers_passed > high_score) {
            high_score = barriers_passed;
          }
        }
      }
      
      // Draw barriers in updated position unless barrier has just been reset
      if (draw[i] == 1) {
        drawRect(xcoord[i], 0, barrier_length[i], top_height[i], WHITE);
        drawRect(xcoord[i], 480-bottom_height[i], barrier_length[i], bottom_height[i], WHITE);
      }
      draw[i] = 1;
      
    }
  }
}

void configure_death_audio () {
  // Setup the control channel
  dma_channel_config c3 = dma_channel_get_default_config(ctrl_chan);   // default configs
  channel_config_set_transfer_data_size(&c3, DMA_SIZE_32);             // 32-bit txfers
  channel_config_set_read_increment(&c3, false);                       // no read incrementing
  channel_config_set_write_increment(&c3, false);                      // no write incrementing
  channel_config_set_chain_to(&c3, data_chan);                         // chain to data channel

  dma_channel_configure(
      ctrl_chan,                          // Channel to be configured
      &c3,                                 // The configuration we just created
      &dma_hw->ch[data_chan].read_addr,   // Write address (data channel read address)
      &Death_Pointer,                       // Read address (POINTER TO AN ADDRESS)
      1,                                  // Number of transfers
      false                               // Don't start immediately
  );

  // Setup the data channel
  dma_channel_config c4 = dma_channel_get_default_config(data_chan);  // Default configs
  channel_config_set_transfer_data_size(&c4, DMA_SIZE_16);            // 16-bit txfers
  channel_config_set_read_increment(&c4, true);                       // yes read incrementing
  channel_config_set_write_increment(&c4, false);                     // no write incrementing
  // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
  // sys_clk is 250 MHz. Configured to ~16 kHz
  dma_timer_set_fraction(0, 0, audio_speed) ;                         // 0xffff for 16 kHz, 0xd903 for 18 kHz, 0xc350 for 20 kHz
  // 0x3b means timer0 (see SDK manual)
  channel_config_set_dreq(&c4, 0x3b);                                 // DREQ paced by timer 0
  // chain to the controller DMA channel
  channel_config_set_chain_to(&c4, ctrl_chan);                        // Chain to control channel


  dma_channel_configure(
      data_chan,                  // Channel to be configured
      &c4,                        // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
      death_crash_cropped,         // The initial read address
      death_array_size,                 // Number of transfers
      false                       // Don't start immediately.
  );
}

// End game screen
void EndGame() {

  // Blank out rectangle in center of screen
  fillRect(160,120,320,240,BLACK);
  drawRect(170,130,300,220,WHITE);
  // End screen for 1 player
  if (gamemode == 1) {
    setCursor(222,230);
    setTextSize(3);
    setTextColor(WHITE);
    sprintf(you_died_array, "YOU DIED :(");
    writeString(you_died_array);
  }
  // End screen for 2 players, depending on who wins
  else {
    if (player1win) {
      setCursor(180,230);
      setTextSize(3);
      setTextColor(WHITE);
      sprintf(you_died_array, "PLAYER 2 DIED :(");
      writeString(you_died_array);
    }
    else if (player2win) {
      setCursor(180,230);
      setTextSize(3);
      setTextColor(WHITE);
      sprintf(you_died_array, "PLAYER 1 DIED :(");
      writeString(you_died_array);
    }
  }

  // Allow user to return to start
  setCursor(220,300);
  setTextSize(1);
  setTextColor(WHITE);
  sprintf(press_button_array, "Press button to return to menu...");
  writeString(press_button_array);
}

// Move player 1 based on joystick input
void MovePlayer1() {
  // Erase previous position
  fillRect(player1.xpos, player1.ypos, 30, 30, BLACK);

  // Straight Up
  if (gpio_get(11) == 0 && gpio_get(12) == 1 && gpio_get(13) == 1) {
    player1.ypos -= 10;
    p1_x_offset = 0;
    p1_y_offset = -3;
  }
  // Up Left
  else if (gpio_get(11) == 0 && gpio_get(12) == 0) {
    player1.ypos -= 10;
    player1.xpos -= 10;
    p1_x_offset = -3;
    p1_y_offset = -3;
  }
  // Straight Left
  else if (gpio_get(10) == 1 && gpio_get(11) == 1 && gpio_get(12) == 0) {
    player1.xpos -= 10;
    p1_x_offset = -3;
    p1_y_offset = 0;
  }
  // Down Left
  else if (gpio_get(10) == 0 && gpio_get(12) == 0) {
    player1.ypos += 10;
    player1.xpos -= 10;
    p1_x_offset = -3;
    p1_y_offset = 3;
  }
  // Straight Down
  else if (gpio_get(10) == 0 && gpio_get(12) == 1 && gpio_get(13) == 1) {
    player1.ypos += 10;
    p1_x_offset = 0;
    p1_y_offset = 3;
  }
  // Down Right
  else if (gpio_get(10) == 0 && gpio_get(13) == 0) {
    player1.ypos += 10;
    player1.xpos += 10;
    p1_x_offset = 3;
    p1_y_offset = 3;
  }
  // Straight Right
  else if (gpio_get(10) == 1 && gpio_get(11) == 1 && gpio_get(13) == 0) {
    player1.xpos += 10;
    p1_x_offset = 3;
    p1_y_offset = 0;
  }
  // Up Right
  else if (gpio_get(11) == 0 && gpio_get(13) == 0) {
    player1.ypos -= 10;
    player1.xpos += 10;
    p1_x_offset = 3;
    p1_y_offset = -3;
  }
  // Not Moving
  else {
    p1_x_offset=0; 
    p1_y_offset=0; 
  }
  

  // Keep player within screen
  if (player1.xpos <= 0) {
    player1.xpos = 0;
  }
  else if (player1.xpos >= 610) {
    player1.xpos = 610;
  }
  if (player1.ypos <= 0) {
    player1.ypos = 0;
  }
  else if (player1.ypos >= 450) {
    player1.ypos = 450;
  }
  
  // Draw player 1 (red for now)
  drawPlayer1();
}

// Move player 2 based on joystick input
void MovePlayer2() {
  // Erase previous position
  fillRect(player2.xpos, player2.ypos, 30, 30, BLACK);
  
  // Straight Up
  if (gpio_get(8) == 0 && gpio_get(7) == 1 && gpio_get(6) == 1) {
    player2.ypos -= 10;
    p2_x_offset = 0;
    p2_y_offset = -3;
  }
  // Up Left
  else if (gpio_get(8) == 0 && gpio_get(7) == 0) {
    player2.ypos -= 10;
    player2.xpos -= 10;
    p2_x_offset = -3;
    p2_y_offset = -3;
  }
  // Straight Left
  else if (gpio_get(9) == 1 && gpio_get(8) == 1 && gpio_get(7) == 0) {
    player2.xpos -= 10;
    p2_x_offset = -3;
    p2_y_offset = 0;
  }
  // Down Left
  else if (gpio_get(9) == 0 && gpio_get(7) == 0) {
    player2.ypos += 10;
    player2.xpos -= 10;
    p2_x_offset = -3;
    p2_y_offset = 3;
  }
  // Straight Down
  else if (gpio_get(9) == 0 && gpio_get(7) == 1 && gpio_get(6) == 1) {
    player2.ypos += 10;
    p2_x_offset = 0;
    p2_y_offset = 3;
  }
  // Down Right
  else if (gpio_get(9) == 0 && gpio_get(6) == 0) {
    player2.ypos += 10;
    player2.xpos += 10;
    p2_x_offset = 3;
    p2_y_offset = 3;
  }
  // Straight Right
  else if (gpio_get(9) == 1 && gpio_get(8) == 1 && gpio_get(6) == 0) {
    player2.xpos += 10;
    p2_x_offset = 3;
    p2_y_offset = 0;
  }
  // Up Right
  else if (gpio_get(8) == 0 && gpio_get(6) == 0) {
    player2.ypos -= 10;
    player2.xpos += 10;
    p2_x_offset = 3;
    p2_y_offset = -3;
  }
  // Not Moving
  else {
    p2_x_offset=0; 
    p2_y_offset=0; 
  }
  
  
  // Keep player within screen
  if (player2.xpos <= 0) {
    player2.xpos = 0;
  }
  else if (player2.xpos >= 610) {
    player2.xpos = 610;
  }
  if (player2.ypos <= 0) {
    player2.ypos = 0;
  }
  else if (player2.ypos >= 450) {
    player2.ypos = 450;
  }
  
  // Draw player 2 (blue for now)
  drawPlayer2();
}

void configure_audio () {
  // Setup the control channel
  dma_channel_config c = dma_channel_get_default_config(ctrl_chan);   // default configs
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // 32-bit txfers
  channel_config_set_read_increment(&c, false);                       // no read incrementing
  channel_config_set_write_increment(&c, false);                      // no write incrementing
  channel_config_set_chain_to(&c, data_chan);                         // chain to data channel

  dma_channel_configure(
      ctrl_chan,                          // Channel to be configured
      &c,                                 // The configuration we just created
      &dma_hw->ch[data_chan].read_addr,   // Write address (data channel read address)
      &DAC_pointer,                       // Read address (POINTER TO AN ADDRESS)
      1,                                  // Number of transfers
      false                               // Don't start immediately
  );

  // Setup the data channel
  dma_channel_config c2 = dma_channel_get_default_config(data_chan);  // Default configs
  channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);            // 16-bit txfers
  channel_config_set_read_increment(&c2, true);                       // yes read incrementing
  channel_config_set_write_increment(&c2, false);                     // no write incrementing
  // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
  // sys_clk is 250 MHz. Configured to ~16 kHz
  dma_timer_set_fraction(0, 0, audio_speed) ;                         // 0xffff for 16 kHz, 0xd903 for 18 kHz, 0xc350 for 20 kHz
  // 0x3b means timer0 (see SDK manual)
  channel_config_set_dreq(&c2, 0x3b);                                 // DREQ paced by timer 0
  // chain to the controller DMA channel
  channel_config_set_chain_to(&c2, ctrl_chan);                        // Chain to control channel


  dma_channel_configure(
      data_chan,                  // Channel to be configured
      &c2,                        // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
      audio_cropped_8bit,         // The initial read address
      array_size,                 // Number of transfers
      false                       // Don't start immediately.
  );
}






// Main thread that runs through game process
static PT_THREAD (protothread_anim(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    static int begin_time ;
    static int spare_time_0 = 33000 ;

    // Draw Large Player 1 on menu screen
    fillRect(185, 75, 90, 90, RED);
  
    fillCircle(185 + 33, 75 + 33, 15, WHITE);
    fillCircle(185 + 69, 75 + 33, 15, WHITE);
    
    fillCircle(185 + 33, 75 + 33, 6, BLACK);
    fillCircle(185 + 69, 75 + 33, 6, BLACK);

    // Draw Large Player 2 on menu screen
    fillRect(365, 75, 90, 90, CYAN);
    
    fillCircle(365 + 33, 75 + 33, 15, WHITE);
    fillCircle(365 + 69, 75 + 33, 15, WHITE);
    
    fillCircle(365 + 33, 75 + 33, 6, BLACK);
    fillCircle(365 + 69, 75 + 33, 6, BLACK);

    // Display start menu screen while button not pressed
    while(gpio_get(15)) {
      StartGame();
    }
    // Check for button press and release
    while(!gpio_get(15)) {

    }
    // Black out screen on button release for game start
    fillRect(0,0,640,480,BLACK);

    // Start audio
    configure_audio();
    dma_timer_set_fraction(0, 0x0004, audio_speed) ;
    
    // start the control channel
    dma_start_channel_mask(1u << ctrl_chan) ;
    

    // Draw player 1, add player 2 if 2 player mode
    drawPlayer1();
    if (gamemode == 2) {
      drawPlayer2();
    }
    
    // Gameplay
    while(1) {
      begin_time = time_us_32();
      
      // Constantly update barriers and move players, depending on game mode
      UpdateBarriers();
      MovePlayer1();
      if (gamemode == 2) {
        MovePlayer2();
      }

      // If end game flag is 1, jump to end game screen
      if (endgame == 1) {
        break;
      }

      // Print current score, updates after each barrier
      setCursor(520,5);
      setTextSize(1);
      setTextColor(WHITE);
      sprintf(score_array, "Current score: %d", barriers_passed);
      fillRect(600, 4, 25, 15, BLACK);
      writeString(score_array);

      // Print high score, updates after game if beaten
      setCursor(520,15);
      setTextSize(1);
      setTextColor(WHITE);
      sprintf(high_score_array, "   High score: %d", high_score);
      fillRect(600, 14, 25, 15, BLACK);
      writeString(high_score_array);
      
      // delay in accordance with frame rate
      spare_time_0 = 33000 - (time_us_32() - begin_time);

      // yield for necessary amount of time
      PT_YIELD_usec(spare_time_0) ;
      
      // END WHILE(1)
    }
  if (gamemode == 1 || player2win) {
    fillCircle(player1.xpos + 11, player1.ypos + 11, 5, WHITE);
    fillCircle(player1.xpos + 23, player1.ypos + 11, 5, WHITE);
  
    drawLine(player1.xpos + 7, player1.ypos + 7, player1.xpos + 15, player1.ypos + 15, BLACK);
    drawLine(player1.xpos + 15, player1.ypos + 7, player1.xpos + 7, player1.ypos + 15, BLACK);

    drawLine(player1.xpos + 19, player1.ypos + 7, player1.xpos + 27, player1.ypos + 15, BLACK);
    drawLine(player1.xpos + 27, player1.ypos + 7, player1.xpos + 19, player1.ypos + 15, BLACK);
  }
  else {
    fillCircle(player2.xpos + 11, player2.ypos + 11, 5, WHITE);
    fillCircle(player2.xpos + 23, player2.ypos + 11, 5, WHITE);
    
    drawLine(player2.xpos + 7, player2.ypos + 7, player2.xpos + 15, player2.ypos + 15, BLACK);
    drawLine(player2.xpos + 15, player2.ypos + 7, player2.xpos + 7, player2.ypos + 15, BLACK);

    drawLine(player2.xpos + 19, player2.ypos + 7, player2.xpos + 27, player2.ypos + 15, BLACK);
    drawLine(player2.xpos + 27, player2.ypos + 7, player2.xpos + 19, player2.ypos + 15, BLACK);
  }
      // End audio
    dma_channel_abort(data_chan);
    configure_death_audio();
    audio_speed = 0xffff;
    dma_timer_set_fraction(0, 0x0004, audio_speed);
    
    // start the control channel
    dma_start_channel_mask(1u << ctrl_chan) ;
    PT_YIELD_usec(375000);
    dma_channel_abort(data_chan);

    // End game screen
    EndGame();
    // Hold while button not pressed
    while(gpio_get(15)) {
      
    }
    // Reset necessary parameters and flags, and player positions
    start = 1;
    reset = 1;
    endgame = 0;
    player1win = 0;
    player2win = 0;
    player1.xpos = 100;
    player1.ypos = 210;
    player2.xpos = 100;
    player2.ypos = 240;
    // Check for button press and release
    while(!gpio_get(15)) {

    }
    // Black out screen on button release
    fillRect(0,0,640,480,BLACK);
    
    PT_END(pt);
} // animation thread


// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  // Overclock CPU
  set_sys_clock_khz(250000,1);
 
  // initialize stio
  stdio_init_all() ;

  // initialize VGA
  initVGA() ;

  // Initialize joystick 1 GPIO pins
  gpio_init(10);
  gpio_init(11);
  gpio_init(12);
  gpio_init(13);

  gpio_set_dir(10, GPIO_IN);
  gpio_set_dir(11, GPIO_IN);
  gpio_set_dir(12, GPIO_IN);
  gpio_set_dir(13, GPIO_IN);
  
  gpio_pull_up(10);
  gpio_pull_up(11);
  gpio_pull_up(12);
  gpio_pull_up(13);

  // Initialize joystick 2 GPIO pins (plug in backwards)
  gpio_init(6);
  gpio_init(7);
  gpio_init(8);
  gpio_init(9);

  gpio_set_dir(6, GPIO_IN);
  gpio_set_dir(7, GPIO_IN);
  gpio_set_dir(8, GPIO_IN);
  gpio_set_dir(9, GPIO_IN);
  
  gpio_pull_up(6);
  gpio_pull_up(7);
  gpio_pull_up(8);
  gpio_pull_up(9);
  
  // Initialize button
  gpio_init(15);
  gpio_set_dir(15, GPIO_IN);
  gpio_pull_up(15);

  // add threads
  pt_add_thread(protothread_anim);



  //////////////////////////////////////////////////
  ////////          DMA AUDIO             //////////
  //////////////////////////////////////////////////

  
  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;

  // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // start scheduler
  pt_schedule_start ;
  
  
} 
