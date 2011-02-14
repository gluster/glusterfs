try:
    import ConfigParser
except ImportError:
    # py 3
    import configparser as ConfigParser


DEF_SECT = 'global'

class GConffile(object):

    def __init__(self, path, peers):
        if peers:
            self.section = 'peers ' + ' '.join(peers)
        else:
            self.section = DEF_SECT
        self.path = path
        self.config = ConfigParser.RawConfigParser()
        self.config.read(path)

    def update_to(self, dct):
        for sect in set([DEF_SECT, self.section]):
            if self.config.has_section(sect):
                for k, v in self.config._sections[sect].items():
                    if k == '__name__':
                        continue
                    k = k.replace('-', '_')
                    dct[k] = v

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
        f = None
        try:
            f = open(self.path, 'wb')
            self.config.write(f)
        finally:
            if f:
                f.close()

    def set(self, opt, val):
        if not self.config.has_section(self.section):
            self.config.add_section(self.section)
        self.config.set(self.section, opt, val)
        self.write()

    def delete(self, opt):
        if not self.config.has_section(self.section):
            return
        if self.config.remove_option(self.section, opt):
            self.write()
