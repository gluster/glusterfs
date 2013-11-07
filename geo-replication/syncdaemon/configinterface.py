try:
    import ConfigParser
except ImportError:
    # py 3
    import configparser as ConfigParser
import re
from string import Template
import os
import errno
import sys
from stat import ST_DEV, ST_INO, ST_MTIME

from syncdutils import escape, unescape, norm, update_file, GsyncdError

SECT_ORD = '__section_order__'
SECT_META = '__meta__'
config_version = 2.0

re_type = type(re.compile(''))


class MultiDict(object):
    """a virtual dict-like class which functions as the union of underlying dicts"""

    def __init__(self, *dd):
        self.dicts = dd

    def __getitem__(self, key):
        val = None
        for d in self.dicts:
            if d.get(key) != None:
                val = d[key]
        if val == None:
            raise KeyError(key)
        return val


class GConffile(object):
    """A high-level interface to ConfigParser which flattens the two-tiered
       config layout by implenting automatic section dispatch based on initial
       parameters.

    Also ensure section ordering in terms of their time of addition -- a compat
    hack for Python < 2.7.
    """

    def _normconfig(self):
        """normalize config keys by s/-/_/g"""
        for n, s in self.config._sections.items():
            if n.find('__') == 0:
                continue
            s2 = type(s)()
            for k, v in s.items():
                if k.find('__') != 0:
                    k = norm(k)
                s2[k] = v
            self.config._sections[n] = s2

    def __init__(self, path, peers, *dd):
        """
        - .path: location of config file
        - .config: underlying ConfigParser instance
        - .peers: on behalf of whom we flatten .config
          (master, or master-slave url pair)
        - .auxdicts: template subtituents
        """
        self.peers = peers
        self.path = path
        self.auxdicts = dd
        self.config = ConfigParser.RawConfigParser()
        self.config.read(path)
        self.dev, self.ino, self.mtime = -1, -1, -1
        self._normconfig()

    def _load(self):
        try:
            sres = os.stat(self.path)
            self.dev = sres[ST_DEV]
            self.ino = sres[ST_INO]
            self.mtime = sres[ST_MTIME]
        except (OSError, IOError):
            if sys.exc_info()[1].errno == errno.ENOENT:
                sres = None

        self.config = ConfigParser.RawConfigParser()
        self.config.read(self.path)
        self._normconfig()

    def get_realtime(self, opt):
        try:
            sres = os.stat(self.path)
        except (OSError, IOError):
            if sys.exc_info()[1].errno == errno.ENOENT:
                sres = None
            else:
                raise

        # compare file system stat with that of our stream file handle
        if not sres or sres[ST_DEV] != self.dev or \
           sres[ST_INO] != self.ino or self.mtime != sres[ST_MTIME]:
            self._load()

        return self.get(opt, printValue=False)

    def section(self, rx=False):
        """get the section name of the section representing .peers in .config"""
        peers = self.peers
        if not peers:
            peers = ['.', '.']
            rx = True
        if rx:
            st = 'peersrx'
        else:
            st = 'peers'
        return ' '.join([st] + [escape(u) for u in peers])

    @staticmethod
    def parse_section(section):
        """retrieve peers sequence encoded by section name
           (as urls or regexen, depending on section type)
        """
        sl = section.split()
        st = sl.pop(0)
        sl = [unescape(u) for u in sl]
        if st == 'peersrx':
            sl = [re.compile(u) for u in sl]
        return sl

    def ord_sections(self):
        """Return an ordered list of sections.

        Ordering happens based on the auxiliary
        SECT_ORD section storing indices for each
        section added through the config API.

        To not to go corrupt in case of manually
        written config files, we take care to append
        also those sections which are not registered
        in SECT_ORD.

        Needed for python 2.{4,5,6} where ConfigParser
        cannot yet order sections/options internally.
        """
        so = {}
        if self.config.has_section(SECT_ORD):
            so = self.config._sections[SECT_ORD]
        so2 = {}
        for k, v in so.items():
            if k != '__name__':
                so2[k] = int(v)
        tv = 0
        if so2:
            tv = max(so2.values()) + 1
        ss = [s for s in self.config.sections() if s.find('__') != 0]
        for s in ss:
            if s in so.keys():
                continue
            so2[s] = tv
            tv += 1
        def scmp(x, y):
            return cmp(*(so2[s] for s in (x, y)))
        ss.sort(scmp)
        return ss

    def update_to(self, dct, allow_unresolved=False):
        """update @dct from key/values of ours.

        key/values are collected from .config by filtering the regexp sections
        according to match, and from .section. The values are treated as templates,
        which are substituted from .auxdicts and (in case of regexp sections)
        match groups.
        """
        if not self.peers:
            raise GsyncdError('no peers given, cannot select matching options')
        def update_from_sect(sect, mud):
            for k, v in self.config._sections[sect].items():
                if k == '__name__':
                    continue
                if allow_unresolved:
                    dct[k] = Template(v).safe_substitute(mud)
                else:
                    dct[k] = Template(v).substitute(mud)
        for sect in self.ord_sections():
            sp = self.parse_section(sect)
            if isinstance(sp[0], re_type) and len(sp) == len(self.peers):
                match = True
                mad = {}
                for i in range(len(sp)):
                    m = sp[i].search(self.peers[i])
                    if not m:
                        match = False
                        break
                    for j in range(len(m.groups())):
                        mad['match%d_%d' % (i+1, j+1)] = m.groups()[j]
                if match:
                    update_from_sect(sect, MultiDict(dct, mad, *self.auxdicts))
        if self.config.has_section(self.section()):
            update_from_sect(self.section(), MultiDict(dct, *self.auxdicts))

    def get(self, opt=None, printValue=True):
        """print the matching key/value pairs from .config,
           or if @opt given, the value for @opt (according to the
           logic described in .update_to)
        """
        d = {}
        self.update_to(d, allow_unresolved = True)
        if opt:
            opt = norm(opt)
            v = d.get(opt)
            if v:
                if printValue:
                    print(v)
                else:
                    return v
        else:
            for k, v in d.iteritems():
                if k == '__name__':
                    continue
                print("%s: %s" % (k, v))

    def write(self, trfn, opt, *a, **kw):
        """update on-disk config transactionally

        @trfn is the transaction function
        """
        def mergeconf(f):
            self.config = ConfigParser.RawConfigParser()
            self.config.readfp(f)
            self._normconfig()
            if not self.config.has_section(SECT_META):
                self.config.add_section(SECT_META)
            self.config.set(SECT_META, 'version', config_version)
            return trfn(norm(opt), *a, **kw)
        def updateconf(f):
            self.config.write(f)
        update_file(self.path, updateconf, mergeconf)

    def _set(self, opt, val, rx=False):
        """set @opt to @val in .section"""
        sect = self.section(rx)
        if not self.config.has_section(sect):
            self.config.add_section(sect)
            # regarding SECT_ORD, cf. ord_sections
            if not self.config.has_section(SECT_ORD):
                self.config.add_section(SECT_ORD)
            self.config.set(SECT_ORD, sect, len(self.config._sections[SECT_ORD]))
        self.config.set(sect, opt, val)
        return True

    def set(self, opt, *a, **kw):
        """perform ._set transactionally"""
        self.write(self._set, opt, *a, **kw)

    def _delete(self, opt, rx=False):
        """delete @opt from .section"""
        sect = self.section(rx)
        if self.config.has_section(sect):
            return self.config.remove_option(sect, opt)

    def delete(self, opt, *a, **kw):
        """perform ._delete transactionally"""
        self.write(self._delete, opt, *a, **kw)
