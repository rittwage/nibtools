a65: 

..\mnib.c: ..\mnib1541.inc ..\mnib1571.inc

..\mnib1541.a65: ..\mnib15x1.asm
	echo DRIVE=1541 > 1.tmp
	copy 1.tmp + ..\mnib15x1.asm ..\mnib1541.a65
	del 1.tmp

..\mnib1571.a65: ..\mnib15x1.asm
	echo DRIVE=1571 > 1.tmp
	copy 1.tmp + ..\mnib15x1.asm ..\mnib1571.a65
	del 1.tmp

.SUFFIXES: .a65

{..\}.a65{..\}.inc:
	..\..\WINDOWS\buildoneinc ..\.. $?
