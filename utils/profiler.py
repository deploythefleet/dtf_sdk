#!/usr/bin/env python
import serial
import sys
import time
import re

if __name__ == "__main__":
    if(len(sys.argv) < 2):
        print("Usage: profiler.py [name] [device path (Defaults to /dev/ttyUSB0)]")
        sys.exit(1)

    device_path = '/dev/ttyUSB0'
    if(len(sys.argv) == 3):
        device_path = sys.argv[2]

    serial = serial.Serial(port=device_path, baudrate=115200, timeout=10)
    filename = time.strftime(f'{sys.argv[1]}_perf_%M.txt')
    with open(filename, "w", encoding="utf-8") as output_file:
        try:
            print("Capturing data from device")
            while(True):
                line = serial.read_until().decode(errors='replace').replace('\033', '')
                output_file.write(re.sub(r'\[0;..m|\[0m', '', line))

        except KeyboardInterrupt:
            serial.close()
            print(f'Done capturing data to {filename}')
            sys.exit()
        