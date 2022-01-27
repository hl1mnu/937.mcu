/*
 *		TCSD.C
 *
 *		Temperature Controlled Soldering Station
 *		Based on Hakko 937 Hardware
 */

#include <stdio.h> 
#include <ADuC832.h>

#define CONTROL_METHOD

#define uint8	unsigned char
#define uint16	unsigned int

#define TICK_SECOND		0x80
#define TICK_SUBSEC		0x40

#define OSCILLATOR 2097152
#define BAUD 9600

#define KEY_THRESHOLD	60
#define KEY_PRESSED		0x80
#define KEY_HANDLED		0x40
#define KEY_STATE		(KEY_PRESSED | KEY_HANDLED)
#define KEY_COUNT		~KEY_STATE

#define ADC_CH_TIP_TEMP	5

const uint8 font[16] = { 0xfc, 0x60, 0xda, 0xf2, 0x66, 0xb6, 0xbe, 0xe0,
                         0xfe, 0xf6, 0xee, 0x3e, 0x9c, 0x7a, 0x9e, 0x8e };

#ifdef __SDCC_mcs51
#define KEYLOCK		P1_3		// P1.3
#define TEMPSENS	P1_5 		// P1.5
#define KEYPAD		P1_6  		// P1.6

#define TEMPRD		P2_3		// P2.3
#define ROW1		P2_4		// P2.4
#define ROW2		P2_5		// P2.5
#define ROW3		P2_6		// P2.6
#define HEATER		P2_7		// P2.7
#else
sbit KEYLOCK = P1 ^ 3;	// P1.3
sbit TEMPSENS = P1 ^ ADC_CH_TIP_TEMP; // P1.5
sbit KEYPAD  = P1 ^ 6;  // P1.6

sbit TEMPRD = P2 ^ 3;	// P2.3
sbit ROW1 = P2 ^ 4;		// P2.4
sbit ROW2 = P2 ^ 5;		// P2.5
sbit ROW3 = P2 ^ 6;		// P2.6
sbit HEATER = P2 ^ 7;	// P2.7
#endif

uint8 disp_buff[3];		// display buffer
uint8 row_scan;			// the row presently being scanned

int zero_crossing_cnt = 0;

uint8 tick = 0;
uint8 key_plus, key_minus, key_star;

// control system elements
int g_SP = 0;	// setpoint			:	temperature in degC
int g_MV = 0;	// measured value	:	temperature in degC
int g_ADC = 0;
int g_AC = 0;	// actuator signal
int g_P = 4;	// coefficient

void FSM_KeyInput(void);

/*
 *	EEPROM Read long
 */
long EE_ReadLong(uint16 pg)
{
	unsigned long ld;
	uint8 *p = (uint8 *) (void *) &ld;

	EADRH = (uint8) ((pg & 0xff00) >> 8);
	EADRL = (uint8) (pg & 0x00ff);
	ECON = 0x01;	// 01H = READ

	// the data read into EDATA1, EDATA2, EDATA3, EDATA4
	*p++ = EDATA1;
	*p++ = EDATA2;
	*p++ = EDATA3;
	*p++ = EDATA4;

	return ld;
}

/*
 *	EEPROM Write Page
 */
int EE_WriteLong(uint16 pg, long ld)
{
	uint8 *p = (uint8 *) (void *) &ld;

	EADRH = (uint8) ((pg & 0xff00) >> 8);
	EADRL = (uint8) (pg & 0x00ff);

	// the data write into EDATA1, EDATA2, EDATA3, EDATA4
	EDATA1 = *p++;
	EDATA2 = *p++;
	EDATA3 = *p++;
	EDATA4 = *p++;

	ECON = 0x05;	// 05H = ERASE
	ECON = 0x02;	// 05H = WRITE
	ECON = 0x04;	// 04H = VERIFY

	// returns 1 if successful or 0 otherwise
	return (ECON == 0);
}

/*
 *	Display decimal number (0~999)
 */
void DisplayDecimal(int dec)
{
	if (dec == 511)
	{
		disp_buff[2] = 0xb6;	// 'S'
		disp_buff[1] = 0x02;	// '-'
		disp_buff[0] = 0x9e;	// 'E'
	}
	else
	{
		disp_buff[0] = font[dec % 10];
		dec /= 10;
		disp_buff[1] = font[dec % 10];
		dec /= 10;
		disp_buff[2] = font[dec % 10];
	}
}

/*
 *	Initiate A/D conversion and get data
 */
