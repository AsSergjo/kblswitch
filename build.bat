echo off
rc.exe /nologo /fo resource.res resource.rc
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /TC kblswitch.c
link.exe kblswitch.obj resource.res /SUBSYSTEM:WINDOWS /OUT:kblswitch.exe user32.lib gdi32.lib shell32.lib