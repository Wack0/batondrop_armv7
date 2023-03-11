@echo off
cl /O2 /LD /Feboot.efi /Iinc boot.c /link /nodefaultlib /subsystem:efi_application /entry:EfiMain