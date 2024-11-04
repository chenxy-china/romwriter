使用方法
Usage: romw I2CBUS ADDRESS [romfilename] [-c] [-v [verify-size]]
 I2CBUS is an integer or an I2C bus name
 ADDRESS is an integer (0x08 - 0x77, or 0x00 - 0x7f if -a is given)
  romfilename is file which your want to write to eeprom 
  if romfilename not set, just test i2c read and write
 -c is to clear eeprom data 
 -v is read eeprom data and write to vrom.bin file for verify
 verify-size is size which you want read from eeprom
 default size is 8192 bytes, if you set romfilename
 default size is romfile size
 Example (i2c-3, address 0x57 read and write test
 # romw 3 0x57 
  Example (same EEPROM, write rom file rom.bin to with verify
 # romw 3 0x57 rom.bin -v 
  Example (same EEPROM, read rom data with size 4k to file vrom.bin
# romw 3 0x57 -v 4096 
