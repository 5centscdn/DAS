# das.spec.  Generated from das.spec.in by configure.
#
# Spec file for GNOME Inform 7
#

Name: gnome-inform7
Version: 1.0
Release: @RPM_RELEASE@

URL: http://inform7.com/
License: GPLv3

Group: Development/Languages
Source: gnome-inform7-1.0.tar.gz
# Packager: P. F. Chimento <philip.chimento@gmail.com>

# Build requirements:
# For building manuals
BuildRequires: texlive
BuildRequires: graphviz
# Extra build tools
BuildRequires: intltool
BuildRequires: pkgconfig
BuildRequires: xz-lzma-compat
# Library devel packages:
BuildRequires: libuuid-devel
BuildRequires: glib2-devel
BuildRequires: gtk2-devel
BuildRequires: gtksourceview2-devel
BuildRequires: gtkspell-devel
BuildRequires: webkitgtk-devel
BuildRequires: goocanvas-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires: GConf2
Requires(pre): GConf2
Requires(post): GConf2
Requires(preun): GConf2

Summary: An IDE for the Inform 7 interactive fiction programming language

%description

%prep
%setup -q

%build
%configure --enable-manuals
make

%pre
%gconf_schema_prepare gnome-inform7

%install
rm -rf %{buildroot}
export GCONF_DISABLE_MAKEFILE_SCHEMA_INSTALL=1
make install DESTDIR=%{buildroot}
unset GCONF_DISABLE_MAKEFILE_SCHEMA_INSTALL
# Clean out files that should not be part of the rpm.
%{__rm} -f %{buildroot}%{_libdir}/%{name}/*.la

%post
%gconf_schema_upgrade gnome-inform7
touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :

%preun
%gconf_schema_remove gnome-inform7

%postun
if [ $1 -eq 0 ] ; then
    touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%posttrans
gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :

%clean
rm -rf %{buildroot}

%files
%defattr(-, root, root)
%define pkgdatadir %{_datadir}/%{name}
%define pkgdocdir %{_datadir}/doc/%{name}
%define pkglibdir %{_libdir}/%{name}
%define pkglibexecdir %{_libexecdir}/%{name}
%docdir %{pkgdocdir}
%docdir %{pkgdatadir}/Documentation
%{_datadir}/applications/%{name}.desktop
%{_sysconfdir}/gconf/schemas/%{name}.schemas
%{pkgdocdir}/TODO
%{pkgdatadir}/uninstall_manifest.txt
%{_datadir}/icons/hicolor/*/actions/inform7-builtin.png
%{pkglibexecdir}/cBlorb
%{pkgdocdir}/cBlorb/Complete.pdf
%{pkglibdir}/frotz.so

%changelog
* Sun Feb 12 2012 P. F. Chimento <philip.chimento@gmail.com>
- Changed 'lzma' requirement to 'xz-lzma-compat'.
das (1.0-1) unstable; urgency=low

  * Initial release 

 -- m5l2764k <m5l2764k@gmail.com>>  Mon, 07 Jan 2013 11:34:05 +0200
