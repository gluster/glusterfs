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

import java.net.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.io.*;
import java.util.HashMap;
import java.util.TreeMap;
import java.util.ArrayList;
import java.util.Iterator;

import org.apache.hadoop.fs.BlockLocation;

public class GlusterFSXattr {

	public enum LAYOUT { D, S, R, DS, DR, SR, DSR }
        public enum CMD { GET_HINTS, GET_REPLICATION, GET_BLOCK_SIZE, CHECK_FOR_QUICK_IO }

        private static String hostname;

        public GlusterFSXattr() { }

        public static String brick2host (String brick)
        throws IOException {
                String[] hf = null;

                hf = brick.split(":");
                if (hf.length != 2) {
                        System.out.println("brick not of format hostname:path");
                        throw new IOException("Error getting hostname from brick");
                }

                return hf[0];
        }

        public static String brick2file (String brick)
        throws IOException {
                String[] hf = null;

                hf = brick.split(":");
                if (hf.length != 2) {
                        System.out.println("brick not of format hostname:path");
                        throw new IOException("Error getting hostname from brick");
                }

                return hf[1];
        }

        public static BlockLocation[] getPathInfo (String filename, long start, long len)
                throws IOException {
                HashMap<String, ArrayList<String>> vol = null;
                HashMap<String, Integer> meta          = new HashMap<String, Integer>();

                vol = execGetFattr(filename, meta, CMD.GET_HINTS);

                return getHints(vol, meta, start, len, null);
        }

        public static long getBlockSize (String filename)
                throws IOException {
                HashMap<String, ArrayList<String>> vol = null;
                HashMap<String, Integer> meta          = new HashMap<String, Integer>();

                vol = execGetFattr(filename, meta, CMD.GET_BLOCK_SIZE);

                if (!meta.containsKey("block-size"))
                        return 0;

                return (long) meta.get("block-size");

        }

        public static short getReplication (String filename)
                throws IOException {
                HashMap<String, ArrayList<String>> vol = null;
                HashMap<String, Integer> meta          = new HashMap<String, Integer>();

                vol = execGetFattr(filename, meta, CMD.GET_REPLICATION);

                return (short) getReplicationFromLayout(vol, meta);

        }

        public static TreeMap<Integer, GlusterFSBrickClass> quickIOPossible (String filename, long start,
                                                                             long len)
                throws IOException {
                String                   realpath      = null;
                HashMap<String, ArrayList<String>> vol = null;
                HashMap<String, Integer> meta          = new HashMap<String, Integer>();
                TreeMap<Integer, GlusterFSBrickClass> hnts = new TreeMap<Integer, GlusterFSBrickClass>();

                vol = execGetFattr(filename, meta, CMD.GET_HINTS);
                getHints(vol, meta, start, len, hnts);

                if (hnts.size() == 0)
                        return null; // BOOM !!

                // DEBUG - dump hnts here
                return hnts;
        }

