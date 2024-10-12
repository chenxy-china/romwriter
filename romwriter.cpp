
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/time.h>

#define MISSING_FUNC_FMT	"Error: Adapter does not have %s capability\n"

int lookup_i2c_bus(const char *i2cbus_arg)
{
	unsigned long i2cbus;
	char *end;

	i2cbus = strtoul(i2cbus_arg, &end, 0);
	if (*end || !*i2cbus_arg) {
		return -1;
	}
	if (i2cbus > 0xFFFFF) {
		fprintf(stderr, "Error: I2C bus out of range!\n");
		return -2;
	}

	return i2cbus;
}

int open_i2c_dev(int i2cbus, char *filename, size_t size, int quiet)
{
	int file, len;

	len = snprintf(filename, size, "/dev/i2c/%d", i2cbus);
	if (len >= (int)size) {
		fprintf(stderr, "%s: path truncated\n", filename);
		return -EOVERFLOW;
	}
	file = open(filename, O_RDWR);

	if (file < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		len = snprintf(filename, size, "/dev/i2c-%d", i2cbus);
		if (len >= (int)size) {
			fprintf(stderr, "%s: path truncated\n", filename);
			return -EOVERFLOW;
		}
		file = open(filename, O_RDWR);
	}

	if (file < 0 && !quiet) {
		if (errno == ENOENT) {
			fprintf(stderr, "Error: Could not open file "
				"`/dev/i2c-%d' or `/dev/i2c/%d': %s\n",
				i2cbus, i2cbus, strerror(ENOENT));
		} else {
			fprintf(stderr, "Error: Could not open file "
				"`%s': %s\n", filename, strerror(errno));
			if (errno == EACCES)
				fprintf(stderr, "Run as root?\n");
		}
	}

	return file;
}


/*
 * Parse a CHIP-ADDRESS command line argument and return the corresponding
 * chip address, or a negative value if the address is invalid.
 */
int parse_i2c_address(const char *address_arg)
{
	long address;
	char *end;
	long min_addr = 0x08;
	long max_addr = 0x77;

	address = strtol(address_arg, &end, 0);
	if (*end || !*address_arg) {
		fprintf(stderr, "Error: Chip address is not a number!\n");
		return -1;
	}

	if (address < min_addr || address > max_addr) {
		fprintf(stderr, "Error: Chip address out of range "
			"(0x%02lx-0x%02lx)!\n", min_addr, max_addr);
		return -2;
	}

	return address;
}


static int check_funcs(int file)
{
	unsigned long funcs;

	/* check adapter functionality */
	if (ioctl(file, I2C_FUNCS, &funcs) < 0) {
		fprintf(stderr, "Error: Could not get the adapter "
			"functionality matrix: %s\n", strerror(errno));
		return -1;
	}

	if (!(funcs & I2C_FUNC_I2C)) {
		fprintf(stderr, MISSING_FUNC_FMT, "I2C transfers");
		return -1;
	}

	return 0;
}

#define PRINT_STDERR	(1 << 0)
#define PRINT_READ_BUF	(1 << 1)
#define PRINT_WRITE_BUF	(1 << 2)
#define PRINT_HEADER	(1 << 3)
static void print_msgs(struct i2c_msg *msgs, __u32 nmsgs, unsigned flags)
{
	FILE *output = flags & PRINT_STDERR ? stderr : stdout;
	unsigned i;
	__u16 j;

	for (i = 0; i < nmsgs; i++) {
		int read = msgs[i].flags & I2C_M_RD;
		int recv_len = msgs[i].flags & I2C_M_RECV_LEN;
		int print_buf = (read && (flags & PRINT_READ_BUF)) ||
				(!read && (flags & PRINT_WRITE_BUF));
		__u16 len = msgs[i].len;

		if (recv_len && print_buf && len != msgs[i].buf[0] + 1) {
			fprintf(stderr, "Correcting wrong msg length after recv_len! Please fix the I2C driver and/or report.\n");
			len = msgs[i].buf[0] + 1;
		}

		if (flags & PRINT_HEADER) {
			fprintf(output, "msg %u: addr 0x%02x, %s, len ",
				i, msgs[i].addr, read ? "read" : "write");
			if (!recv_len || flags & PRINT_READ_BUF)
				fprintf(output, "%u", len);
			else
				fprintf(output, "TBD");
		}

		if (len && print_buf) {
			if (flags & PRINT_HEADER)
				fprintf(output, ", buf ");
			for (j = 0; j < len - 1; j++)
				fprintf(output, "0x%02x ", msgs[i].buf[j]);
			/* Print final byte with newline */
			fprintf(output, "0x%02x\n", msgs[i].buf[j]);
		} else if (flags & PRINT_HEADER) {
			fprintf(output, "\n");
		}
	}
}

