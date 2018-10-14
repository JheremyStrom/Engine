@echo off

REM take away d from -MTd in release
set CommonCompilerFlags=-MTd -nologo -fp:fast -Gm- -GR- -EHa- -Od -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -DENGINE_INTERNAL=1 -DENGINE_SLOW=1 -FC -Z7
set CommonLinkerFlags= -incremental:no -opt:ref user32.lib gdi32.lib winmm.lib

IF NOT EXIST build mkdir build
pushd build
cls

REM 32-bit build
REM cl  %CommonCompilerFlags% ..\code\win32_engine.cpp /link -subsystem:windows,5.1 %CommonLinkerFlags%+

REM 64-bit build
del *.pdb > NUL 2> NUL
REM Optimization switches /O2 /Oi /fp:fast
echo WAITING FOR PDB > lock.tmp
cl  %CommonCompilerFlags% ..\code\engine.cpp  -Fmengine.map -LD /link -incremental:no /PDB:engine_%random%.pdb -EXPORT:GameGetSoundSamples -EXPORT:GameUpdateAndRender
del lock.tmp
cl  %CommonCompilerFlags% ..\code\win32_engine.cpp  -Fmwin32_engine.map /link %CommonLinkerFlags%

popd