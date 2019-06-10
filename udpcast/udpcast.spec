%define _binary_payload w9.gzdio
%global _binary_filedigest_algorithm 1
%global _source_filedigest_algorithm 1
Name:           udpcast
Summary:        UDP broadcast file distribution and installation
Version:        20120424
Release:        1
License:        GPLv2+ and BSD
Group:          Applications/System
URL:            http://udpcast.linux.lu/
Source:         http://udpcast.linux.lu/download/%{name}-%{version}.tar.gz
Buildroot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  m4
BuildRequires:  perl


%description
Command-line client for UDP sender and receiver.  Udpcast is an
application for multicasting data to multiple targets.


%prep
%setup -q

# Don't pass -s (strip) option to ld
sed -i -e '/override LDFLAGS +=-s/d' Makefile.in

%configure \
  --prefix=%{buildroot}%{_prefix} \
  --mandir=%{buildroot}%{_mandir} \
  --libdir=%{buildroot}%{_libdir} \


%build
make


%clean
echo rm -rf $RPM_BUILD_ROOT


%install
make install


%files
%defattr(-,root,root)
%{_sbindir}/udp-sender
%{_sbindir}/udp-receiver
%{_mandir}/man1/udp-sender.1.gz
%{_mandir}/man1/udp-receiver.1.gz
%{_includedir}/udpcast/rateGovernor.h
%doc Changelog.txt cmd.html COPYING


%changelog
* Tue Apr 24 2012 Alain Knaff <alain@knaff.lu>
- In receiver, return correct status if pipe fails
- By default, use nosync, even on device files
* Sun Jul 10 2011 Alain Knaff <alain@knaff.lu>
- fixed some -Wextra compilation warnings
- new --no-progress flag to suppress progress display
- print most debug messages to log, if available
- properly handle Control-C signal
- --receive-timeout parameter for receiver timeout during transmission
- "real" daemon mode
* Sat Jan 30 2010 Alain Knaff <alain@knaff.lu>
- In busybox, call the executables udp-receiver and udp-sender, the
same as outside of busybox
* Wed Dec 30 2009 Alain Knaff <alain@knaff.lu>
- new "--ignore-lost-data" mode
- Lift limit in number of supported network interfaces
* Sat Oct 31 2009 Alain Knaff <alain@knaff.lu>
- Support for start-timeout also on sender
* Sun Sep 20 2009 Alain Knaff <alain@knaff.lu>
- Fixed slice management bug introduced with streaming
* Sat Sep 12 2009 Alain Knaff <alain@knaff.lu>
- Restored block alignment enforcement (needed when reading data from a pipe)
- Fixed division-by-zero error on zero-sized transmissions
- only make data blocks as big as needed
* Tue Sep 01 2009 Alain Knaff <alain@knaff.lu>
- "Streaming" mode
- On receiver, make failure to send hello packet not fatal
- More efficient transmission on small files
- Allow pointopoint mode to be "used" (ignored) together with async,
  as long as a dataMcastAddress is supplied
* Sat Dec 13 2008 Alain Knaff <alain@knaff.lu>
- Fixed compilation on pre-historic compilers
* Sun Nov 30 2008 Alain Knaff <alain@knaff.lu>
- Fix a couple of compiler warnings
* Sun Nov 16 2008 Alain Knaff <alain@knaff.lu>
- Configurable statistics printout period
- Do not print uncompressed offset is pipe is in use, or offset not seekable
* Thu Sep 11 2008 Alain Knaff <alain@knaff.lu>
- Added distclean target to make Debian build easier
- Adapted to new name of mingw compiler, and other Mingw adaptations
- Removed obsolete m486 flag
- Fixed parameter types for getsockopt
- If there are no participants after autostart delay, do not
 transmit but exit right away

* Fri May  2 2008 Richard W.M. Jones <rjones@redhat.com> - 20071228-3
- Remove '-s' flag from Makefile.
- Remove unused udpcast_version macro.
- Use configure macro.
- Fix the license, GPLv2+ and BSD.
- BuildRequires perl.

* Mon Apr 21 2008 Richard W.M. Jones <rjones@redhat.com> - 20071228-2
- BR m4.

* Mon Apr 21 2008 Richard W.M. Jones <rjones@redhat.com> - 20071228-1
- Initial packaging for Fedora.

* Fri Jun  1 2007 Alain Knaff <alain@knaff.lu>
- Patch to fix parallel make & make DESTDIR=/tmp/xxx install
- Address gcc4 warnings
- Remove some #define fn udpc_fn lines

* Thu May 30 2007 Jan Oelschlaegel <joe77@web.de>
- Adapt to Solaris 10 x86 (added includes and configure checks)
- Tested on Linux and Solaris 10 (maybe some other OS are broken now...)

* Fri Mar 23 2007 Alain Knaff <alain@knaff.lu>
- Fixed typoes in socklib.c

* Tue Mar 6 2007 Alain Knaff <alain@knaff.lu>
- Fix issue with pipes and no destination file on receiver

* Sun Feb 18 2007 Alain Knaff <alain@knaff.lu>
- Documentation fix

* Mon Feb 5 2007 Alain Knaff <alain@knaff.lu>
- Adapt to busybox 1.4.1 (Config.in)

* Wed Jan 31 2007 Alain Knaff <alain@knaff.lu>
- Added #include <linux/types.h> to make it compile under (K)ubuntu
- Fix uninitialized variable in udp-receiver

* Mon Jan 29 2007 Alain Knaff <alain@knaff.lu>
- Adapt to busybox 1.3.2

* Wed Dec 20 2006 Alain Knaff <alain@knaff.lu>
- Adapt to new busybox 1.3.0

* Sat Dec 16 2006 Alain Knaff <alain@knaff.lu>
- Added startTimeout flag: abort if sender does not start within specified
time
- Darwin build fixes patch
- Refactoring to postpone file creation until sender is located

* Fri Oct 20 2006 Alain Knaff <alain@knaff.lu>
- Fix usage message to use full names for --mcast-data-address and
mcast-rdv-address

* Thu Sep 21 2006 Alain Knaff <alain@knaff.lu>
- Include uio.h into socklib.h, needed with older include files for iovec
- Avoid variable name "log", apparently, for older compilers, this shadows the
name of a built-in

* Wed Sep 20 2006 Alain Knaff <alain@knaff.lu>
- Added missing format string to printMyIp

* Sun Sep 17 2006 Alain Knaff <alain@knaff.lu>
- If --rexmit-hello-interval set on sender, still only display prompt
once on receiver
- Improved logging (on sender, offer option to periodically log
instantaneous bandwidth, log retransmission, and added datestamp to
all log)
- Enable autoconf (configure) in order to make it easier to compile it
on other Unices
- Reorganized cmd.html file to make it cleaner HTML (all the man stuff
now in separate files)
- Fix a buffer overrun on Windows

* Sat Mar 25 2006 Alain Knaff <alain@knaff.lu>
- Separate commandline spec file and mkimage spec file
