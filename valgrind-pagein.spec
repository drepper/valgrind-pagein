Name:		valgrind-pagein
Version:	1.2
Release:	1%{?dist}
Summary:	Valgrind tool to determine page-in order

Group:		Development/Tools
License:	GPLv2
URL:		https://github.com/drepper/valgrind-pagein
Source0:	%{name}-%{version}.tar.bz2

BuildRequires:	valgrind-devel
BuildRequires:	valgrind-tools-devel
BuildRequires:	pkgconfig


# Disable build root strip policy
%define __spec_install_post /usr/lib/rpm/brp-compress || :

# Disable -debuginfo package generation
%define debug_package   %{nil}

%description
valgrind-pagein provides a valgrind tool to determine the order in which
code and data pages are paged into the traced program.

%prep
%setup -q


%build
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc
%{_libdir}/valgrind/pagein-*


%changelog
* Fri Feb 1 2019  Ulrich Drepper <drepper@redhat.com> - 1.2
- Patch by Will Cohen <wcohen@redhat.com> to support recent
  versions of valgrind
* Thu May 31 2012 Ulrich Drepper <drepper@gmail.com> -
- Initial build.
