#On-Wire Compression + Decompression

The 'compression translator' compresses and decompresses data in-flight
between client and bricks.

###Working
When a writev call occurs, the client compresses the data before sending it to
brick. On the brick, compressed data is decompressed. Similarly, when a readv
call occurs, the brick compresses the data before sending it to client. On the
client, the compressed data is decompressed. Thus, the amount of data sent over
the wire is minimized. Compression/Decompression is done using Zlib library.

During normal operation, this is the format of data sent over wire:

~~~
<compressed-data> + trailer(8 bytes)
~~~

The trailer contains the CRC32 checksum and length of original uncompressed
data. This is used for validation.

###Usage

Turning on compression xlator:

~~~
gluster volume set <vol_name> network.compression on
~~~

###Configurable parameters (optional)

**Compression level**
~~~
gluster volume set <vol_name> network.compression.compression-level 8
~~~

~~~
0  : no compression
1  : best speed
9  : best compression
-1 : default compression
~~~

**Minimum file size**

~~~
gluster volume set <vol_name> network.compression.min-size 50
~~~

Data is compressed only when its size exceeds the above value in bytes.

**Other paramaters**

Other less frequently used parameters include `network.compression.mem-level`
and `network.compression.window-size`. More details can about these options
can be found by running `gluster volume set help` command.

###Known Issues and Limitations

* Compression translator cannot work with striped volumes.
* Mount point hangs when writing a file with write-behind xlator turned on. To
overcome this, turn off `performance.write-behind` entirely OR
set`performance.strict-write-ordering` to on.
* For glusterfs versions <= 3.5, compression traslator can ONLY work with pure
distribute volumes. This limitation is caused by AFR not being able to
propagate xdata. This issue has been fixed in glusterfs versions > 3.5

###TODO
Although zlib offers high compression ratio, it is very slow. We can make the
translator pluggable to add support for other compression methods such as
[lz4 compression](https://code.google.com/p/lz4/)
