
extern int hd_read(uint_fast8_t minor, uint_fast8_t rawflag, uint_fast8_t flag);
extern int hd_write(uint_fast8_t minor, uint_fast8_t rawflag, uint_fast8_t flag);
extern int hd_open(uint_fast8_t minor, uint16_t flag);
extern int hd_close(uint_fast8_t minor);
extern void fdhd_init(void);

extern void found_swap(uint16_t dev, uint16_t blocks);


