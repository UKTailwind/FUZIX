#define	FA_OPEN_EXISTING	0x00
#define	FA_READ			    0x01
#define	FA_WRITE			0x02
#define	FA_CREATE_NEW		0x04
#define	FA_CREATE_ALWAYS	0x08
#define	FA_OPEN_ALWAYS		0x10
#define FA_REALMODE		    0x80

extern void m4_gettime(void);
extern uint8_t m4_sd_read_block(void);
extern uint8_t m4_sd_write_block(void);
extern uint8_t m4_img_seek(void);
extern uint8_t m4_img_open(void);
extern uint8_t m4_img_close(void);
extern unsigned char m4_time_str[20];
extern uint8_t m4_present;
extern uint32_t read_lba;
extern uint32_t write_lba;
extern uint8_t * block_data_ptr;
extern uint8_t m4_open_mode;
extern uint8_t m4_img_fd;
extern uint32_t m4_img_lba;
extern uint8_t m4_img_write_fd;
extern uint8_t m4_img_read_fd;
extern uint8_t m4_is_img;
extern uint8_t m4_img_close_fd;

int m4_plt_rtc_read(void);
int m4_plt_rtc_write(void);
int m4_sd_xfer(uint_fast8_t dev, bool is_read, uint32_t lba, uint8_t *dptr);
int m4_img_xfer(uint_fast8_t dev, bool is_read, uint32_t lba, uint8_t *dptr);

