#include <stdio.h>
#include <wiringPi.h>

// define the pin numberings for the ay connections
#define PIN_D0 8
#define PIN_D1 9
#define PIN_D2 1
#define PIN_D3 0
#define PIN_D4 2
#define PIN_D5 3
#define PIN_D6 4
#define PIN_D7 5
#define PIN_BC 12 // (bc2) (bc1 is tied low)
#define PIN_EN0 13 // (bdir of ay 1)
#define PIN_EN1 14 // (bdir of ay 2)
#define PIN_CLK 7

// ay ids
#define AY_0 1
#define AY_1 2
#define AY_BOTH 3

// output on bc2
#define BC_LATCH 0
#define BC_WRITE 1

// clock freq to out to the ays
#define CLK_FREQ 1789773
//#define CLK_FREQ 3579545

// put a value on the data lines
void dataOut(int data)
{
	digitalWrite(PIN_D0, (data & 1) ? HIGH : LOW);
	digitalWrite(PIN_D1, (data & (1 << 1)) ? HIGH : LOW);
	digitalWrite(PIN_D2, (data & (1 << 2)) ? HIGH : LOW);
	digitalWrite(PIN_D3, (data & (1 << 3)) ? HIGH : LOW);
	digitalWrite(PIN_D4, (data & (1 << 4)) ? HIGH : LOW);
	digitalWrite(PIN_D5, (data & (1 << 5)) ? HIGH : LOW);
	digitalWrite(PIN_D6, (data & (1 << 6)) ? HIGH : LOW);
	digitalWrite(PIN_D7, (data & (1 << 7)) ? HIGH : LOW);
}

// pulses an enable signal to the ays given
void pulseEnable(int ay)
{
	if(ay & AY_0) digitalWrite(PIN_EN0, 1);
	if(ay & AY_1) digitalWrite(PIN_EN1, 1);
	delay(1);
	if(ay & AY_0) digitalWrite(PIN_EN0, 0);
	if(ay & AY_1) digitalWrite(PIN_EN1, 0);
	delay(1);
}

// latch an address on one of the ays (or both) 0 = first, 1 = second, 2 = both
void latchAddress(int address, int ay)
{
	dataOut(address);
	digitalWrite(PIN_BC, BC_LATCH);
	pulseEnable(ay);
}

// write to the selected register
void latchValue(int value, int ay)
{
	dataOut(value);
	digitalWrite(PIN_BC, BC_WRITE);
	pulseEnable(ay);
}

// finally, use this to write a value to an ay register
void writeToRegister(int address, int value, int ay)
{
	latchAddress(address, ay);
	latchValue(value, ay);
}

// testing notes
void test()
{
	writeToRegister(0x08, 0x0F, AY_BOTH);
	writeToRegister(0x07, 0x3E, AY_BOTH);

	writeToRegister(0x00, 0x5D, AY_0);
	writeToRegister(0x01, 0x01, AY_0);
	writeToRegister(0x00, 0x7D, AY_1);
	writeToRegister(0x01, 0x01, AY_1);
	delay(500);
	writeToRegister(0x00, 0x5D, AY_0);
	writeToRegister(0x01, 0x02, AY_0);
	writeToRegister(0x00, 0x7D, AY_1);
	writeToRegister(0x01, 0x02, AY_1);
	delay(500);
	writeToRegister(0x00, 0x5D, AY_0);
	writeToRegister(0x01, 0x04, AY_0);
	writeToRegister(0x00, 0x7D, AY_1);
	writeToRegister(0x01, 0x04, AY_1);
}

// start
int main()
{
	// setup wiring pi
	if(wiringPiSetup() == -1)
	{
		printf("error initing wiringpi ? ?\n");
		return 1;
	}

	// set all the pins to out mode
	pinMode(PIN_D0, OUTPUT);
	pinMode(PIN_D1, OUTPUT);
	pinMode(PIN_D2, OUTPUT);
	pinMode(PIN_D3, OUTPUT);
	pinMode(PIN_D4, OUTPUT);
	pinMode(PIN_D5, OUTPUT);
	pinMode(PIN_D6, OUTPUT);
	pinMode(PIN_D7, OUTPUT);
	pinMode(PIN_BC, OUTPUT);
	pinMode(PIN_EN0, OUTPUT);
	pinMode(PIN_EN1, OUTPUT);

	// set up the clock pin
	pinMode(PIN_CLK, GPIO_CLOCK);
	gpioClockSet(PIN_CLK, CLK_FREQ);

	// test
	test();

	// bye
	return 0;
}