        public static HashMap<String, ArrayList<String>> execGetFattr (String filename,
                                                                       HashMap<String, Integer> meta,
                                                                       CMD cmd)
                throws IOException {
                Process        p              = null;
                BufferedReader brInput        = null;
                String         s              = null;
                String         cmdOut         = null;
                String         getfattrCmd    = null;
                String         xlator         = null;
                String         enclosingXl    = null;
                String         enclosingXlVol = null;
                String         key            = null;
                String         layout         = "";
                int            rcount         = 0;
                int            scount         = 0;
                int            dcount         = 0;
                int            count          = 0;

                HashMap<String, ArrayList<String>> vol = new HashMap<String, ArrayList<String>>();

                getfattrCmd = "getfattr -m . -n trusted.glusterfs.pathinfo " + filename;

                p = Runtime.getRuntime().exec(getfattrCmd);
                brInput = new BufferedReader(new InputStreamReader(p.getInputStream()));

                cmdOut = "";
                while ( (s = brInput.readLine()) != null )
                        cmdOut += s;

                /**
                 * TODO: Use a single regex for extracting posix paths as well
                 * as xlator counts for layout matching.
                 */

                Pattern pattern = Pattern.compile("<(.*?)[:\\(](.*?)>");
                Matcher matcher = pattern.matcher(cmdOut);

                Pattern p_px = Pattern.compile(".*?:(.*)");
                Matcher m_px;
                String gibberish_path;

                s = null;
                while (matcher.find()) {
                        xlator = matcher.group(1);
                        if (xlator.equalsIgnoreCase("posix")) {
                                if (enclosingXl.equalsIgnoreCase("replicate"))
                                        count = rcount;
                                else if (enclosingXl.equalsIgnoreCase("stripe"))
                                        count = scount;
                                else if (enclosingXl.equalsIgnoreCase("distribute"))
                                        count = dcount;
                                else
                                        throw new IOException("Unknown Translator: " + enclosingXl);

                                key = enclosingXl + "-" + count;

                                if (vol.get(key) == null)
                                        vol.put(key, new ArrayList<String>());

                                gibberish_path = matcher.group(2);

                                /* extract posix path from the gibberish string */
                                m_px = p_px.matcher(gibberish_path);
                                if (!m_px.find())
                                        throw new IOException("Cannot extract posix path");

                                vol.get(key).add(m_px.group(1));
                                continue;
                        }

                        enclosingXl = xlator;
                        enclosingXlVol = matcher.group(2);

                        if (xlator.equalsIgnoreCase("replicate"))
                                if (rcount++ != 0)
                                        continue;

                        if (xlator.equalsIgnoreCase("stripe")) {
                                if (scount++ != 0)
                                        continue;


                                Pattern ps = Pattern.compile("\\[(\\d+)\\]");
                                Matcher ms = ps.matcher(enclosingXlVol);

                                if (ms.find()) {
                                        if (((cmd == CMD.GET_BLOCK_SIZE) || (cmd == CMD.GET_HINTS))
                                            && (meta != null))
                                            meta.put("block-size", Integer.parseInt(ms.group(1)));
                                } else
                                        throw new IOException("Cannot get stripe size");
                        }

                        if (xlator.equalsIgnoreCase("distribute"))
                                if (dcount++ != 0)
                                        continue;

                        layout += xlator.substring(0, 1);
                }

                if ((dcount == 0) && (scount == 0) && (rcount == 0))
                        throw new IOException("Cannot get layout");

                if (meta != null) {
                        meta.put("dcount", dcount);
                        meta.put("scount", scount);
                        meta.put("rcount", rcount);
                }

                vol.put("layout", new ArrayList<String>(1));
                vol.get("layout").add(layout);

                return vol;
        }

