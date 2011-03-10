try:
    # py 3
    from urllib import parse as urllib
except ImportError:
    import urllib

def escape(s):
    return urllib.quote_plus(s)

def unescape(s):
    return urllib.unquote_plus(s)
