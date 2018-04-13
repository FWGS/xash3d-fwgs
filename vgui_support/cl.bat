 set MSVCDir=Z:\path\to\msvc
 set INCLUDE=%MSVCDir%\include
 set LIB=%MSVCDir%\lib
 set PATH=%MSVCDir%\bin;%PATH%
cl.exe %*