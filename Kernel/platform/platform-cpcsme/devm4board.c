#include <kernel.h>
#include <kdata.h>
#include <rtc.h>
#include <printf.h>
#include <tinydisk.h>
#include "devm4board.h"

#ifdef CONFIG_M4BOARD

int m4_plt_rtc_read(void){
	uint16_t len = sizeof(struct cmos_rtc);
	struct cmos_rtc cmos;
	uint8_t *p = cmos.data.bytes;
	uint16_t year;
	if (!m4_present) {
		udata.u_error = EOPNOTSUPP;
		return -1;
	}

	if (udata.u_count < len)
		len = udata.u_count;
	m4_gettime(); /*stores rtc time string in m4_time_str with format: hh:mm:ss yyyy-mm-dd*/
	year = ((m4_time_str[9]-'0')*1000)+(m4_time_str[10]-'0')*100+((m4_time_str[11]-'0')*10)+(m4_time_str[12]-'0');
	*p++ = year & 0xff;
	*p++ = year >> 8;
	*p++ = (((m4_time_str[14]-'0')*10)+(m4_time_str[15]-'0'-1));/* month 0-11 */
	*p++ = ((m4_time_str[17]-'0')*10)+(m4_time_str[18]-'0');
	*p++ = ((m4_time_str[0]-'0')*10)+(m4_time_str[1]-'0');
	*p++ = ((m4_time_str[3]-'0')*10)+(m4_time_str[4]-'0');
	*p++ = ((m4_time_str[6]-'0')*10)+(m4_time_str[7]-'0');
	cmos.type = CMOS_RTC_DEC; /* YYYY MM DD HH MM SS Byte encoded . Year is 2 bytes little endian */
	if (uput(&cmos, udata.u_base, len) == -1)
		return -1;
	return len;
}

int m4_plt_rtc_write(void)
{
	udata.u_error = EOPNOTSUPP;
	return -1;
}

int m4_xfer(bool is_read, uint8_t *dptr){
	uint8_t res;
	block_data_ptr = dptr;
	/*kprintf("R/W:%u,p:%x,lba:%lx,raw:%u,page:%x-",is_read,dptr,lba,td_raw,td_page);*/
	if (is_read){
		/*kputs("M4 SD READ\n");*/
		res = m4_sd_read_block();
		switch (res){
		case 0:
			return 1;
		case 1:
			kputs("M4 SD R Error\n");
			return 0;
		case 3:
			kputs("M4 SD R Not Ready\n");
			return 0;
		case 4:
			kputs("M4 SD R Invalid Parameter\n");
			return 0;
		default:
			kprintf("M4 SD R ??:%u\n",res);
			return 0;
		}
	}
	else{
		/*kputs("M4 SD WRITE\n");*/
		res = m4_sd_write_block();
		switch (res){
		case 0:
			return 1;
		case 1:
			kputs("M4 SD W Error\n");
			return 0;
		case 2:
			kputs("M4 SD W Write Protected\n");
			return 0;
		case 3:
			kputs("M4 SD W Not Ready\n");
			return 0;
		case 4:
			kputs("M4 SD W Invalid Parameter\n");
			return 0;
		default:
			kprintf("M4 SD W ??:%u\n",res);
			return 0;
		}
	}
}
int m4_sd_xfer(uint_fast8_t dev, bool is_read, uint32_t lba, uint8_t *dptr){
	m4_is_img=0;
	if (is_read)
		read_lba = lba;
	else
		write_lba = lba;
	return m4_xfer(is_read, dptr);
}

int m4_img_xfer(uint_fast8_t dev, bool is_read, uint32_t lba, uint8_t *dptr){
	int err = 0;
	m4_is_img=1;
	m4_img_lba = (lba << 1);
	if (!m4_img_seek())
		err = m4_xfer(is_read, dptr);
	else
		kputs("M4 file seek error\n");
end:
	return err;
}

#endif