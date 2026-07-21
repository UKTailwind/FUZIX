/* rx.c - Simple xmodem file receiver
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

#define SOH         0x01 
#define EOT         0x04
#define ACK         0x06 
#define NAK         0x15 
#define CAN         0x18

static struct termios termsave;
static struct termios termcur;
static int ttyfd = -1;
static uint_fast8_t xmodem_buffer[128];
static uint_fast8_t disp=0;
FILE *receive_fp;

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

static void term_raw(int fd)  
{
    memcpy(&termcur, &termsave, sizeof(struct termios));
    cfmakeraw(&termcur);                                                
    termcur.c_cc[VMIN] = 0;
    termcur.c_cc[VTIME] = 10;
    tcsetattr(fd, TCSAFLUSH, &termcur);                                 
}                                                                             

static void restore(int fd)
{
    tcsetattr(fd, TCSAFLUSH, &termsave);                                  
    if (fd >= 0) {
        tcsetattr(fd, TCSAFLUSH, &termsave);
        close(fd);
    }
}

static int xmodem_receive(void) {
    uint_fast8_t block_cnt;
    uint_fast8_t block_exp = 1;
    uint_fast8_t pos = 0;
    uint_fast8_t inp;
    uint_fast8_t outp;
    uint_fast8_t checksum;
    uint_fast8_t outt;

    outt='W';
    
    outp = NAK;
    while(1) {
        write(ttyfd, &outp, 1);
        read(ttyfd, &inp, 1);
        if(inp == EOT) {
            /* Transmission done */
            outp = ACK;
            write(ttyfd, &outp, 1);
            if(disp) fputc('\n', stderr); 
            return 0;
        }
        else if(inp == CAN) {
            /* Transmission cancelled */
            if(disp) fputc('\n', stderr);
            return -1;
        }
        else if(inp == SOH) {
            /* Got header, get package */
            outp = NAK;
            outt = 'T';
            checksum = 0;
            read(ttyfd, &inp, 1);
            block_cnt = inp;
            read(ttyfd, &inp, 1);
            if((block_cnt == (inp ^0xFF)) && (block_cnt == block_exp)) {
                /* Get block, otherwise retry */
                for(pos=0; pos<128; pos++) {
                    read(ttyfd, &inp, 1);
                    xmodem_buffer[pos]=inp;
                    checksum += inp;
                }
                /* Verify checksum */
                read(ttyfd, &inp, 1);
                if(checksum == inp) {
                    outp = ACK;
                    fwrite(&xmodem_buffer, 1, 128, receive_fp);
                    block_exp++;
                }
            }
        } else if(inp!=0 && !disp)  {
            /* Unexpected character - assume user input and abort */
            return -1;
        }
        /* Progress indicator if STDIN is not used */
        if(disp) fputc(outt,stderr);
    }

}

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
    fputs("rx - receive a file using X-modem\n", stderr);
    fputs("Usage: rx [-t tty] [-b baudrate] [-f] filename\n", stderr);
}


int main(int argc, char *argv[]) 
{
    const char *filename;
    const char *ext_tty_filename;
    int ret;
    int opt;
    int flags;
    int fd;
    uint_fast8_t ext_tty=0;
    uint_fast8_t overwrite=0;
    speed_t speedval = 0;

    while((opt = getopt(argc, argv, "t:b:f")) != -1) {
        switch(opt) {
            case 't':
                /* Use specified TTY instead of STDIN */
                ext_tty = 1;
                ext_tty_filename = optarg;
                break;
            case 'b':
                /* Parse baudrate */
                if(!parsespeed(optarg, &speedval)) {
                    fprintf(stderr, "Invalid baudrate: %s\n", optarg);
                    return 1;
                }
                break;
            case 'f':
                /* Force overwrite of existing file */
                overwrite = 1;
                break;
            default:
                break;
        }
    }

    if(optind < argc)
       filename = argv[optind];
    else {
        usage();
        return 1;
    }
        
    /* Setup TTY */
    if (!ext_tty) {
        ttyfd = STDIN_FILENO;
        disp = 0;
        if (!isatty(ttyfd)) {
            fprintf(stderr, "stdin is not a terminal\n");
            return 1;
        } 
    } else {
        /* open specified port */
        ttyfd = open(ext_tty_filename, O_RDWR | O_NOCTTY);
        if (ttyfd < 0) {
            perror(argv[2]);
            return 1;
        }
        disp = 1;
    } 

    /* Open file to receive */
    flags = O_WRONLY | O_CREAT;
    
    if(overwrite)
        flags |=O_TRUNC;
    else
        flags |=O_EXCL;

    fd = open(filename, flags, 0644);
    receive_fp = fdopen(fd, "wb");
    if(!receive_fp) {
        perror(filename);
        close(fd);
        return 1;
    }
    
    fputs("Waiting for sender to initiate X-modem transfer\n",stderr);
    if(ttyfd == STDIN_FILENO)
        fputs("Press any key to cancel\n",stderr);
    
    tcgetattr(ttyfd, &termsave);
    if(speedval > 0) {
        if(cfsetospeed(&termsave, (speed_t)speedval) < 0 ||
           tcsetattr(ttyfd, TCSAFLUSH, &termsave) < 0) {
            
            restore(ttyfd);
            perror("baudrate");
            exit(1);
        }
    }
    term_raw(ttyfd);

    ret = xmodem_receive();
    fclose(receive_fp);
    restore(ttyfd);
    if(ret<0) fputs("Transfer cancelled\n", stderr);
    else fputs("Transfer complete\n",stderr);
}

