a65: 

..\nibwrite.c: ..\nibtools_1541.inc ..\nibtools_1571.inc

..\nibtools_1541.a65: ..\nibtools_15x1.asm
	echo DRIVE=1541 > 1.tmp
	copy 1.tmp + ..\nibtools_15x1.asm ..\nibtools_1541.a65
	del 1.tmp

..\nibtools_1571.a65: ..\nibtools_15x1.asm
	echo DRIVE=1571 > 1.tmp
	copy 1.tmp + ..\nibtools_15x1.asm ..\nibtools_1571.a65
	del 1.tmp

.SUFFIXES: .a65

{..\}.a65{..\}.inc:
	..\..\WINDOWS\buildoneinc ..\.. $?