int i2c_dev_write(int file, struct i2c_msg *msgs, int nmsgs)
{
    int nmsgs_sent;
    struct i2c_rdwr_ioctl_data rdwr;

    rdwr.msgs = msgs;
    rdwr.nmsgs = nmsgs;
    nmsgs_sent = ioctl(file, I2C_RDWR, &rdwr);
    if (nmsgs_sent < 0) {
        fprintf(stderr, "Error: Sending messages failed: %s\n", strerror(errno));
        return -1;
    } else if (nmsgs_sent < nmsgs) {
        fprintf(stderr, "Warning: only %d/%d messages were sent\n", nmsgs_sent, nmsgs);
        return -1;
    }
    return nmsgs_sent;
}

int i2c_read(int file, int address)
{
    int nmsgs_sent = 0;
    struct i2c_msg msgs[2];

    //第1个msg，写入需要读取的地址
    msgs[0].addr = address; //chip addr
    msgs[0].flags = 0;  //write data
    msgs[0].len = 2;    //根据chip手册，地址 2 byte
    msgs[0].buf = (unsigned char*)malloc(msgs[0].len);
    if (!msgs[0].buf) {
        fprintf(stderr, "Error: No memory for buffer\n");
        return -1;
    }
    memset(msgs[0].buf, 0, msgs[0].len);

    //第2个msg，写入需要读取的长度
    msgs[1].addr = address; //chip addr
    msgs[1].flags = I2C_M_RD;  //read data
    msgs[1].len = 255;
    msgs[1].buf = (unsigned char*)malloc(msgs[1].len);
    if (!msgs[1].buf) {
        fprintf(stderr, "Error: No memory for buffer\n");
        free(msgs[0].buf);
        return -1;
    }
    memset(msgs[1].buf, 0, msgs[1].len);
    if (msgs[1].flags & I2C_M_RECV_LEN)
        msgs[1].buf[0] = 1;  /* number of extra bytes ,length will be first received byte */

    nmsgs_sent = i2c_dev_write(file,msgs,2);

    print_msgs(msgs, nmsgs_sent, PRINT_READ_BUF |  PRINT_HEADER | PRINT_WRITE_BUF);

    free(msgs[0].buf);
    free(msgs[1].buf);
    return nmsgs_sent;
}

int i2c_write_read_test(int file, int address) 
{
    int nmsgs_sent = 0;
    struct i2c_msg msgs[3];
    unsigned char testvalue = 0x19;

    //第1个msg，写入需要写入的地址+值
    msgs[0].addr = address; //chip addr
    msgs[0].flags = 0;  //write data
    msgs[0].len = 4;    //根据chip手册，地址 2 byte + 写入值 2byte
    msgs[0].buf = (unsigned char*)malloc(msgs[0].len);
    if (!msgs[0].buf) {
        fprintf(stderr, "Error: No memory for buffer\n");
        return -1;
    }
    memset(msgs[0].buf, 0, msgs[0].len);
    msgs[0].buf[0] = 0x64;
    msgs[0].buf[1] = 0x64;
    msgs[0].buf[2] = testvalue;
    msgs[0].buf[3] = 0xff;

    nmsgs_sent = i2c_dev_write(file,msgs,1);
    print_msgs(msgs, nmsgs_sent, PRINT_READ_BUF |  PRINT_HEADER | PRINT_WRITE_BUF);

    //写与读，不能在一个ioctl操作，同时两个ioctl需要间隔
    sleep(1);

    //第2个msg，写入需要读取的地址
    msgs[1].addr = address; //chip addr
    msgs[1].flags = 0;  //write data
    msgs[1].len = 2;    //根据chip手册，地址 2 byte
    msgs[1].buf = (unsigned char*)malloc(msgs[1].len);
    if (!msgs[1].buf) {
        fprintf(stderr, "Error: No memory for buffer\n");
        free(msgs[0].buf);
        return -1;
    }
    memset(msgs[1].buf, 0, msgs[1].len);
    msgs[1].buf[0] = 0x64;
    msgs[1].buf[1] = 0x63;

    //第3个msg，写入需要读取的长度
    msgs[2].addr = address; //chip addr
    msgs[2].flags = I2C_M_RD;  //read data
    msgs[2].len = 3;
    msgs[2].buf = (unsigned char*)malloc(msgs[2].len);
    if (!msgs[2].buf) {
        fprintf(stderr, "Error: No memory for buffer\n");
        free(msgs[0].buf);
        free(msgs[1].buf);
        return -1;
    }
    memset(msgs[2].buf, 0, msgs[2].len);
    if (msgs[2].flags & I2C_M_RECV_LEN)
        msgs[2].buf[0] = 1;  /* number of extra bytes ,length will be first received byte */

    nmsgs_sent = i2c_dev_write(file,msgs + 1,2);

    print_msgs(msgs + 1, nmsgs_sent, PRINT_READ_BUF |  PRINT_HEADER | PRINT_WRITE_BUF);

    if(msgs[2].buf[1] != testvalue){
        printf("i2c_write_read_test failed~~~, value 0x%02x\n",msgs[2].buf[1]);
    }else{
        printf("i2c_write_read_test sucessed!!!\n");
    }

    free(msgs[0].buf);
    free(msgs[1].buf);
    free(msgs[2].buf);
    return 0;
}

