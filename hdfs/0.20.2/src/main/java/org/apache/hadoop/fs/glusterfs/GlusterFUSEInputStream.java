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
import java.util.TreeMap;

import org.apache.hadoop.fs.FSInputStream;
import org.apache.hadoop.fs.FileSystem;


public class GlusterFUSEInputStream extends FSInputStream {
        File                                  f;
        boolean                               lastActive;
        long                                  pos;
        boolean                               closed;
        String                                thisHost;
        RandomAccessFile                      fuseInputStream;
        RandomAccessFile                      fsInputStream;
        GlusterFSBrickClass                   thisBrick;
        int                                   nodeLocation;
        TreeMap<Integer, GlusterFSBrickClass> hnts;

        public GlusterFUSEInputStream (File f, TreeMap<Integer, GlusterFSBrickClass> hnts,
                                       String hostname) throws IOException {
                this.f = f;
                this.pos = 0;
                this.closed = false;
                this.hnts = hnts;
                this.thisHost = hostname;
                this.fsInputStream = null;
                this.fuseInputStream = new RandomAccessFile(f.getPath(), "r");

                this.lastActive = true; // true == FUSE, false == backed file

                String directFilePath = null;
                if (this.hnts != null) {
                        directFilePath = findLocalFile(f.getPath(), this.hnts);
                        if (directFilePath != null) {
                                this.fsInputStream = new RandomAccessFile(directFilePath, "r");
                                this.lastActive = !this.lastActive;
                        }
                }
        }

        public String findLocalFile (String path, TreeMap<Integer, GlusterFSBrickClass> hnts) {
                int i = 0;
                String actFilePath = null;
                GlusterFSBrickClass gfsBrick = null;

                gfsBrick = hnts.get(0);

                /* do a linear search for the matching host not worrying
                   about file stripes */
                for (i = 0; i < hnts.size(); i++) {
                        gfsBrick = hnts.get(i);
                        actFilePath = gfsBrick.brickIsLocal(this.thisHost);
                        if (actFilePath != null) {
                                this.thisBrick = gfsBrick;
                                this.nodeLocation = i;
                                break;
                        }
                }

                return actFilePath;
        }

        public long getPos () throws IOException {
                return pos;
        }

        public synchronized int available () throws IOException {
                return (int) ((f.length()) - getPos());
        }

        public void seek (long pos) throws IOException {
                fuseInputStream.seek(pos);
                if (fsInputStream != null)
                        fsInputStream.seek(pos);
        }

        public boolean seekToNewSource (long pos) throws IOException {
                return false;
        }

        public RandomAccessFile chooseStream (long start, int[] nlen)
                throws IOException {
                GlusterFSBrickClass gfsBrick = null;
                RandomAccessFile in = fuseInputStream;
                boolean oldActiveStream = lastActive;
                lastActive = true;

                if ((hnts != null) && (fsInputStream != null)) {
                        gfsBrick = hnts.get(0);
                        if (!gfsBrick.isChunked()) {
                                in = fsInputStream;
                                lastActive = false;
                        } else {
                                // find the current location in the tree and the amount of data it can serve
                                int[] nodeInTree = thisBrick.getBrickNumberInTree(start, nlen[0]);

                                // does this node hold the byte ranges we have been requested for ?
                                if ((nodeInTree[2] != 0) && thisBrick.brickHasFilePart(nodeInTree[0], nodeLocation)) {
                                        in = fsInputStream;
                                        nlen[0] = nodeInTree[2]; // the amount of data that can be read from the stripe
                                        lastActive = false;
                                }
                        }
                }

                return in;
        }

        public synchronized int read () throws IOException {
                int byteRead = 0;
                RandomAccessFile in = null;

                if (closed)
                        throw new IOException("Stream Closed.");

                int[] nlen = { 1 };

                in = chooseStream(getPos(), nlen);

                byteRead = in.read();
                if (byteRead >= 0) {
                        pos++;
                        syncStreams(1);
                }

                return byteRead;
        }

        public synchronized int read (byte buff[], int off, int len) throws
                IOException {
                int result = 0;
                RandomAccessFile in = null;

                if (closed)
                        throw new IOException("Stream Closed.");

                int[] nlen = {len}; // hack to make len mutable
                in = chooseStream(pos, nlen);

                result = in.read(buff, off, nlen[0]);
                if (result > 0) {
                        pos += result;
                        syncStreams(result);
                }

                return result;
        }

        /**
         * TODO: use seek() insted of skipBytes(); skipBytes does I/O
         */
        public void syncStreams (int bytes) throws IOException {
                if ((hnts != null) && (hnts.get(0).isChunked()) && (fsInputStream != null))
                        if (!this.lastActive)
                                fuseInputStream.skipBytes(bytes);
                        else
                                fsInputStream.skipBytes(bytes);
        }

        public synchronized void close () throws IOException {
                if (closed)
                        throw new IOException("Stream closed.");

                super.close();
                if (fsInputStream != null)
                        fsInputStream.close();
                fuseInputStream.close();

                closed = true;
        }

        // Not supported - mark () and reset ()

        public boolean markSupported () {
                return false;
        }

        public void mark (int readLimit) {}

        public void reset () throws IOException {
                throw new IOException("Mark/Reset not supported.");
        }
}