uint16 ReadADC(uint8 ch)
{
	int adc_ch = 0;
	int adc_da = 0;

	ADCCON2 |= ch;
	SCONV = 1;
	while (ADCCON3 & 0x80);

	adc_da = (int) ADCDATAH;
	adc_ch = (adc_da & 0xf0) >> 4;
	adc_da &= 0x0f;
	adc_da <<= 8;
	adc_da |= (int) ADCDATAL;

	return adc_da;
}

/*
 *	ISR for External Interrupt 0
 *
 *	- called at every zero-crossing
 *  - read tip temperature by ADC
 *  - switch heater on or off
 *
 */
#ifdef __SDCC_mcs51
void ISR_IE0(void) __interrupt (0)
#else
void ISR_IE0() interrupt 0
#endif
#ifdef CONTROL_METHOD
{
	// measure tip temperature
	TEMPRD = 1;
	g_ADC = ReadADC(ADC_CH_TIP_TEMP);
	TEMPRD = 0;

	g_MV = (g_ADC >> 3) - 20;

	if (g_MV > g_SP)		// measured temperature is too high, cool it down!
	{
		disp_buff[0] &= ~0x01;
		HEATER = 0;
	}
	else					// measured temperature is too low, heat it up!
	{
		disp_buff[0] |= 0x01;
		HEATER = 1;
	}
}
#else
{
	static int cycle_cnt = 0;

	if (!cycle_cnt)
	{
		// measure tip temperature
		TEMPRD = 1;
		g_MV = ReadADC(ADC_CH_TIP_TEMP);
		TEMPRD = 0;

		g_MV >>= 3;
		g_AC = g_P * (g_SP - g_MV) / 10;
	}

	if (cycle_cnt < g_AC)
	{
		disp_buff[0] |= 0x01;
		HEATER = 1;
	}
	else
	{
		disp_buff[0] &= ~0x01;
		HEATER = 0;
	}
	++cycle_cnt;
	cycle_cnt %= 10;
}
#endif

/*
 *	ISR for Timer/Counter 0 Interrupt
 *
 *	- called every 1/180 sec
 *	- refresh 7 segment LEDs with contents of disp_buff[]
 *	- check keypad and update key state variables: key_plus, key_minus, key_star
 *	- move to next row
 *	- mark on tick variable (bit TICK_SUBSEC)
 *
 */
#ifdef __SDCC_mcs51
void ISR_TF0(void) __interrupt	(1)
#else
void ISR_TF0() interrupt	1
#endif
{
	TH0 = 0xfc;		// timer : 03cbh = 971
	TL0 = 0x34;		// 	 : 2.097152 MHz / 12 / 971 = 180 Hz

	// select row
	switch (row_scan)
	{
	case 0:
		ROW1 = 0; ROW2 = 1; ROW3 = 1;
		P0 = ~disp_buff[row_scan];
		if (KEYPAD)
		{
			key_plus |= KEY_PRESSED;
			if ((key_plus & KEY_COUNT) < KEY_THRESHOLD) key_plus++;
		}
		else
			key_plus = 0;
		break;

	case 1:
		ROW1 = 1; ROW2 = 0; ROW3 = 1;
		P0 = ~disp_buff[row_scan];
		if (KEYPAD)
		{
			key_minus |= KEY_PRESSED;
			if ((key_minus & KEY_COUNT) < KEY_THRESHOLD) key_minus++;
		}
		else
			key_minus = 0;
		break;

	case 2:
		ROW1 = 1; ROW2 = 1; ROW3 = 0;
		P0 = ~disp_buff[row_scan];
		if (KEYPAD)
		{
			key_star |= KEY_PRESSED;
			if ((key_star & KEY_COUNT) < KEY_THRESHOLD) key_star++;
		}
		else 
			key_star = 0;
		break;

	default:
		break;
	}
	if (++row_scan >= sizeof(disp_buff))
	{
		row_scan = 0;
		tick |= TICK_SUBSEC;
	}
}

/*
 *	ISR for Time Interval Counter Interrupt
 *
 *	- called every second
 *	- just mark on tick variable (bit TICK_SECOND)
 *
 */
#ifdef __SDCC_mcs51
void ISR_TII(void) __interrupt (10)
#else
void ISR_TII() interrupt 10
#endif
{
	TIMECON &= ~0x04;		// clear TII bit
	tick |= TICK_SECOND;
}

/*
 *
 */
