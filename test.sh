PIN_D0=8
PIN_D1=9
PIN_D2=1
PIN_D3=0
PIN_D4=2
PIN_D5=3
PIN_D6=4
PIN_D7=5
PIN_BC=12
PIN_EN0=13
PIN_EN1=14
PIN_RES=10

BC_LATCH=0
BC_WRITE=1

# init pins
gpio mode $PIN_D0 out
gpio mode $PIN_D1 out
gpio mode $PIN_D2 out
gpio mode $PIN_D3 out
gpio mode $PIN_D4 out
gpio mode $PIN_D5 out
gpio mode $PIN_D6 out
gpio mode $PIN_D7 out
gpio mode $PIN_BC out
gpio mode $PIN_EN0 out
gpio mode $PIN_EN1 out
gpio mode $PIN_RES out

function latch_address
{
	gpio write $PIN_D0 $((($1 & 0x01) != 0))
	gpio write $PIN_D1 $((($1 & 0x02) != 0))
	gpio write $PIN_D2 $((($1 & 0x04) != 0))
	gpio write $PIN_D3 $((($1 & 0x08) != 0))
	gpio write $PIN_D4 $((($1 & 0x10) != 0))
	gpio write $PIN_D5 $((($1 & 0x20) != 0))
	gpio write $PIN_D6 $((($1 & 0x40) != 0))
	gpio write $PIN_D7 $((($1 & 0x80) != 0))

	gpio write $PIN_BC $BC_LATCH
	gpio write $PIN_EN0 1
	gpio write $PIN_EN1 1
	gpio write $PIN_EN0 0
	gpio write $PIN_EN1 0
}

function latch_value
{
	gpio write $PIN_D0 $((($1 & 0x01) != 0))
	gpio write $PIN_D1 $((($1 & 0x02) != 0))
	gpio write $PIN_D2 $((($1 & 0x04) != 0))
	gpio write $PIN_D3 $((($1 & 0x08) != 0))
	gpio write $PIN_D4 $((($1 & 0x10) != 0))
	gpio write $PIN_D5 $((($1 & 0x20) != 0))
	gpio write $PIN_D6 $((($1 & 0x40) != 0))
	gpio write $PIN_D7 $((($1 & 0x80) != 0))

	gpio write $PIN_BC $BC_WRITE
	gpio write $PIN_EN0 1
	gpio write $PIN_EN1 1
	gpio write $PIN_EN0 0
	gpio write $PIN_EN1 0
}

function write_value
{
	latch_address $1
	latch_value $2
}

# reset high (not reset)
gpio write $PIN_RES 1

# max vol, no env
write_value 0x08 0x0F
# enable chan A
write_value 0x07 0x3E

write_value 0x00 0x5D
write_value 0x01 0x01
write_value 0x00 0x5D
write_value 0x01 0x02
write_value 0x00 0x5D
write_value 0x01 0x03
