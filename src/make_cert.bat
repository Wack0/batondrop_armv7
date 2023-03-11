@echo off
makecert -r -pe -n "CN=baton drop armv7" -a sha256 -cy end -sky signature -eku 1.3.6.1.5.5.7.3.3,1.3.6.1.4.1.311.10.3.6,1.3.6.1.4.1.311.10.3.21 -len 1024 -sv selfsignedwin2.pvk selfsignedwin2.cer
pvk2pfx -pvk selfsignedwin2.pvk -spc selfsignedwin2.cer -pfx selfsignedwin2.pfx