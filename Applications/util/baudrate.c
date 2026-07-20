/* baudrate.c - Utility to set baudrate of a tty
 *
 * Copyright (C) 2026 Henrik Löfgren, All rights reserved.
 *
 * This file is part of FUZIX Operating System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>

static struct termios termcur;
static int ttyfd = -1;

static int baud[] = {
    50,     /* B50 */
    75,     /* B75 */
    110,    /* B110 */
    134,    /* B134 */
    150,    /* B150 */
    300,    /* B300 */
    600,    /* B600 */
    1200,   /* B1200 */
    2400,   /* B2400 */
    4800,   /* B4800 */
    9600,   /* B9600 */
    19200,  /* B19200 */
    38400,  /* B38400 */
    57600,  /* B57600 */
    115200, /* B115200 */
};
  
static speed_t speed[] = {
    B50,
    B75,
    B110,
    B134,
    B150,
    B300,
    B600,
    B1200,
    B2400,
    B4800,
    B9600,
    B19200,
    B38400,
    B57600,
    B115200,
};

static int parsespeed(char *str, speed_t *s) {
    register int i;
    register int b = atoi(str);
    for(i =0; i<sizeof(baud) / sizeof(baud[0]); i++) {
        if(baud[i] == b) {
            *s = speed[i];
            return 1;
        }
    }
    return 0;
}   
    
static void usage(void)
{
    fputs("Usage: baudrate [tty] [baudrate]\n", stderr);
}


int main(int argc, char *argv[]) 
{
    int fd;
    speed_t speedval = 0;

    if(argc != 3) {
        usage();
        return 1;
    }

    if(!parsespeed(argv[2], &speedval)) {
        fprintf(stderr, "Invalid baudrate: %s\n", optarg);
        return 1;
    }

      
    ttyfd = open(argv[1], O_RDWR | O_NOCTTY);
    if (ttyfd < 0) {
        perror(argv[2]);
        return 1;
    }

    tcgetattr(fd, &termcur);

    if(cfsetospeed(&termcur, (speed_t)speedval) < 0 ||
           tcsetattr(ttyfd, TCSAFLUSH, &termcur) < 0) {
            perror("baudrate");
            close(ttyfd);
            exit(1);
    }
    close(ttyfd);
    exit(0); 
}