int i2c_write(int file, int address, unsigned char* data, ssize_t bytes)
{

    int nmsgs_sent = 0;
    struct i2c_msg msgs[I2C_RDRW_IOCTL_MAX_MSGS];
    unsigned long len = 0;
    unsigned int i,j, step = 32;

    for (len = 0;len < 65535 ; ) {
        for(i = 0; i < I2C_RDRW_IOCTL_MAX_MSGS; i++){
            //写入需要写入的地址+值
            msgs[i].addr = address;     //chip addr
            msgs[i].flags = 0;          //write data
            msgs[i].len = 2 + step;     //根据chip手册，地址 2 byte + data 32byte
            msgs[i].buf = (unsigned char*)malloc(msgs[0].len);
            if (!msgs[0].buf) {
                fprintf(stderr, "Error: No memory for buffer\n");
                return -1;
            }

            memset(msgs[0].buf, 0, msgs[i].len);
            msgs[i].buf[0] = (len >> 8) & 0xff;
            msgs[i].buf[1] = len & 0xff;
            // printf("=== %d 0x%02x 0x%02x \n", i, msgs[i].buf[0], msgs[i].buf[1]);

            len+=step;
            if(len > 65535){
                i++;
                break;
            }
        }

        nmsgs_sent = i2c_dev_write(file,msgs,i);
        //print_msgs(msgs, nmsgs_sent, PRINT_READ_BUF |  PRINT_HEADER | PRINT_WRITE_BUF);
        for(j=0;j<i;j++){
            free(msgs[j].buf);  
        }
        usleep(10000);
    }

    
    for (len = 0;len < bytes ;) {
        for(i = 0; i < I2C_RDRW_IOCTL_MAX_MSGS; i++){
            //写入需要写入的地址+值
            msgs[i].addr = address;     //chip addr
            msgs[i].flags = 0;          //write data
            msgs[i].len = 2 + step;     //根据chip手册，地址 2 byte + data 32byte
            msgs[i].buf = (unsigned char*)malloc( msgs[i].len);
            if (! msgs[i].buf) {
                fprintf(stderr, "Error: No memory for buffer\n");
                return -1;
            }

            memset( msgs[i].buf, 0,  msgs[i].len);
            msgs[i].buf[0] = (len >> 8) & 0xff;
            msgs[i].buf[1] = len & 0xff;
            // printf("0x%02x 0x%02x \t",  msgs[i].buf[0],  msgs[i].buf[1]);

            //拷贝rom数据
            if(len + step < bytes){
                memcpy( msgs[i].buf + 2,data+len, step);
            }else{
                memcpy( msgs[i].buf + 2,data+len, bytes-len);
            }

            len+=step;
            if(len > bytes){
                i++;
                break;
            }
        }
        
        nmsgs_sent = i2c_dev_write(file,msgs,i);
        print_msgs(msgs, nmsgs_sent, PRINT_READ_BUF |  PRINT_HEADER | PRINT_WRITE_BUF);
        for(j=0;j<i;j++){
            free(msgs[j].buf);  
        }
        usleep(10000);
    }

    return 0;
}

int main(int argc,char** argv)
{
    printf("program start \n");

    struct timeval start,end;
    long dif_sec, dif_usec;
    gettimeofday(&start,NULL);

	char filename[20],romfilename[20];
	int i2cbus,  address = -1, file, romfile;
    unsigned char buffer[65535];
    memset(buffer, 0, sizeof(buffer));

    //查找i2c-n
    //printf("i2c %s \n",argv[1]);
	i2cbus = lookup_i2c_bus(argv[1]);
	if (i2cbus < 0){
		exit(1);
    }

    //打开i2c-n
	file = open_i2c_dev(i2cbus, filename, sizeof(filename), 0);
	if (file < 0 || check_funcs(file)){
		exit(1);
    }

    //获取chip的i2c地址
    //printf("chip_i2c_addr %s \n",argv[2]);
    address = parse_i2c_address(argv[2]);
    if (address < 0){
        goto err_out;
    }

    //读取rom文件名参数
    //printf("rom_filename %s \n",argv[3]);
    if(!argv[3]){
        goto err_out;
    }
    strcpy(romfilename,argv[3]);
    //rom文件存在
    if (access(romfilename,R_OK) < 0 ){
        printf("romfile %s not exist\n",romfilename);
        goto err_out;
    }

    // i2c_read(file, address);

    // i2c_write_read_test(file, address);

    //打开romfile
    ssize_t bytes_read;
    if((romfile = open (romfilename, O_RDONLY)) < 0 ){
        printf("open rom file  %s error\n",romfilename);
        goto err_out;
    }
    //读取到缓冲区
    bytes_read = read(romfile, buffer, sizeof(buffer));
    //写缓冲区内容到i2c
    i2c_write(file, address, buffer, bytes_read);
    close(romfile);

 err_out:
    close(file);

    gettimeofday(&end,NULL);
    dif_sec = end.tv_sec - start.tv_sec;
    dif_usec = end.tv_usec - start.tv_usec;
    
    if(dif_usec < 0 ){
        dif_sec = dif_sec -1;
        dif_usec = dif_usec + 1000000;
    }
    printf("running time is %ldsec (%ld us)\n", dif_sec, dif_usec);

    exit(1);
}