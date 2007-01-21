/*
	drive.c - (C) Pete Rittwage, Markus Brenner, and friends.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"

unsigned char *floppy_code = NULL;
unsigned int lpt[4];
int lpt_num;
extern int drivetype;
unsigned int floppybytes;
extern CBM_FILE fd;

void ARCH_SIGNALDECL
handle_signals(int sig)
{
	/* Ignore multiple presses of ^C */
	signal(SIGINT, SIG_IGN);
	printf("\nExit requested by user. ");
	exit(1);
}

void ARCH_SIGNALDECL
handle_exit(void)
{
	send_mnib_cmd(fd, FL_RESET);
	printf("\nResetting drive... ");
	cbm_reset(fd);
#ifndef DJGPP
	cbm_driver_close(fd);
#endif
}


int
compare_extension(char * filename, char * extension)
{
	char *dot;

	dot = strrchr(filename, '.');
	if (dot == NULL)
		return (0);

	for (++dot; *dot != '\0'; dot++, extension++)
		if (tolower(*dot) != tolower(*extension))
			return (0);

	if (*extension == '\0')
		return (1);
	else
		return (0);
}

int
upload_code(CBM_FILE fd, BYTE drive)
{
	unsigned int databytes;
	int ret;

    static BYTE floppycode1541[] = {
#include "nibtools_1541.inc"
    };

    static BYTE floppycode1571[] = {
#include "nibtools_1571.inc"
    };

    switch (drivetype)
    {
    case 1571:
        floppy_code = floppycode1571;
        databytes = sizeof(floppycode1571);
        break;

    case 1541:
        floppy_code = floppycode1541;
        databytes = sizeof(floppycode1541);
        break;

	default:
		printf("Unsupported drive type\n");
		return -1;
    }

	printf("Uploading floppy-side code...\n");
	ret = cbm_upload(fd, drive, 0x300, floppy_code, databytes);
	if (ret < 0)
		return (ret);
	floppybytes = databytes;

	return 0;
}

int
test_par_port(CBM_FILE fd)
{
	unsigned int i, rv;

	send_mnib_cmd(fd, FL_TEST);

	for (i = 0, rv = 1; i < 0x100; i++)
		if (cbm_parallel_burst_read(fd) != i)
			rv = 0;

	if (cbm_parallel_burst_read(fd) != 0)
		rv = 0;
	return (rv);
}

int
verify_floppy(CBM_FILE fd)
{
	unsigned int i, rv;

	rv = 1;
	send_mnib_cmd(fd, FL_VERIFY_CODE);
	for (i = 0; i < floppybytes; i++)
	{
		if (cbm_parallel_burst_read(fd) != floppy_code[i])
		{
			rv = 0;
			printf("diff: %d\n", i);
		}
	}
	for (; i < 0x0800 - 0x0300; i++)
		cbm_parallel_burst_read(fd);

	if (cbm_parallel_burst_read(fd) != 0)
		rv = 0;
	return (rv);
}

#ifdef DJGPP
int
find_par_port(CBM_FILE fd)
{
	int i;
	for (i = 0; set_par_port(i); i++)
	{
		if (test_par_port(fd))
		{
			printf(" Found!\n");
			return (1);
		}
		printf(" no\n");
	}
	return (0);		/* no parallel port found */
}
#endif // DJGPP

void
send_mnib_cmd(CBM_FILE fd, unsigned char cmd)
{
	cbm_parallel_burst_write(fd, 0x00);
	cbm_parallel_burst_write(fd, 0x55);
	cbm_parallel_burst_write(fd, 0xaa);
	cbm_parallel_burst_write(fd, 0xff);
	cbm_parallel_burst_write(fd, cmd);
}

void
set_full_track(CBM_FILE fd)
{
	send_mnib_cmd(fd, FL_MOTOR);
	cbm_parallel_burst_write(fd, 0xfc);	/* $1c00 CLEAR mask (clear stepper bits) */
	cbm_parallel_burst_write(fd, 0x02);	/* $1c00 SET mask (stepper bits = %10) */
	cbm_parallel_burst_read(fd);
	delay(500);			/* wait for motor to step */
}

void
motor_on(CBM_FILE fd)
{
	send_mnib_cmd(fd, FL_MOTOR);
	cbm_parallel_burst_write(fd, 0xf3);	/* $1c00 CLEAR mask */
	cbm_parallel_burst_write(fd, 0x0c);	/* $1c00 SET mask (LED + motor ON) */
	cbm_parallel_burst_read(fd);
	delay(500);			/* wait for motor to turn on */
}

void
motor_off(CBM_FILE fd)
{
	send_mnib_cmd(fd, FL_MOTOR);
	cbm_parallel_burst_write(fd, 0xf3);	/* $1c00 CLEAR mask */
	cbm_parallel_burst_write(fd, 0x00);	/* $1c00 SET mask (LED + motor OFF) */
	cbm_parallel_burst_read(fd);
	delay(500);			/* wait for motor to turn off */
}

