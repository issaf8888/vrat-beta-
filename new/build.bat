@echo off
echo Compiling application icon resources...
windres resource.rc -O coff -o resource.res
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Resource compilation failed
    exit /b %ERRORLEVEL%
)

echo Compiling vrat.exe with g++ (LLVM-MinGW)...
g++ -O3 -std=c++17 main.cpp resource.res -lgdiplus -lgdi32 -luser32 -ldwmapi -o vrat.exe
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] vrat.exe successfully built with logo icon!
) else (
    echo [ERROR] Build failed with error code %ERRORLEVEL%
)
