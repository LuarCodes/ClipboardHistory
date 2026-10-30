@echo off
rem Builds ClipboardHistory.exe  -  works with MinGW-w64 (g++) or Visual Studio (cl)
cd /d "%~dp0"
where g++ >nul 2>nul && goto gcc
where cl  >nul 2>nul && goto msvc
echo.
echo No C++ compiler found.
echo  - Install MinGW-w64:   winget install BrechtSanders.WinLibs.POSIX.UCRT
echo  - or open "Developer Command Prompt for VS" and run build.bat there.
echo.
pause
exit /b 1

:gcc
echo Building with g++ ...
windres resource.rc -O coff -o resource.o || goto fail
g++ -O2 -std=c++17 -municode -mwindows -static -o ClipboardHistory.exe main.cpp resource.o -lcomctl32 -lshell32 || goto fail
del resource.o
goto ok

:msvc
echo Building with Visual Studio ...
rc /nologo resource.rc || goto fail
cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE main.cpp resource.res /Fe:ClipboardHistory.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO || goto fail
del *.obj resource.res
goto ok

:fail
echo.
echo BUILD FAILED
pause
exit /b 1

:ok
echo.
echo Done: ClipboardHistory.exe
pause
