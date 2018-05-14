## How to write your Cloudsync Plugin

### Background

Cloudsync translator is part of the archival feature in Gluster. This translator
does the retrieval/download part. Each cold  file will be archived to a remote
storage (public or private cloud). On future access to the file, it will be
retrieved from the remote storage by Cloudsync translator. Each remote storage
would need a unique plugin. Cloudsync translator will load this plugin and
call the necessary plugin functions.

Upload can be done by a script or program. There are some basic mandatory steps
for uploading the data. There is a sample script for crawl and upload given at
the end of this guide.

### Necessary changes to create a plugin

1. Define store_methods:

* This structure is the container of basic functions that will be called by
  cloudsync xlator.

        typedef struct store_methodds {
                int (*fop_download) (call_frame_t *frame, void *config);
                /* return type should be the store config */
                void *(*fop_init) (xlator_t *this);
                int (*fop_reconfigure) (xlator_t *this, dict_t *options);
                void (*fop_fini) (void *config);
        } store_methods_t;


        Member details:
        fop_download:
                This is the download function pointer.

                frame: This will have the fd to write data downloaded from
                       cloud to GlusterFS.(frame->local->fd)

                config: This is the plugin configuration variable.

                Note: Structure cs_local_t has member dlfd and dloffset which
                      can be used to manage the writes to Glusterfs.
                      Include cloudsync-common.h to access these structures.

        fop_init:
                This is similar to xlator init. But here the return value is
                the plugin configuration pointer. This pointer will be stored
                in the cloudsync private object (priv->stores->config). And
                the cloudsync private object can be accessed by "this->private"
                where "this" is of type xlator_t.

        fop_reconfigure:
                This is similar to xlator_reconfigure.

        fop_fini:
                Free plugin resources.

        Note: Store_methods_t is part of cs_private_t which in turn part of
              xlator_t. Create a store_methods_t object named "store_ops" in
              your plugin. For example

              store_methods_t store_ops = {
                .fop_download = aws_download_s3,
                .fop_init     = aws_init,
                .fop_reconfigure = aws_reconfigure,
                .fop_fini     = aws_fini,
              };


2 - Making Cloudsync xlator aware of the plugin:

        Add an entry in to the cs_plugin structure. For example
        struct cs_plugin plugins[] = {
                {
                  .name = "amazons3",
                  .library = "libamazons3.so",
                  .description = "amazon s3 store."
                },

                {.name = NULL},
        };

        Description about individual members:
                name: name of the plugin
                library: This is the shared object created. Cloudsync will load
                         this library during init.
                description: Describe about the plugin.

3- Makefile Changes in Cloudsync:

		Add <plugin.la> to cloudsync_la_LIBADD variable.

4 - Configure.ac changes:

                In cloudsync section add the necessary dependency checks for
                the plugin.

5 - Export symbols:

        Cloudsync needs "store_ops" to resolve all plugin functions.
        Create a file <plugin>.sym and add write "store_ops" to it.


### Sample script for upload
This script assumes amazon s3 is the target cloud and bucket name is
gluster-bucket. User can do necessary aws configuration using command
"aws configure". Currently for amazons3 there are four gluster settings
available.
1- features.s3plugin-seckey -> s3 secret key
2- features.s3plugin-keyid -> s3 key id
3- features.s3plugin-bucketid -> bucketid
4- features.s3plugin-hostname -> hostname e.g. s3.amazonaws.com

Additionally set cloudsync storetype to amazons3.

gluster v set <VOLNAME> cloudsync-storetype amazons3

Now create a mount dedicated for this upload task.

That covers necessary configurations needed.

Below is the sample script for upload. The script will crawl directly on the
brick and will upload those files which are not modified for last one month.
It needs two arguments.
1st arguement - Gluster Brick path
2nd arguement - coldness that is how many days since the file was modified.
3rd argument - dedicated gluster mount point created for uploading.

Once the cloud setup is done, run the following script on individual bricks.
Note: For an AFR volume, pick only the fully synchronized brick among the
replica bricks.

```
target_folder=$1
coldness=$2
mnt=$3

cd $target_folder
for i in `find . -type f  | grep -v "glusterfs" | sed 's/..//'`
do
        echo "processing $mnt/$i"

        #check whether the file is already archived
        getfattr -n trusted.glusterfs.cs.remote $i &> /dev/null
        if [ $? -eq 0 ]
        then
                echo "file $mnt/$i is already archived"
        else
                #upload to cloud
                aws s3 cp $mnt/$i s3://gluster-bucket/
                mtime=`stat -c "%Y" $mnt/$i`

                #post processing of upload
                setfattr -n trusted.glusterfs.csou.complete -v $mtime $mnt/$i
                if [ $? -ne 0 ]
                then
                        echo "archiving of file $mnt/$i failed"
                else
                        echo "archiving of file $mnt/$i succeeded"
                fi

        fi
done
```