void OnTimeTick(int state, int val)
{
	static int blinking = 0;

	/*
	 *	On Half Second Tick:
	 */
	if (tick & TICK_SECOND)
	{
		tick &= ~TICK_SECOND;	// clear tick bit
printf("%d\n", g_ADC);
		switch (state)
		{
		case 0:		// steady state - display current temperature
			DisplayDecimal(g_MV);
			break;

		case 1:		// blinking digit A, 100's
			if (blinking)
				disp_buff[2] = 0;
			else
				DisplayDecimal(val);
			break;

		case 2:		// blinking digit B, 10's
			if (blinking)
				disp_buff[1] = 0;
			else
				DisplayDecimal(val);
			break;

		case 3:		// blinking digit C, 1's
			if (blinking)
				disp_buff[0] = 0;
			else
				DisplayDecimal(val);
			break;
		}
		blinking = !blinking;
	}

	/*
	 *	On Sub Second Tick
	 */
	if (tick & TICK_SUBSEC)
	{
		tick &= ~TICK_SUBSEC;	// clear tick bit

		if ((key_plus & KEY_COUNT) == KEY_THRESHOLD)
		{
			g_SP++;
			DisplayDecimal(g_SP);
		}
		if ((key_minus & KEY_COUNT) == KEY_THRESHOLD)
		{
			g_SP--;
			DisplayDecimal(g_SP);
		}
	}
}

/*
 *
 */
void FSM_KeyInput(void)
{
	static int state = 0;
	static int val = 0;

	OnTimeTick(state, val);

	switch (state)
	{
	case 0:
		if ((key_star & KEY_STATE) == KEY_PRESSED)
		{
			state = 1;
			val = g_SP;
			DisplayDecimal(val);
			key_star |= KEY_HANDLED;
		}
		break;

	case 1:
		if ((key_plus & KEY_STATE) == KEY_PRESSED)
		{
			if (val < 900) val += 100;
			DisplayDecimal(val);
			key_plus |= KEY_HANDLED;
		}
		else if ((key_minus & KEY_STATE) == KEY_PRESSED)
		{
			if (val > 100) val -= 100;
			DisplayDecimal(val);
			key_minus |= KEY_HANDLED;
		}
		else if ((key_star & KEY_STATE) == KEY_PRESSED)
		{
			state = 2;
			DisplayDecimal(val);
			key_star |= KEY_HANDLED;
		}
		break;
		
	case 2:
		if ((key_plus & KEY_STATE) == KEY_PRESSED)
		{
			if (val < 990) val += 10;
			DisplayDecimal(val);
			key_plus |= KEY_HANDLED;
		}
		else if ((key_minus & KEY_STATE) == KEY_PRESSED)
		{
			if (val > 10) val -= 10;
			DisplayDecimal(val);
			key_minus |= KEY_HANDLED;
		}
		else if ((key_star & KEY_STATE) == KEY_PRESSED)
		{
			state = 3;
			DisplayDecimal(val);
			key_star |= KEY_HANDLED;
		}
		break;

	case 3:
		if ((key_plus & KEY_STATE) == KEY_PRESSED)
		{
			if (val < 999) val += 1;
			DisplayDecimal(val);
			key_plus |= KEY_HANDLED;
		}
		else if ((key_minus & KEY_STATE) == KEY_PRESSED)
		{
			if (val > 1) val -= 1;
			DisplayDecimal(val);
			key_minus |= KEY_HANDLED;
		}
		else if ((key_star & KEY_STATE) == KEY_PRESSED)
		{
			g_SP = val;
	printf("Saving val=%d returns %d\n", val, EE_WriteLong(0, (long) val));
			state = 0;
			key_star |= KEY_HANDLED;
		}
		break;
	}
}

/*
 *	Program Main
 */
void main (void)
{
	HEATER = 0;

	//	Configure Time Interval Counter  
	TIMECON = 0x03;
	INTVAL = 0x40;		// 0.5 seconds

	//  Configure Timer/Counter 0 & 1
	TMOD = 0x11;		// Timer 1/0 M0 = 1
	TCON = 0x11;		// TR0 = 1, IT0 = 1
	TH0 = 0xfc;			// timer : 03cbh = 971
	TL0 = 0x34;			// 	 : 2.097152 MHz / 12 / 971 = 180 Hz

	//Configure the baud rate 9600
	T3CON = 0x82;
	T3FD  = 0x2D;
	SCON  = 0x52;

	P1 = (uint8) ~(0x48);			// set P1.6 & P1.3 as digital input

	// configure ADC
	ADCCON1 = 0xbc;
	ADCCON2 = 0x10;

	// read power-on default for Temperature Setpoint from EEPROM
	g_SP = (int) EE_ReadLong(0);

	//	Configure External Interrupt
	IEIP2 = 0xA4;		// enable TIC interrupt
	IE = 0x03;
	EA = 1;				// enable interrupts

	printf("Starting Program g_SP=%d\n", g_SP);
	
	while (1)
	{ 
		FSM_KeyInput();
	}
}

#ifdef __SDCC_mcs51
int putchar (int c)
{
	while (!TI)
		;
	TI = 0;
	SBUF = c;
	
	return 0;
}
#endif