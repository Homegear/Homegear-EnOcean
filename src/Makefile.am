AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear
LIBS += -Wl,-Bdynamic

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_enocean.la
mod_enocean_la_SOURCES = MyFamily.cpp MyFamily.h MyPacket.cpp MyPacket.h MyPeer.cpp MyPeer.h Factory.cpp Factory.h GD.cpp GD.h MyCentral.cpp MyCentral.h Interfaces.h Interfaces.cpp Security.h Security.cpp PhysicalInterfaces/IEnOceanInterface.h PhysicalInterfaces/IEnOceanInterface.cpp PhysicalInterfaces/Usb300.h PhysicalInterfaces/Usb300.cpp
mod_enocean_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_enocean.la
