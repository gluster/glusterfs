import os
import os.path
import sys

fl = os.listdir(os.path.dirname(sys.argv[0]) or '.')
fl.sort()
for f in fl:
    if f[-3:] != '.py' or f[0] == '_':
        continue
    m = f[:-3]
    sys.stdout.write('importing %s ...' %  m)
    __import__(m)
    print(' OK.')

def sys_argv_set(a):
    sys.argv = sys.argv[:1] + a

gsyncd = sys.modules['gsyncd']
for a in [['--help'], ['--version'], ['--canonicalize-escape-url', '/foo']]:
    print('>>> invoking program with args: %s' % ' '.join(a))
    pid = os.fork()
    if not pid:
        sys_argv_set(a)
        gsyncd.main()
    _, r = os.waitpid(pid, 0)
    if r:
        raise RuntimeError('invocation failed')
