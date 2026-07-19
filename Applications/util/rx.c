#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

#define SOH         0x01 
#define EOT         0x04
#define ACK         0x06 
#define NAK         0x15 
#define CAN         0x18

static struct termios termsave;
static struct termios termcur;
static int ttyfd = -1;
static uint8_t xmodem_buffer[128];
FILE *receive_fp;

static void term_raw(int fd)  
{
    tcgetattr(fd, &termsave);
    memcpy(&termcur, &termsave, sizeof(struct termios));
    cfmakeraw(&termcur);                                                
    termcur.c_cc[VMIN] = 1;
    termcur.c_cc[VTIME] = 0;
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
    uint8_t block_cnt;
    uint8_t block_exp = 1;
    uint8_t pos = 0;
    uint8_t inp;
    uint8_t outp;
    uint8_t checksum;
    
    outp = NAK;
    while(1) {
        write(ttyfd, &outp, 1);
        read(ttyfd, &inp, 1);
        if(inp == EOT) {
            // Transmission done
            outp = ACK;
            write(ttyfd, &outp, 1);
            return 0;
        }
        if(inp == CAN) {
            // Transmission cancelled
            return -1;
        }
        if(inp == SOH) {
            // Got header, get package
            outp = NAK;
            checksum = 0;
            read(ttyfd, &inp, 1);
            block_cnt = inp;
            read(ttyfd, &inp, 1);
            if((block_cnt == (inp ^0xFF)) && (block_cnt == block_exp)) {
                // Get block, otherwise retry
                for(pos=0; pos<128; pos++) {
                    read(ttyfd, &inp, 1);
                    xmodem_buffer[pos]=inp;
                    checksum += inp;
                }
                // Verify checksum
                read(ttyfd, &inp, 1);
                if(checksum == inp) {
                    outp = ACK;
                    fwrite(&xmodem_buffer, 1, 128, receive_fp);
                }
                block_exp++;
            }
        }        
    }

}

int main(int argc, char *argv[]) 
{
    const char *filename;
    int ret;
    if (argc == 2) {
        /* rx filename */
        ttyfd = STDIN_FILENO;
        filename = argv[1];

        if (!isatty(ttyfd)) {
            fprintf(stderr, "stdin is not a terminal\n");
            return 1;
        } 
    } else if (argc == 4 && strcmp(argv[1], "-t") == 0) {
        /* open specified port */
        ttyfd = open(argv[2], O_RDWR | O_NOCTTY);
        if (ttyfd < 0) {
            perror(argv[2]);
            return 1;
        }

        filename = argv[3];
    } else {
        fputs("rx - receive a file using X-modem\n", stderr);
        fputs("Usage: rx [-t tty] filename\n", stderr);
        return 1;
    }

    fputs("Waiting for sender to initiate X-modem transfer\n",stderr);
    receive_fp = fopen(filename, "w");
    term_raw(ttyfd);
    ret = xmodem_receive();   
    fclose(receive_fp);
    restore(ttyfd);
    if(ret<0) fputs("X-modem transmission cancelled\n", stderr);
    else fputs("X-modem transmission complete\n",stderr);
}

