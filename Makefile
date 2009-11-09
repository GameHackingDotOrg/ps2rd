# This is the main makefile of ps2rd.
#
# The build targets are:
#  check   - check for environment variables (invoked by all)
#  all     - compile project (default)
#  clean   - clean project
#  rebuild - rebuild project (clean + all)
#  release - create "release package"
#
# The following variables can influence the build process:

# Set DEBUG to 1 to enable the debug mode. In debug mode, a lot of helpful debug
# messages will be printed to host: when using ps2link.
DEBUG = 1

# Enable or disable netlog support (send log messages over UDP)
NETLOG = 0

# Set SMS_MODULES to 1 to build ps2rd with the network modules from SMS.
SMS_MODULES = 1

VARS=DEBUG=$(DEBUG) NETLOG=$(NETLOG) SMS_MODULES=$(SMS_MODULES)

.SILENT:

all: check
	$(VARS) $(MAKE) -C iop
	bin2o iop/debugger/debugger.irx ee/loader/debugger_irx.o _debugger_irx
	bin2o iop/dev9/ps2dev9.irx ee/loader/ps2dev9_irx.o _ps2dev9_irx
	bin2o iop/eesync/eesync.irx ee/loader/eesync_irx.o _eesync_irx
	bin2o iop/memdisk/memdisk.irx ee/loader/memdisk_irx.o _memdisk_irx
	bin2o $(PS2SDK)/iop/irx/usbd.irx ee/loader/usbd_irx.o _usbd_irx
	bin2o iop/usb_mass/usb_mass.irx ee/loader/usb_mass_irx.o _usb_mass_irx
	@if [ $(SMS_MODULES) = "1" ]; then \
		bin2o iop/SMSMAP/SMSMAP.irx ee/loader/ps2smap_irx.o _ps2smap_irx; \
		bin2o iop/SMSTCPIP/bin/SMSTCPIP.irx ee/loader/ps2ip_irx.o _ps2ip_irx; \
	else \
		bin2o iop/smap/ps2smap.irx ee/loader/ps2smap_irx.o _ps2smap_irx; \
		bin2o $(PS2SDK)/iop/irx/ps2ip.irx ee/loader/ps2ip_irx.o _ps2ip_irx; \
	fi
	$(VARS) $(MAKE) -C ee
	$(MAKE) -C pc

clean:
	$(VARS) $(MAKE) -C ee clean
	rm -f ee/loader/*_irx.o
	$(VARS) $(MAKE) -C iop clean
	$(MAKE) -C pc clean
	rm -rf release/

rebuild: clean all

release: all
	rm -rf release
	mkdir -p release/ps2 release/pc
	@if [ -x $(PS2DEV)/bin/ps2-packer ]; then \
		ps2-packer -v ee/loader/ps2rd.elf release/ps2/ps2rd.elf; \
		chmod +x release/ps2/ps2rd.elf; \
	else \
		cp ee/loader/ps2rd.elf release/ps2/ps2rd.elf; \
	fi
	cp ee/loader/ps2rd.conf release/ps2/
	cp ee/loader/cheats.txt release/ps2/
	cp pc/ntpbclient/bin/* release/pc/
	cp BUGS CHANGES COMMIT COPYING* CREDITS INSTALL README TODO release/
	cp -r doc/ release/

check:
	@if [ -z $(PS2SDK) ]; then \
		echo "PS2SDK env var not set." >&2; \
		exit 1; \
	fi
