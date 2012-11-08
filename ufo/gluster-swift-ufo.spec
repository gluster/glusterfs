############################################################################################################
# Command to build rpms.#
# $ rpmbuild -ta %{name}-%{version}-%{release}.tar.gz #
############################################################################################################
# Setting up the environment. #
#  * Create a directory %{name}-%{version} under $HOME/rpmbuild/SOURCES #
#  * Copy the contents of gluster directory into $HOME/rpmbuild/SOURCES/%{name}-%{version} #
#  * tar zcvf %{name}-%{version}-%{release}.tar.gz $HOME/rpmbuild/SOURCES/%{name}-%{version} %{name}.spec #
# For more information refer #
# http://fedoraproject.org/wiki/How_to_create_an_RPM_package #
############################################################################################################

%if ! (0%{?fedora} > 12 || 0%{?rhel} > 5)
%{!?python_sitelib: %global python_sitelib %(%{__python} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%endif

%define _confdir     /etc/swift
%define _ufo_version 1.1
%define _ufo_release 1

Summary  : GlusterFS Unified File and Object Storage.
Name     : gluster-swift-ufo
Version  : %{_ufo_version}
Release  : %{_ufo_release}
Group    : Application/File
Vendor   : Red Hat Inc.
Source0  : %{name}-%{version}-%{release}.tar.gz
Packager : gluster-users@gluster.org
License  : Apache
BuildArch: noarch
Requires : memcached
Requires : openssl
Requires : python
#Requires : openstack-swift >= 1.4.8
#Requires : openstack-swift-account >= 1.4.8
#Requires : openstack-swift-auth >= 1.4.8
#Requires : openstack-swift-container >= 1.4.8
#Requires : openstack-swift-object >= 1.4.8
#Requires : openstack-swift-proxy >= 1.4.8
#Obsoletes: gluster-swift
#Obsoletes: gluster-swift-plugin

%description
Gluster Unified File and Object Storage unifies NAS and object storage
technology. This provides a system for data storage that enables users to access
the same data as an object and as a file, simplifying management and controlling
storage costs.

%prep
%setup -q

%build
%{__python} setup.py build

%install
rm -rf %{buildroot}

%{__python} setup.py install -O1 --skip-build --root %{buildroot}

mkdir -p      %{buildroot}/%{_confdir}/
cp -r etc/*   %{buildroot}/%{_confdir}/

mkdir -p                             %{buildroot}/%{_bindir}/
cp bin/gluster-swift-gen-builders    %{buildroot}/%{_bindir}/

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{python_sitelib}/gluster
#%{python_sitelib}/gluster/swift/*.py*
#%{python_sitelib}/gluster/swift/common/*.py*
#%{python_sitelib}/gluster/swift/common/middleware
#%{python_sitelib}/gluster/swift/proxy
#%{python_sitelib}/gluster/swift/obj
#%{python_sitelib}/gluster/swift/container
#%{python_sitelib}/gluster/swift/account
%{python_sitelib}/gluster_swift_ufo-%{version}-*.egg-info
%{_bindir}/gluster-swift-gen-builders
%dir %{_sysconfdir}/swift
%config %{_confdir}/account-server/1.conf-gluster
%config %{_confdir}/container-server/1.conf-gluster
%config %{_confdir}/object-server/1.conf-gluster
%config %{_confdir}/swift.conf-gluster
%config %{_confdir}/proxy-server.conf-gluster
%config %{_confdir}/fs.conf-gluster
