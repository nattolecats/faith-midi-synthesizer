@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
set "INCLUDE=%CD%\.deps\sdk\c\Include\10.0.26100.0\um;%CD%\.deps\sdk\c\Include\10.0.26100.0\shared;%CD%\.deps\sdk\c\Include\10.0.26100.0\ucrt;%INCLUDE%"
set "LIB=%CD%\.deps\sdk-x86\c\um\x86;%CD%\.deps\sdk-x86\c\ucrt\x86;%LIB%"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /MT /utf-8 /DUNICODE /D_UNICODE src\settings.cpp src\host.cpp /FeFaithMidiSettings.exe /Fobuild\ /link /SUBSYSTEM:WINDOWS winmm.lib comctl32.lib user32.lib gdi32.lib advapi32.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /MT /utf-8 /DUNICODE /D_UNICODE /LD src\driver.cpp /FeFaithMidi32.dll /Fobuild\driver32.obj /link /DEF:src\driver32.def /IMPLIB:build\FaithMidi32.lib winmm.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set "INCLUDE=%CD%\.deps\sdk\c\Include\10.0.26100.0\um;%CD%\.deps\sdk\c\Include\10.0.26100.0\shared;%CD%\.deps\sdk\c\Include\10.0.26100.0\ucrt;%INCLUDE%"
set "LIB=%CD%\.deps\sdk-x64\c\um\x64;%CD%\.deps\sdk-x64\c\ucrt\x64;%LIB%"
cl /nologo /std:c++17 /EHsc /W4 /MT /utf-8 /DUNICODE /D_UNICODE /LD src\driver.cpp /FeFaithMidi64.dll /Fobuild\driver64.obj /link /DEF:src\driver64.def /IMPLIB:build\FaithMidi64.lib winmm.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
exit /b %errorlevel%