        static BlockLocation[] getHints (HashMap<String, ArrayList<String>> vol,
                                         HashMap<String, Integer> meta,
                                         long start, long len,
                                         TreeMap<Integer, GlusterFSBrickClass> hnts)
                throws IOException {
                String            brick         = null;
                String            key           = null;
                boolean           done          = false;
                int               i             = 0;
                int               counter       = 0;
                int               stripeSize    = 0;
                long              stripeStart   = 0;
                long              stripeEnd     = 0;
                int               nrAllocs      = 0;
                int               allocCtr      = 0;
                BlockLocation[] result          = null;
                ArrayList<String> brickList     = null;
                ArrayList<String> stripedBricks = null;
                Iterator<String>  it            = null;

                String[] blks = null;
                GlusterFSBrickRepl[] repl = null;
                int dcount, scount, rcount;

                LAYOUT l = LAYOUT.valueOf(vol.get("layout").get(0));
                dcount = meta.get("dcount");
                scount = meta.get("scount");
                rcount = meta.get("rcount");

                switch (l) {
                case D:
                        key = "DISTRIBUTE-" + dcount;
                        brick = vol.get(key).get(0);

                        if (hnts == null) {
                                result = new BlockLocation[1];
                                result[0] = new BlockLocation(null, new String[] {brick2host(brick)}, start, len);
                        } else
                                hnts.put(0, new GlusterFSBrickClass(brick, start, len, false, -1, -1, -1));
                        break;

                case R:
                case DR:
                        /* just the name says it's striped - the volume isn't */
                        stripedBricks = new ArrayList<String>();

                        for (i = 1; i <= rcount; i++) {
                                key = "REPLICATE-" + i;
                                brickList = vol.get(key);
                                it = brickList.iterator();
                                while (it.hasNext()) {
                                        stripedBricks.add(it.next());
                                }
                        }

                        nrAllocs = stripedBricks.size();
                        if (hnts == null) {
                                result = new BlockLocation[1];
                                blks = new String[nrAllocs];
                        }

                        for (i = 0; i < nrAllocs; i++) {
                                if (hnts == null)
                                        blks[i] = brick2host(stripedBricks.get(i));
                                else
                                        hnts.put(i, new GlusterFSBrickClass(stripedBricks.get(i), start, len, false, -1, -1, -1));
                        }

                        if (hnts == null)
                                result[0] = new BlockLocation(null, blks, start, len);

                        break;

                case SR:
                case DSR:
                        int rsize = 0;
                        ArrayList<ArrayList<String>> replicas = new ArrayList<ArrayList<String>>();

                        stripedBricks = new ArrayList<String>();

                        if (rcount == 0)
                                throw new IOException("got replicated volume with replication count 0");

                        for (i = 1; i <= rcount; i++) {
                                key = "REPLICATE-" + i;
                                brickList = vol.get(key);
                                it = brickList.iterator();
                                replicas.add(i - 1, new ArrayList<String>());
                                while (it.hasNext()) {
                                        replicas.get(i - 1).add(it.next());
                                }
                        }

                        stripeSize = meta.get("block-size");

                        nrAllocs = (int) (((len - start) / stripeSize) + 1);
                        if (hnts == null) {
                                result = new BlockLocation[nrAllocs];
                                repl = new GlusterFSBrickRepl[nrAllocs];
                        }

                        // starting stripe position
                        counter = (int) ((start / stripeSize) % rcount);
                        stripeStart = start;

                        key = null;
                        int currAlloc = 0;
                        boolean hntsDone = false;
                        while ((stripeStart < len) && !done) {
                                stripeEnd = (stripeStart - (stripeStart % stripeSize)) + stripeSize - 1;
                                if (stripeEnd > start + len) {
                                        stripeEnd = start + len - 1;
                                        done = true;
                                }

                                rsize = replicas.get(counter).size();

                                if (hnts == null)
                                        repl[allocCtr] = new GlusterFSBrickRepl(rsize, stripeStart, (stripeEnd - stripeStart));

                                for (i = 0; i < rsize; i++) {
                                        brick = replicas.get(counter).get(i);
                                        currAlloc = (allocCtr * rsize) + i;

                                        if (hnts == null)
                                                repl[allocCtr].addHost(brick2host(brick));
                                        else
                                                if (currAlloc <= (rsize * rcount) - 1) {
                                                        hnts.put(currAlloc, new GlusterFSBrickClass(brick, stripeStart,
                                                                                                    (stripeEnd - stripeStart),
                                                                                                    true, stripeSize, rcount, rsize));
                                                } else
                                                        hntsDone = true;
                                }

                                if (hntsDone)
                                        break;

                                stripeStart = stripeEnd + 1;

                                allocCtr++;
                                counter++;

                                if (counter >= replicas.size())
                                        counter = 0;
                        }

                        if (hnts == null)
                                for (int k = 0; k < nrAllocs; k++)
                                        result[k] = new BlockLocation(null, repl[k].getReplHosts(), repl[k].getStartLen(), repl[k].getOffLen());

                        break;

                case S:
                case DS:
                        if (scount == 0)
                                throw new IOException("got striped volume with stripe count 0");

                        stripedBricks = new ArrayList<String>();
                        stripeSize = meta.get("block-size");

                        key = "STRIPE-" + scount;
                        brickList = vol.get(key);
                        it = brickList.iterator();
                        while (it.hasNext()) {
                                stripedBricks.add(it.next());
                        }

                        nrAllocs = (int) ((len - start) / stripeSize) + 1;
                        if (hnts == null)
                                result = new BlockLocation[nrAllocs];

                        // starting stripe position
                        counter = (int) ((start / stripeSize) % stripedBricks.size());
                        stripeStart = start;

                        key = null;
                        while ((stripeStart < len) && !done) {
                                brick = stripedBricks.get(counter);

                                stripeEnd = (stripeStart - (stripeStart % stripeSize)) + stripeSize - 1;
                                if (stripeEnd > start + len) {
                                        stripeEnd = start + len - 1;
                                        done = true;
                                }

                                if (hnts == null)
                                        result[allocCtr] = new BlockLocation(null, new String[] {brick2host(brick)},
                                                                             stripeStart, (stripeEnd - stripeStart));
                                else
                                        if (allocCtr <= stripedBricks.size()) {
                                                hnts.put(allocCtr, new GlusterFSBrickClass(brick, stripeStart, (stripeEnd - stripeStart),
                                                                                           true, stripeSize, stripedBricks.size(), -1));
                                        } else
                                                break;

                                stripeStart = stripeEnd + 1;

                                counter++;
                                allocCtr++;

                                if (counter >= stripedBricks.size())
                                        counter = 0;
                        }

                        break;
                }

                return result;
        }

        /* TODO: use meta{dcount,scount,rcount} for checking */
        public static int getReplicationFromLayout (HashMap<String, ArrayList<String>> vol,
                                                    HashMap<String, Integer> meta)
                throws IOException {
                int replication = 0;
                LAYOUT l = LAYOUT.valueOf(vol.get("layout").get(0));

                switch (l) {
                case D:
                case S:
                case DS:
                        replication = 1;
                        break;

                case R:
                case DR:
                case SR:
                case DSR:
                        final String key = "REPLICATION-1";
                        replication = vol.get(key).size();
                }

                return replication;
        }
}
