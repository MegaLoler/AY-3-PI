#!/bin/sh
./set_clock2.sh
nc -k -l 1234 | ./ay
