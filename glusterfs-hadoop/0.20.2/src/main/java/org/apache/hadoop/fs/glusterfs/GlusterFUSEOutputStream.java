/**
 *
 * Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
 * This file is part of GlusterFS.
 *
 * Licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 */

package org.apache.hadoop.fs.glusterfs;

import java.io.*;

import org.apache.hadoop.fs.FSOutputSummer;
import org.apache.hadoop.fs.FileSystem;

public class GlusterFUSEOutputStream extends OutputStream {
        File         f;
        long         pos;
        boolean      closed;
        OutputStream fuseOutputStream;

        public GlusterFUSEOutputStream (String file, boolean append) throws
                IOException {
                this.f = new File(file); /* not needed ? */
                this.pos = 0;
                this.fuseOutputStream = new FileOutputStream(file, append);
                this.closed = false;
        }

        public long getPos () throws IOException {
                return pos;
        }

        public void write (int v) throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                byte[] b = new byte[1];
                b[0] = (byte) v;

                write(b, 0, 1);
        }

        public void write (byte b[]) throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                fuseOutputStream.write(b, 0, b.length);
                pos += (long) b.length;
        }

        public void write (byte b[], int off, int len) throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                fuseOutputStream.write(b, off, len);
                pos += (long) len;
        }

        public void flush () throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                fuseOutputStream.flush();
        }

        public void close () throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                flush();
                fuseOutputStream.close();
                closed = true;
        }
}
