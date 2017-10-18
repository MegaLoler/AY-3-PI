/* raspy pi 2xAY3 player!
 *
 * todo:
 *   better timer interrupt that isnt so unreliable??
 *   better clock generator? this isnt so accurate
 *   magic command to reset everything through stdin
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
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

// sample multiplier (lower is more stable but less than 1 loses sample resolution)
#define SAMPLE_MULTIPLIER 0.0625
// how much time between samples
#define SAMPLE_DELAY (1000000 / SAMPLE_MULTIPLIER / 44100)

// how many bytes the header of a vgm file is
#define HEADER_SIZE 256

// vgm data buffer
#define BUF_SIZE (1024 * 1024)
uint8_t* buffer;
uint8_t* playPointer;
uint8_t* eofPointer;

// debug mode? prints things yo
#define DEBUG
// prints all the commands
//#define VERBOSE
// prints every byte!
//#define REAL_VERBOSE

// wait a bit to let the chips register the signals
void pulseDelay()
{
	usleep(1);
	//delay(1);
}

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
	pulseDelay();
	digitalWrite(PIN_RST, 1);
	pulseDelay();
}

// pulses an enable signal to the ays given
void pulseEnable(int ay)
{
	if(ay & AY_0) digitalWrite(PIN_EN0, 1);
	if(ay & AY_1) digitalWrite(PIN_EN1, 1);
	pulseDelay();
	if(ay & AY_0) digitalWrite(PIN_EN0, 0);
	if(ay & AY_1) digitalWrite(PIN_EN1, 0);
	pulseDelay();
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

// stores specified eof offset
int eof;

// stores specified loop offset
int loop;

// end of sound data yet?
int end;

// 32bit buffer
uint32_t valueBuffer;
// how many bytes left to add to it
int valueBufRemaining;
// how many bytes have been added to it
int valueBufSize;

// new song
// newly expecting header
void newSong()
{
	fileHeader = HEADER_SIZE;
	offset = 0;
	end = 0;
	loop = 0;
	eof = 0;
	valueBufRemaining = 0;
}

// reset the player
void resetPlayer()
{
	sampleDelta = 0;
	eofPointer = playPointer = buffer;
	newSong();
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

// handle a command string
void handleCommand(int commandString)
{
	uint8_t* bytes = (uint8_t*)&commandString;
#ifdef DEBUG
#ifdef VERBOSE
	printf("CMD: 0x%x 0x%x 0x%x 0x%x\n", bytes[0], bytes[1], bytes[2], bytes[3]);
#endif
#endif

	// ay write
	if(bytes[0] == 0xA0)
	{
		if((bytes[1] & 0x80) == 0)
		{
			writeToRegister(bytes[1], bytes[2], AY_0);
		}
		else
		{
			writeToRegister(bytes[1] & 0x0F, bytes[2], AY_1);
		}
	}
	// wait nn nn samples
	else if(bytes[0] == 0x61)
	{
		int samples = bytes[1] + (bytes[2] << 8);
		sampleDelta += samples * SAMPLE_MULTIPLIER;
	}
	// wait 735 samples
	else if(bytes[0] == 0x62)
	{
		sampleDelta += 735 * SAMPLE_MULTIPLIER;
	}
	// wait 882 samples
	else if(bytes[0] == 0x63)
	{
		sampleDelta += 882 * SAMPLE_MULTIPLIER;
	}
	// end of sound data
	else if(bytes[0] == 0x66)
	{
		if(loop)
		{
			// get distance back to loop offset from here
			// plus an extra to account for this one
			int delta = offset - loop + 1;
			// jump back there
			offset -= delta;
			playPointer -= delta;
#ifdef DEBUG
			printf("Looping back to 0x%x!\n", offset);
#endif
		}
		else
		{
			// end of sound data, ignore rest
			end = 1;
#ifdef DEBUG
			printf("End of sound data.\n");
#endif
		}
	}
	// wait n+1 samples
	else if((bytes[0] & 0xF0) == 0x70)
	{
		int samples = (bytes[0] & 0x0F) + 1;
		sampleDelta += samples * SAMPLE_MULTIPLIER;
	}
	else
	{
#ifdef DEBUG
		printf("UNKNOWN CMD: 0x%x 0x%x 0x%x 0x%x\n", bytes[0], bytes[1], bytes[2], bytes[3]);
#endif
	}
}

// this is the sample timer interrupt handler
void timerHandler(int sig)
{
	// while we are behind or on schedule..
	// and while there is data to be processed..
	// ..process bits
	while(sampleDelta <= 0 && playPointer != eofPointer)
	{
		// if this is the end of the file then.. prepare for new vgm file data!
		if(eof && offset == eof)
		{
#ifdef DEBUG
			printf("End of file reached.\n");
#endif
			newSong();
			continue;
		}

		// get the byte
		uint8_t byte = *(playPointer++);

		// as long as we havent reached the end of sound data!
		if(!end)
		{
#ifdef DEBUG
#ifdef VERBOSE
#ifdef REAL_VERBOSE
			printf("BYTE: 0x%x\n", byte);
#endif
#endif
#endif

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
				// remainig bytes of file
				if(offset == 0x04)
				{
					collectBytes(byte, 4);
				}
				else if(offset == 0x07)
				{
					eof = valueBuffer ? (valueBuffer + 0x04) : 0;
#ifdef DEBUG
					printf("EOF: 0x%x\n", eof);
#endif
				}
				// loop offset
				else if(offset == 0x1C)
				{
					collectBytes(byte, 4);
				}
				else if(offset == 0x1F)
				{
					loop = valueBuffer ? (valueBuffer - 0x1C) : 0;
#ifdef DEBUG
					printf("LOOP: 0x%x\n", loop);
#endif
				}
				// remainig bytes of header
				else if(offset == 0x34)
				{
					collectBytes(byte, 4);
				}
				else if(offset == 0x37)
				{
					fileHeader = valueBuffer - valueBufSize;
#ifdef DEBUG
					printf("REM: 0x%x\n", fileHeader);
#endif
				}
				// 32bit value of ay clock value
				else if(offset == 0x74)
				{
					collectBytes(byte, 4);
				}
				else if(offset == 0x77)
				{
					gpioClockSet(PIN_CLK, valueBuffer);
#ifdef DEBUG
					printf("CLOCK: %ihz\n", valueBuffer);
#endif
				}

				// clear the value buffer before reading commands
				if(fileHeader == 0) valueBuffer = 0;
			}
			else
			{
				// processes vgm stream bytes
				// collect whole command strings
				if(valueBufRemaining == 0)
				{
					if(valueBuffer)
					{
						handleCommand(valueBuffer);
						valueBuffer = 0;
					}
					else
					{
						if((byte & 0xF0) == 0xE0)
						{
							collectBytes(byte, 5);
						}
						else if((byte & 0xF0) >= 0xC0 && (byte & 0xF0) <= 0xD0)
						{
							collectBytes(byte, 4);
						}
						else if(((byte & 0xF0) >= 0xA0 && (byte & 0xF0) <= 0xB0) || (byte >= 0x51 && byte <= 0x61))
						{
							collectBytes(byte, 3);
						}
						else if(byte == 0x4F || byte == 0x50)
						{
							collectBytes(byte, 2);
						}
						else
						{
							handleCommand(byte);
						}
					}
				}
			}
		}

		// increment the byte offset counter
		offset++;
	}

	// one sample processed
	// only process samples if we are passed the header
	// (that's when playback has "begun")
	if(fileHeader == 0) sampleDelta--;
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
	struct itimerval tout_val;

	tout_val.it_interval.tv_sec = 0;
	tout_val.it_interval.tv_usec = SAMPLE_DELAY;
	tout_val.it_value.tv_sec = 0;
	tout_val.it_value.tv_usec = SAMPLE_DELAY;
	setitimer(ITIMER_REAL, &tout_val,0);

	signal(SIGALRM, timerHandler);
	
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
	int byte;
	while((byte = getchar()) != -1)
	{
		*(eofPointer++) = (uint8_t)byte;
		while(eofPointer - buffer >= BUF_SIZE) eofPointer -= BUF_SIZE;
	}

	// wait forever ? ?
	while(1) delay(100);

	// bye
	return 0;
}
