# This spec file should be used for testing before any release
# 

# Namespace posix
volume brick-ns
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export-ns        # Export this directory
end-volume

# 1st server

volume brick1
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export1        # Export this directory
end-volume

# == Posix-Locks ==
 volume plocks1
   type features/posix-locks
#  option mandatory on
   subvolumes brick1
 end-volume

volume iot1
  type performance/io-threads
  subvolumes plocks1 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb1
  type performance/write-behind
  subvolumes iot1
# option <key> <value>
end-volume

volume ra1
  type performance/read-ahead
  subvolumes wb1
# option <key> <value>
end-volume

volume brick2
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export2        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash2
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick2
# end-volume

# == Posix-Locks ==
volume plocks2
  type features/posix-locks
#  option <something> <something>
  subvolumes brick2
end-volume

volume iot2
  type performance/io-threads
  subvolumes plocks2 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb2
  type performance/write-behind
  subvolumes iot2
# option <key> <value>
end-volume

volume ra2
  type performance/read-ahead
  subvolumes wb2
# option <key> <value>
end-volume

volume brick3
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export3        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash3
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick3
# end-volume

# == Posix-Locks ==
volume plocks3
  type features/posix-locks
#  option <something> <something>
  subvolumes brick3
end-volume

volume iot3
  type performance/io-threads
  subvolumes plocks3 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb3
  type performance/write-behind
  subvolumes iot3
# option <key> <value>
end-volume

volume ra3
  type performance/read-ahead
  subvolumes wb3
# option <key> <value>
end-volume

volume brick4
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export4        # Export this directory
end-volume

# == Posix-Locks ==
volume plocks4
  type features/posix-locks
# option <something> <something>
  subvolumes brick4
end-volume

volume iot4
  type performance/io-threads
  subvolumes plocks4 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb4
  type performance/write-behind
  subvolumes iot4
# option <key> <value>
end-volume

volume ra4
  type performance/read-ahead
  subvolumes wb4
# option <key> <value>
end-volume

volume brick5
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export5        # Export this directory
end-volume


# == Posix-Locks ==
volume plocks5
  type features/posix-locks
# option <something> <something>
  subvolumes brick5
end-volume

volume iot5
  type performance/io-threads
  subvolumes plocks5 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb5
  type performance/write-behind
  subvolumes iot5
# option <key> <value>
end-volume

volume ra5
  type performance/read-ahead
  subvolumes wb5
# option <key> <value>
end-volume

volume brick6
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export6        # Export this directory
end-volume

# == Posix-Locks ==
volume plocks6
  type features/posix-locks
#   option <something> <something>
  subvolumes brick6
end-volume

volume iot6
  type performance/io-threads
  subvolumes plocks6 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb6
  type performance/write-behind
  subvolumes iot6
# option <key> <value>
end-volume

volume ra6
  type performance/read-ahead
  subvolumes wb6
# option <key> <value>
end-volume

volume brick7
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export7        # Export this directory
end-volume

# == Posix-Locks ==
volume plocks7
  type features/posix-locks
#   option <something> <something>
  subvolumes brick7
end-volume

volume iot7
  type performance/io-threads
  subvolumes plocks7 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb7
  type performance/write-behind
  subvolumes iot7
# option <key> <value>
end-volume

volume ra7
  type performance/read-ahead
  subvolumes wb7
# option <key> <value>
end-volume

volume brick8
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export8        # Export this directory
end-volume

# == Posix-Locks ==
volume plocks8
  type features/posix-locks
#   option <something> <something>
  subvolumes brick8
end-volume

volume iot8
  type performance/io-threads
  subvolumes plocks8 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb8
  type performance/write-behind
  subvolumes iot8
# option <key> <value>
end-volume

volume ra8
  type performance/read-ahead
  subvolumes wb8
# option <key> <value>
end-volume

volume server8
  type protocol/server
  subvolumes ra8 ra1 ra2 ra3 ra4 ra5 ra6 ra7 brick-ns
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option client-volume-filename /examples/qa-client.vol
  option auth.addr.ra1.allow * # Allow access to "stat8" volume
  option auth.addr.ra2.allow * # Allow access to "stat8" volume
  option auth.addr.ra3.allow * # Allow access to "stat8" volume
  option auth.addr.ra4.allow * # Allow access to "stat8" volume
  option auth.addr.ra5.allow * # Allow access to "stat8" volume
  option auth.addr.ra6.allow * # Allow access to "stat8" volume
  option auth.addr.ra7.allow * # Allow access to "stat8" volume
  option auth.addr.ra8.allow * # Allow access to "stat8" volume
  option auth.addr.brick-ns.allow * # Allow access to "stat8" volume
end-volume

