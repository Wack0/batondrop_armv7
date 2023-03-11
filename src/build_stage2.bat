@echo off
cl /O2 /LD /Festage2.dll /Iinc stage2.c /link /nodefaultlib /subsystem:native /entry:Stage2Entry divide.obj