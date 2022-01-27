#----------------------------------------------------------------------
#
#	Description: MAKEFILE Template for uVision C51 Compiler Projects
#	Author:		 leekw@telematicsM.com
#	Date:		 2001/10/18
#
#	Copyright (c) 2001 Telematics Co., Ltd.  All rights reserved.
#
#----------------------------------------------------------------------

TARGET	= TCSD
OBJS	= $(TARGET).obj

#----------------------------------------------------------------------

CC		= C51
LD		= BL51
HC		= OH51

C_DIRS	= PL(69) PW(132) ROM(Compact) Small OT(6,Speed)
LD_DIRS = RS(128) PL(68) PW(78)

%.obj: %.C
	$(CC) $*.c	$(C_DIRS)

all: $(TARGET).HEX

$(TARGET).HEX:	$(OBJS)
	$(LD) $(OBJS: =,) TO $(TARGET).ABS $(LD_DIRS)
	$(HC) $(TARGET).ABS

clean:
	@if exist $(TARGET).ABS del $(TARGET).ABS
	@if exist $(TARGET).HEX del $(TARGET).HEX
	@if exist $(OBJS) del $(OBJS)
	@if exist $(OBJS:.obj=.err) del $(OBJS:.obj=.err)
	@if exist $(OBJS:.obj=.lst) del $(OBJS:.obj=.lst)
	@if exist $(OBJS:.obj=.m51) del $(OBJS:.obj=.m51)