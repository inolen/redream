@echo off

set INCLUDE=C:\Program Files\PellesC\Include\;C:\Program Files\PellesC\Include\Win\;
set LIB=C:\Program Files\PellesC\Lib\;C:\Program Files\PellesC\Lib\Win64\;
set name=BeaEngine

echo ____________________________________
echo *
echo *  COMPILATION with POCC.EXE (Pelles C)
echo *
echo ____________________________________

pocc /Tamd64-coff /Ze /W2 /DBEA_ENGINE_STATIC beaengineSources/%name%.c


echo ____________________________________
echo *
echo *   CREATE LIB with POLIB.EXE (Pelles C)
echo *
echo ____________________________________

"C:\Program Files\PellesC\Bin\PoLib" /MACHINE:X64 /out:%name%64.lib beaengineSources/%name%.obj
pause





