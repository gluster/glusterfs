try:
    import ConfigParser
except ImportError:
    # py 3
    import configparser as ConfigParser
import re

import syncdutils

SECT_ORD = '__section_order__'
SECT_META = '__meta__'
config_version = 2.0

re_type = type(re.compile(''))

class GConffile(object):

    def __init__(self, path, peers):
        self.peers = peers
        self.path = path
        self.config = ConfigParser.RawConfigParser()
        self.config.read(path)

    def section(self, rx=False):
        peers = self.peers
        if not peers:
            peers = ['.', '.']
            rx = True
        if rx:
            st = 'peersrx'
        else:
            st = 'peers'
        return ' '.join([st] + [syncdutils.escape(u) for u in peers])

    @staticmethod
    def parse_section(section):
        sl = section.split()
        st = sl.pop(0)
        sl = [syncdutils.unescape(u) for u in sl]
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

        Needed for python 2.{4,5} where ConfigParser
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

    def update_to(self, dct):
        if not self.peers:
            raise RuntimeError('no peers given, cannot select matching options')
        def update_from_sect(sect):
            for k, v in self.config._sections[sect].items():
                if k == '__name__':
                    continue
                k = k.replace('-', '_')
                dct[k] = v
        for sect in self.ord_sections():
            sp = self.parse_section(sect)
            if isinstance(sp[0], re_type) and len(sp) == len(self.peers):
                match = True
                for i in range(len(sp)):
                    if not sp[i].search(self.peers[i]):
                        match = False
                        break
                if match:
                    update_from_sect(sect)
        if self.config.has_section(self.section()):
            update_from_sect(self.section())

    def get(self, opt=None):
        d = {}
        self.update_to(d)
        if opt:
            d = {opt: d.get(opt, "")}
        for k, v in d.iteritems():
            if k == '__name__':
                continue
            print("%s: %s" % (k, v))

    def write(self):
        if not self.config.has_section(SECT_META):
            self.config.add_section(SECT_META)
        self.config.set(SECT_META, 'version', config_version)
        f = None
        try:
            f = open(self.path, 'wb')
            self.config.write(f)
        finally:
            if f:
                f.close()

    def set(self, opt, val, rx=False):
        sect = self.section(rx)
        if not self.config.has_section(sect):
            self.config.add_section(sect)
            # regarding SECT_ORD, cf. ord_sections
            if not self.config.has_section(SECT_ORD):
                self.config.add_section(SECT_ORD)
            self.config.set(SECT_ORD, sect, len(self.config._sections[SECT_ORD]))
        self.config.set(sect, opt, val)
        self.write()

    def delete(self, opt, rx=False):
        sect = self.section(rx)
        if not self.config.has_section(sect):
            return
        if self.config.remove_option(sect, opt):
            self.write()