void
step_to_halftrack(CBM_FILE fd, int halftrack)
{
	send_mnib_cmd(fd, FL_STEPTO);
	cbm_parallel_burst_write(fd, (__u_char) ((halftrack != 0) ? halftrack : 1));
	cbm_parallel_burst_read(fd);
}

unsigned int
track_capacity(CBM_FILE fd)
{
	unsigned int capacity;
	send_mnib_cmd(fd, FL_CAPACITY);
	capacity = (unsigned int) cbm_parallel_burst_read(fd);
	capacity |= (unsigned int) cbm_parallel_burst_read(fd) << 8;
	return (capacity);
}

int
reset_floppy(CBM_FILE fd, BYTE drive)
{
	char cmd[80];
	int ret;

	/* Turn on motor, go to track 18, send reset cmd to drive code */
	motor_on(fd);
	step_to_halftrack(fd, 18*2);
	send_mnib_cmd(fd, FL_RESET);
	printf("drive reset...\n");
	delay(5000);
	cbm_listen(fd, drive, 15);

	/* Send Initialize command */
	ret = cbm_raw_write(fd, "I", 1);
	if (ret < 0) {
		printf("reset_floppy: error %d initializing\n", ret);
		return (ret);
	}
	cbm_unlisten(fd);
	delay(5000);

	/* Begin executing drive code at the start again */
	sprintf(cmd, "M-E%c%c", 0x00, 0x03);
	cbm_listen(fd, drive, 15);
	ret = cbm_raw_write(fd, cmd, 5);
	if (ret < 0) {
		printf("reset_floppy: error %d sending cmd\n", ret);
		return (ret);
	}
	cbm_unlisten(fd);
	cbm_parallel_burst_read(fd);
	return (0);
}

int
set_density(CBM_FILE fd, int density)
{
	send_mnib_cmd(fd, FL_DENSITY);
	cbm_parallel_burst_write(fd, density_branch[density]);
	cbm_parallel_burst_write(fd, 0x9f);
	cbm_parallel_burst_write(fd, bitrate_value[density]);
	cbm_parallel_burst_read(fd);

	return (density);
}

/* $13d6 */
int
set_bitrate(CBM_FILE fd, int density)
{
	send_mnib_cmd(fd, FL_MOTOR);
	cbm_parallel_burst_write(fd, 0x9f);			/* $1c00 CLEAR mask */
	cbm_parallel_burst_write(fd, bitrate_value[density]);	/* $1c00 SET mask */
	cbm_parallel_burst_read(fd);
	return (density);
}

/* $13bc */
BYTE
set_default_bitrate(CBM_FILE fd, int track)
{
	BYTE density;

	density = speed_map_1541[(track / 2) - 1];

	send_mnib_cmd(fd, FL_DENSITY);
	cbm_parallel_burst_write(fd, density_branch[density]);
	cbm_parallel_burst_write(fd, 0x9f);			/* $1c00 CLEAR mask */
	cbm_parallel_burst_write(fd, bitrate_value[density]);	/* $1c00 SET mask */
	cbm_parallel_burst_read(fd);
	return (density);
}

void perform_bump(CBM_FILE fd, BYTE drive)
{
	char cmd[80];
	char byte;
	int rv, count;

	printf("Bumping...\n");

	/* Set job to run on track 1, sector 0 */
	sprintf(cmd, "M-W%c%c%c%c%c", 6, 0, 2, 1, 0);
	if (cbm_exec_command(fd, drive, cmd, 8) != 0) {
		printf("seek track 1 failed, exiting\n");
		exit(4);
	}

	/* Send bump command */
	byte = 0xc0;
	sprintf(cmd, "M-W%c%c%c%c", 0, 0, 1, byte);
	if (cbm_exec_command(fd, drive, cmd, 7) != 0) {
		printf("bump command failed, exiting\n");
		exit(4);
	}
	delay(2000);

	/* Wait until command has been completed (high bit=0) */
	count = 10;
	sprintf(cmd, "M-R%c%c", 0, 0);
	while ((byte & 0x80) != 0 && count-- != 0) {
		delay(500);
		if (cbm_exec_command(fd, drive, cmd, 5) != 0) {
			printf("bump m-r failed, exiting\n");
			exit(4);
		}
		rv = cbm_talk(fd, drive, 15);
		if (rv != 0) {
			printf("bump talk failed: %d\n", rv);
			exit(4);
		}
		rv = cbm_raw_read(fd, &byte, sizeof(byte));
		cbm_untalk(fd);
		if (rv != sizeof(byte)) {
			printf("bump raw read failed: %d\n", rv);
			exit(4);
		}
	}

	/* Check if status was 1 (OK) */
	if (byte != 1) {
		printf("bump status was error: %#x\n", byte);
		exit(4);
	}
}
