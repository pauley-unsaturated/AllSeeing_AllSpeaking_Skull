/*
This sketch illustrates how to set a timer on an SAMD21 based board in Arduino (Feather M0, Arduino Zero should work)
*/

#include <cstdint>

bool tcIsSyncing();
void tcStartCounter();
void tcReset();
void tcDisable();

void *_context = NULL;
void (*_TC4_callback)(void*) = NULL;
void (*_TC5_callback)(void*) = NULL;

//Called by the interrupt at SampleRate
void TC4_Handler (void) {
  // Check for match counter 0 (MC0) interrupt
  if (TC4->COUNT16.INTFLAG.bit.MC0 && TC4->COUNT16.INTENSET.bit.MC0)             
  {
    _TC4_callback(_context);
    REG_TC4_INTFLAG = TC_INTFLAG_MC0; // Clear the MC0 interrupt flag
  }
}

void TC5_Handler(void) {
  // Check for match counter 0 (MC0) interrupt
  if (TC5->COUNT16.INTFLAG.bit.MC0 && TC5->COUNT16.INTENSET.bit.MC0)             
  {
    _TC5_callback(_context);
    REG_TC5_INTFLAG = TC_INTFLAG_MC0; // Clear the MC0 interrupt flag
  }
}

/* 
 *  TIMER SPECIFIC FUNCTIONS FOLLOW
 *  you shouldn't change these unless you know what you're doing
 */
//Configures the TC to generate output events at the sample frequency.
//Configures the TC in Frequency Generation mode, with an event output once
//each time the audio sample frequency period expires.
typedef void (*Timer_Callback)(void* context);
 void Timer_Configure(int sampleRate, int timerFreq, void* context, Timer_Callback sampleCallback, Timer_Callback timerCallback)
{
  _context = context;
  _TC4_callback = sampleCallback;
  _TC5_callback = timerCallback;
  
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(2) |          // Divide the 48MHz clock source by divisor 2: 48MHz/2=24Mhz
                    GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);          // Select GCLK4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  // Feed GCLK4 to TC4 and TC5
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TC4_TC5;     // Feed the GCLK4 to TC4 and TC5
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_TC4_CTRLA |= TC_CTRLA_MODE_COUNT16;           // Set the counter to 16-bit mode
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization

  REG_TC4_COUNT16_CC0 = (24000000 / (sampleRate * 64)) - 1;  // Set the TC4 CC0 register to the sample period
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization

  NVIC_DisableIRQ(TC4_IRQn);
  NVIC_ClearPendingIRQ(TC4_IRQn);
  NVIC_SetPriority(TC4_IRQn, 0);    // Set the Nested Vector Interrupt Controller (NVIC) priority for TC4 to 1 (second-highest)
  NVIC_EnableIRQ(TC4_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)

  REG_TC4_INTFLAG |= TC_INTFLAG_MC0;  // Clear the interrupt flags
  REG_TC4_INTENCLR = TC_INTENCLR_MC1 | TC_INTENCLR_OVF;     // Disable TC4 interrupts
  REG_TC4_INTENSET = TC_INTENSET_MC0; // Enable TC4 interrupts

  REG_TC4_CTRLA |= TC_CTRLA_PRESCALER_DIV64 | // Set freq to 3MHz
                   TC_CTRLA_WAVEGEN_MFRQ;   // Use the counter as the freq
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization

// Set up TC5 for the buffer prime callback
  REG_TC5_CTRLA |= TC_CTRLA_MODE_COUNT16;           // Set the counter to 16-bit mode
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization

  REG_TC5_COUNT16_CC0 = (24000000 / (timerFreq * 1024)) - 1;  // Set the TC4 CC0 register to the sample period
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization

  NVIC_DisableIRQ(TC5_IRQn);
  NVIC_ClearPendingIRQ(TC5_IRQn);
  NVIC_SetPriority(TC5_IRQn, 3);    // Set the Nested Vector Interrupt Controller (NVIC) priority for TC5 to 2 (not highest)
  NVIC_EnableIRQ(TC5_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)

  REG_TC5_INTFLAG |= TC_INTFLAG_MC0;  // Clear the interrupt flags
  REG_TC5_INTENCLR = TC_INTENCLR_MC1 | TC_INTENCLR_OVF;     // Disable TC5 interrupts
  REG_TC5_INTENSET = TC_INTENSET_MC0; // Enable TC4 interrupts

  REG_TC5_CTRLA |= TC_CTRLA_PRESCALER_DIV1024 | // Set freq to 3MHz
                   TC_CTRLA_WAVEGEN_MFRQ;   // Use the counter as the freq
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
}

//Function that is used to check if TC5 is done syncing
//returns true when it is done syncing

//This function enables TC5 and waits for it to be ready
void Timer_Start()
{
  TC5->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;  //set the CTRLA register
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
  TC4->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;  //set the CTRLA register
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
}

//Reset TC5 
void Timer_Reset()
{
  TC5->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
  while (TC5->COUNT16.CTRLA.bit.SWRST);
  TC4->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
  while (TC4->COUNT16.CTRLA.bit.SWRST);
}

//disable TC5
void Timer_Disable()
{
  TC4->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
  //TC5->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  //while (TC5->COUNT16.STATUS.bit.SYNCBUSY);   // Wait for synchronization
}
