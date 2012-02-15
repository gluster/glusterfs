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

public class GlusterFSBrickRepl {
        private String[] replHost;
        private long start;
        private long len;
        private int cnt;

        GlusterFSBrickRepl(int replCount, long start, long len) {
                this.replHost = new String[replCount];
                this.start = start;
                this.len = len;
                this.cnt = 0;
        }

        public void addHost (String host) {
                this.replHost[cnt++] = host;
        }

        public String[] getReplHosts () {
                return this.replHost;
        }

        public long getStartLen () {
                return this.start;
        }

        public long getOffLen () {
                return this.len;
        }
}
