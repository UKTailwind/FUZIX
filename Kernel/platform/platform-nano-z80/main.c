#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <timer.h>
#include <devtty.h>

extern unsigned char irqvector;
uint16_t swap_dev = 0xFFFF;

uint16_t ramtop = PROGTOP;   

uint8_t plt_tick_present;

uint16_t plt_rtc_secs(void) {
    return 0;
}

/* This points to the last buffer in the disk buffers. There must be at least four 
 * * buffers to avoid deadlocks. */                                        
struct blkbuf *bufpool_end = bufpool + NBUFS;                                 
                                                                                
/*                                                                            
*  We pack discard into the memory image is if it were just normal           
*  code but place it at the end after the buffers. When we finish up         
*  booting we turn everything from the buffer pool to common into            
*  buffers. This blows away the _DISCARD segment.                            
*/                                                                           
void plt_discard(void)                                                        
{                                                                             
    uint16_t discard_size = (uint16_t)&udata - (uint16_t)bufpool_end;         
    bufptr bp = bufpool_end;                                                  
                                                                                
    discard_size /= sizeof(struct blkbuf);                                    
                                                                                    
    kprintf("%d buffers added\n", discard_size);                              
                                                                                
    bufpool_end += discard_size;                                              
                                                                                
    memset( bp, 0, discard_size * sizeof(struct blkbuf) );                    
                                                                                
    for( bp = bufpool + NBUFS; bp < bufpool_end; ++bp ){                      
        bp->bf_dev = NO_DEVICE;                                               
        bp->bf_busy = BF_FREE;                                                
    }                                                                            
}  

void plt_idle(void)
{
    timer_interrupt();
}

void plt_interrupt(void)
{
	//tty_drain_sio();
	tty_poll();
    timer_interrupt();
}
