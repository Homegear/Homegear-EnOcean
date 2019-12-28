AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear
LIBS += -Wl,-Bdynamic

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_enocean.la
mod_enocean_la_SOURCES = EnOcean.cpp EnOceanPacket.cpp EnOceanPeer.cpp Factory.cpp GD.cpp EnOceanCentral.cpp Interfaces.cpp Security.cpp PhysicalInterfaces/Hgdc.cpp PhysicalInterfaces/HomegearGateway.cpp PhysicalInterfaces/IEnOceanInterface.cpp PhysicalInterfaces/Usb300.cpp
mod_enocean_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_enocean.la
