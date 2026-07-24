import serial
import time
import pyttsx3
import os
import pygame

os.system('cls' if os.name == 'nt' else 'clear')

Serial = serial.Serial(port="/dev/ttyUSB0", baudrate=115200)

Serial.readline()