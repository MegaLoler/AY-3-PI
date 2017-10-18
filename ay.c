#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
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
#define PIN_RST 10

// ay ids
#define AY_0 1
#define AY_1 2
#define AY_BOTH 3

// output on bc2
#define BC_LATCH 0
#define BC_WRITE 1

// default clock freq to out to the ays until told otherwise
#define CLK_FREQ 1789773
//#define CLK_FREQ 3579545

// how much time between samples
#define SAMPLE_DELAY 1

// how many bytes the header of a vgm file is
#define HEADER_SIZE 256

// vgm data buffer
#define BUF_SIZE (1024 * 1024)
uint8_t* buffer;
uint8_t* playPointer;
uint8_t* eofPointer;

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

// pulses a reset signal to the ays
void resetChips()
{
	digitalWrite(PIN_RST, 0);
	delay(1);
	digitalWrite(PIN_RST, 1);
	delay(1);
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
	writeToRegister(0x09, 0x0F, AY_BOTH);
	writeToRegister(0x0A, 0x0F, AY_BOTH);
	writeToRegister(0x07, 0x38, AY_BOTH);

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
	writeToRegister(0x01, 0x01, AY_0);
	writeToRegister(0x00, 0xFD, AY_1);
	writeToRegister(0x01, 0x01, AY_1);
	writeToRegister(0x02, 0x5D, AY_0);
	writeToRegister(0x03, 0x02, AY_0);
	writeToRegister(0x02, 0xFD, AY_1);
	writeToRegister(0x03, 0x02, AY_1);
	writeToRegister(0x04, 0x5D, AY_0);
	writeToRegister(0x05, 0x03, AY_0);
	writeToRegister(0x04, 0xFD, AY_1);
	writeToRegister(0x05, 0x03, AY_1);
}

// how many samples ahead we are
int sampleDelta;

// how many bytes left of the header
int fileHeader;

// stores the current offset in file
int offset;

// 32bit buffer
uint32_t valueBuffer;
// how many bytes left to add to it
int valueBufRemaining;
// how many bytes have been added to it
int valueBufSize;

// reset the player
// newly expecting header
void resetPlayer()
{
	sampleDelta = 0;
	fileHeader = HEADER_SIZE;
	offset = 0;
	eofPointer = playPointer = buffer;
}

// reset everything, player and chips
void reset()
{
	resetPlayer();
	resetChips();
}

// function to setup collecting bytes onto the value buffer
void collectBytes(uint8_t byte, int bytes)
{
	valueBuffer = byte;
	valueBufSize = 1;
	valueBufRemaining = bytes - valueBufSize;
}

// this is the sample timer interrupt handler
void timerHandler(int sig)
{
	// while we are behind or on schedule..
	// and while there is data to be processed..
	// ..process bits
	while(sampleDelta <= 0 && playPointer != eofPointer)
	{
		// get the byte
		uint8_t byte = *(playPointer++);

		// collect any pending bytes to the value buffer
		if(valueBufRemaining > 0)
		{
			valueBuffer |= byte << (8 * valueBufSize);
			valueBufSize++;
			valueBufRemaining--;
		}

		// process bytes
		if(fileHeader > 0)
		{
			// one less byte in the header remains
			fileHeader--;

			// processes header bytes
			// remainig bytes of header
			if(offset == 0x34)
			{
				collectBytes(byte, 4);
			}
			else if(offset == 0x37)
			{
				fileHeader = valueBuffer - valueBufSize;
				printf("REM: 0x%x\n", valueBuffer);
			}
			// 32bit value of ay clock value
			if(offset == 0x74)
			{
				collectBytes(byte, 4);
			}
			else if(offset == 0x77)
			{
				gpioClockSet(PIN_CLK, valueBuffer);
				printf("CLOCK: 0x%i\n", valueBuffer);
			}
		}
		else
		{
			// processes vgm stream commands
			printf("CMD: 0x%x\n", byte);
			exit(0);
		}

		// increment the byte offset counter
		offset++;
	}

	// one sample processed
	// only process samples if we are passed the header
	// (that's when playback has "begun")
	if(fileHeader == 0) sampleDelta--;
	
	// setup next timer
	alarm(SAMPLE_DELAY);
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
	pinMode(PIN_RST, OUTPUT);

	// set up the clock pin
	pinMode(PIN_CLK, GPIO_CLOCK);
	gpioClockSet(PIN_CLK, CLK_FREQ);

	// reset the chips and player
	reset();

	// test
	//test();
	
	// setup the timer interrupt
	signal(SIGALRM, &timerHandler);
	alarm(SAMPLE_DELAY);
	
	// allocate buffer for vgm stream
	buffer = (uint8_t*)malloc(BUF_SIZE);
	if(buffer == NULL)
	{
		printf("error allocating memory ? ?\n");
		return 1;
	}
	playPointer = buffer;
	eofPointer = buffer;

	// read in data from stdin into the buffer forever
	uint8_t byte;
	while((byte = (uint8_t)getchar()) != EOF)
	{
		*(eofPointer++) = byte;
		while(eofPointer - buffer >= BUF_SIZE) eofPointer -= BUF_SIZE;
	}

	// bye
	return 0;
}
