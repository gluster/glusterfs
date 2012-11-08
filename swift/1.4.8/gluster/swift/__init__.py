""" Gluster Swift UFO """

class Version(object):
    def __init__(self, canonical_version, final):
        self.canonical_version = canonical_version
        self.final = final

    @property
    def pretty_version(self):
        if self.final:
            return self.canonical_version
        else:
            return '%s-dev' % (self.canonical_version,)


_version = Version('1.1', True)
__version__ = _version.pretty_version
__canonical_version__ = _version.canonical_version
