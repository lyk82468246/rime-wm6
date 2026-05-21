@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat"
devenv rime-wm6\rime-wm6.sln /Build "Debug|Windows Mobile 6 Professional SDK (ARMV4I)"