@echo off
cl /O2 /LD /Femcupdate.dll /Iinc mcupdate.c /link /nodefaultlib /subsystem:native /entry:PocMain
signtool sign /f selfsignedwin2.pfx /fd sha256 mcupdate.dll
