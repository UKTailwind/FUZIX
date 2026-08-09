/*
 *	Nothing to declare.
 *
 *	Kernel/include/i2c.h includes this so a platform can put whatever
 *	its controller needs in front of the shared driver - on the Z80
 *	boards that means the PCF8584's I/O ports.  Here the controller is
 *	inside the RP2350 and reached through the SDK, so i2cuser.c
 *	includes hardware/i2c.h itself and there is nothing for this file
 *	to say.  It exists because the include does.
 */
#ifndef PC3_PLATFORM_I2C_H
#define PC3_PLATFORM_I2C_H
#endif
