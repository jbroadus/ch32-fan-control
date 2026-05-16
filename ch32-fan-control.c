#include "ch32fun.h"
#include <stdio.h>

/* Pin connections:
   PWM (out TIM1 CH1N) on PD0
   Encoder S1 (in) on PD2
   Encoder S2 (in) on PD3

   Standard CPU fans use a 25kHz PWM input.
   We're using a 48MHz HCLK.
*/
#define PWM_PERIOD 1920
#define PWM_STEP 200

#define CFGLR_SHIFT(i) ((i) * 4)

/*! Initialize TIM1 for PWM on PD0
 * 
 * This was adapted from the ch32fun tim1_pwm example by Eric Brombaugh.
 */
void t1pwm_init( void )
{
  // Enable GPIOD and TIM1
  RCC->APB2PCENR |=
    RCC_APB2Periph_GPIOD |
    RCC_APB2Periph_TIM1;

  // PD0 is T1CH1N, 50MHz Output alt func, open drain
  GPIOD->CFGLR &= ~(0xf<<(CFGLR_SHIFT(0)));
  GPIOD->CFGLR |= (GPIO_Speed_50MHz | GPIO_CNF_OUT_OD_AF)<<CFGLR_SHIFT(0);

  // Reset TIM1 to init all regs
  RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
  RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;

  // CTLR1: default is up, events generated, edge align
  // SMCFGR: default clk input is CK_INT

  // Prescaler 
  TIM1->PSC = 0x0000;

  // Auto Reload - sets period
  TIM1->ATRLR = PWM_PERIOD;

  // Reload immediately
  TIM1->SWEVGR |= TIM_UG;

  // Enable CH1N output, positive pol
  TIM1->CCER |= TIM_CC1NE | TIM_CC1NP;

  // CH1 Mode is output, PWM1 (CC1S = 00, OC1M = 110)
  TIM1->CHCTLR1 |= TIM_OC1M_2 | TIM_OC1M_1;

  // Start at 100% width which is low speed.
  TIM1->CH1CVR = PWM_PERIOD;

  // Enable TIM1 outputs
  TIM1->BDTR |= TIM_MOE;

  // Enable TIM1
  TIM1->CTLR1 |= TIM_CEN;
}

/*! Set up rotary encoder inputs on PD3 and PD4
 *
 */
void encoder_init() {
    /* Enable GPIOD and AFIO */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;

    /* Encoder S1 on PD2 */
    GPIOD->CFGLR &= ~(0xF << CFGLR_SHIFT(2));
    GPIOD->CFGLR |=  (GPIO_CNF_IN_PUPD << CFGLR_SHIFT(2));
    GPIOD->OUTDR |=  (1 << 2);

    /* Encoder S2 on PD3 */
    GPIOD->CFGLR &= ~(0xF << CFGLR_SHIFT(3));
    GPIOD->CFGLR |=  (GPIO_CNF_IN_PUPD << CFGLR_SHIFT(3));
    GPIOD->OUTDR |=  (1 << 3);

    /* Set up interrupt on S1 (PD2) */
    AFIO->EXTICR = AFIO_EXTICR_EXTI2_PD;
    EXTI->INTENR = EXTI_INTENR_MR2;
    // Falling edge
    EXTI->FTENR = EXTI_FTENR_TR2;

    NVIC_EnableIRQ(EXTI7_0_IRQn);
}

// I see a lot of bounce in my rotary encoder, so I'm just setting the
// rotary direction here and using the delay in the loop as a crude debounce.
// This mostly works, but I do see some bounce-caused direction errors when
// moving the wheel quickly. There are better solutions, but this will work
// for controlling my fan.
static int gDir = 0;

static void rotaryDown() {
  gDir = -1;
}

static void rotaryUp() {
  gDir = 1;
}

/* EXTI ISR (lines 0-7 share one vector on CH32V003) */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void)
{
  if (EXTI->INTFR & (1 << 2)) {
    // S1 falling edge
    if ((GPIOD->INDR & (1 << 3)) == 0) {
      // S2 low - counter-clockwise
      rotaryDown();
    } else {
      // S2 high - clockwise
      rotaryUp();
    }

    // Clear
    EXTI->INTFR |= (1 << 2);
  }
}

int main()
{
  SystemInit();
  Delay_Ms( 100 );

  printf("Initializing...\n\r");
  t1pwm_init();
  encoder_init();

  printf("Starting...\n\r");
  int width = PWM_PERIOD;
  while(1) {
    // Read and clear direction set by IRQ.
    NVIC_DisableIRQ(EXTI7_0_IRQn);
    int dir = gDir;
    gDir = 0;
    NVIC_EnableIRQ(EXTI7_0_IRQn);

    if (dir) {
      // Using TICH1N, so invert.
      width += PWM_STEP * dir * -1;
      if (width < 0)
        width = 0;
      else if (width > PWM_PERIOD)
        width = PWM_PERIOD;
      printf("New pulse width: %d...\n\r", width);
    }

    // Update the pulse width.
    TIM1->CH1CVR = width;

    // Delay to allow rotary encoder debounce.
    Delay_Ms( 5 );
  }
}
