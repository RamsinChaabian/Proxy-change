@echo off
echo Building Proxy Switcher v2.0 (Glass Edition)...
echo.

set PATH=E:\mingw64\bin;%PATH%

gcc -O2 -s proxy.c -o ProxySwitcher.exe ^
    -lgdi32 ^
    -luser32 ^
    -lkernel32 ^
    -ladvapi32 ^
    -lwininet ^
    -lshell32 ^
    -ldwmapi ^
    -lcomctl32 ^
    -lmsimg32 ^
    -mwindows

if %errorlevel% equ 0 (
    echo.
    echo ====================================
    echo Build Successful!
    echo Output: ProxySwitcher.exe
    dir ProxySwitcher.exe | findstr "ProxySwitcher"
    echo ====================================
) else (
    echo.
    echo Build Failed!
)

pause